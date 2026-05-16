// Copyright (c) 2026 LNXSeus. All Rights Reserved.
//
// This project is proprietary software. You are granted a license to use the software as-is.
// You may not copy, distribute, modify, reverse-engineer, maintain a fork, or use this software
// or its source code in any way without the express written permission of the copyright holder.

package main

import (
	"crypto/tls"
	"encoding/json"
	"flag"
	"log"
	"net"
	"sync"
)

// Buffered outbound queue per receiver. A slow consumer that can't drain this
// many frames before the next host broadcast arrives is treated as gone:
// dropping it preserves throughput for the rest of the room. PLAYER_STATES
// frames are large and infrequent, so 64 is comfortable headroom.
const receiverQueueSize = 64

func main() {
	addr := flag.String("addr", ":5842", "listen address")
	certFile := flag.String("cert", "cert.pem", "TLS cert (self-signed, pinned client-side)")
	keyFile := flag.String("key", "key.pem", "TLS private key")
	flag.Parse()

	cert, err := tls.LoadX509KeyPair(*certFile, *keyFile)
	if err != nil {
		log.Fatalf("load cert: %v", err)
	}
	tlsCfg := &tls.Config{
		Certificates: []tls.Certificate{cert},
		MinVersion:   tls.VersionTLS12,
	}

	ln, err := tls.Listen("tcp", *addr, tlsCfg)
	if err != nil {
		log.Fatalf("listen: %v", err)
	}
	defer ln.Close()
	log.Printf("relay listening on %s", *addr)

	rooms := NewRoomTable()

	for {
		conn, err := ln.Accept()
		if err != nil {
			log.Printf("accept: %v", err)
			continue
		}
		go handleConn(conn, rooms)
	}
}

// Receiver wraps a receiver-side TLS connection with a buffered outbound
// queue. Producers (the host fan-out loop) call trySend, which never blocks:
// if the queue is full the receiver is too slow to keep up and gets dropped,
// rather than holding up frames destined for healthy receivers in the same
// room. Each receiver has its own writer goroutine so a stalled write only
// stalls that one connection.
type Receiver struct {
	conn      net.Conn
	send      chan Frame
	done      chan struct{}
	closeOnce sync.Once
}

func newReceiver(c net.Conn) *Receiver {
	r := &Receiver{
		conn: c,
		send: make(chan Frame, receiverQueueSize),
		done: make(chan struct{}),
	}
	go r.writer()
	return r
}

func (r *Receiver) writer() {
	for {
		select {
		case <-r.done:
			return
		case f := <-r.send:
			if err := writeFrame(r.conn, f.Type, f.Payload); err != nil {
				r.shutdown()
				return
			}
		}
	}
}

// shutdown is idempotent and safe to call from any goroutine. After it
// returns, trySend never enqueues, and the writer exits as soon as it
// notices done is closed.
func (r *Receiver) shutdown() {
	r.closeOnce.Do(func() {
		close(r.done)
		r.conn.Close()
	})
}

// trySend never blocks. Returns false if the receiver is closed OR its
// outbound queue is full (== too slow to keep up). The caller should treat
// either failure mode the same: shut the receiver down and remove it from
// the room.
func (r *Receiver) trySend(f Frame) bool {
	select {
	case <-r.done:
		return false
	default:
	}
	select {
	case r.send <- f:
		return true
	default:
		return false
	}
}

func handleConn(conn net.Conn, rooms *RoomTable) {
	defer conn.Close()

	first, err := readFrame(conn)
	if err != nil {
		return
	}

	switch first.Type {
	case MsgListRooms:
		resp := roomListResp{Rooms: rooms.Snapshot()}
		body, _ := json.Marshal(resp)
		_ = writeFrame(conn, MsgRoomListResp, body)
		return

	case MsgCreateRoom:
		var req createRoomReq
		if err := json.Unmarshal(first.Payload, &req); err != nil {
			return
		}
		room := rooms.Create(conn, req.PasswordHash, req.MCVersion)
		defer func() {
			rooms.Remove(room.Code)
			for _, r := range room.ReceiverList() {
				_ = writeFrame(r.conn, MsgRoomClosed, nil)
				r.shutdown()
			}
		}()
		body, _ := json.Marshal(roomCreatedResp{Code: room.Code})
		if err := writeFrame(conn, MsgRoomCreated, body); err != nil {
			return
		}
		log.Printf("room created: %s", room.Code)
		pumpHost(conn, room)

	case MsgJoinRoom:
		var req joinRoomReq
		if err := json.Unmarshal(first.Payload, &req); err != nil {
			return
		}
		room := rooms.Get(req.Code)
		if room == nil {
			_ = writeFrame(conn, MsgJoinRoomDenied, []byte("room not found"))
			return
		}
		if room.PasswordHash != req.PasswordHash {
			_ = writeFrame(conn, MsgJoinRoomDenied, []byte("incorrect password"))
			return
		}
		recv := newReceiver(conn)
		room.AddReceiver(recv)
		defer func() {
			room.RemoveReceiver(recv)
			recv.shutdown()
		}()
		if err := writeFrame(conn, MsgJoinRoomOK, nil); err != nil {
			return
		}
		log.Printf("receiver joined room %s", room.Code)
		pumpReceiver(conn, room)

	default:
		return
	}
}

// pumpHost forwards every frame the host sends to all receivers in the room.
// The enqueue path is non-blocking: a receiver whose queue is full is too
// slow to keep up and gets dropped, so one stuck consumer never blocks
// delivery to the rest of the room.
func pumpHost(host net.Conn, room *Room) {
	for {
		f, err := readFrame(host)
		if err != nil {
			return
		}
		for _, r := range room.ReceiverList() {
			if !r.trySend(f) {
				r.shutdown()
				room.RemoveReceiver(r)
			}
		}
	}
}

// pumpReceiver forwards every frame a receiver sends to the room host.
// Host has a single conn (no fan-out), and Go's tls.Conn is safe for
// concurrent writes, so we write directly without queueing here.
func pumpReceiver(recv net.Conn, room *Room) {
	for {
		f, err := readFrame(recv)
		if err != nil {
			return
		}
		if err := writeFrame(room.Host, f.Type, f.Payload); err != nil {
			return
		}
	}
}