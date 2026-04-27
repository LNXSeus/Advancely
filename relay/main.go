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
)

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
				_ = writeFrame(r, MsgRoomClosed, nil)
				r.Close()
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
		room.AddReceiver(conn)
		defer func() {
			room.RemoveReceiver(conn)
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
func pumpHost(host net.Conn, room *Room) {
	for {
		f, err := readFrame(host)
		if err != nil {
			return
		}
		for _, r := range room.ReceiverList() {
			if err := writeFrame(r, f.Type, f.Payload); err != nil {
				r.Close()
				room.RemoveReceiver(r)
			}
		}
	}
}

// pumpReceiver forwards every frame a receiver sends to the room host.
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