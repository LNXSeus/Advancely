// Copyright (c) 2026 LNXSeus. All Rights Reserved.
//
// This project is proprietary software. You are granted a license to use the software as-is.
// You may not copy, distribute, modify, reverse-engineer, maintain a fork, or use this software
// or its source code in any way without the express written permission of the copyright holder.

package main

import (
	"crypto/rand"
	"net"
	"sync"
	"time"
)

const roomCodeLen = 6

// Ambiguous characters (0/O, 1/I/L) excluded so codes read cleanly aloud.
var codeAlphabet = []byte("ABCDEFGHJKMNPQRSTUVWXYZ23456789")

type Room struct {
	Code         string
	PasswordHash string
	MCVersion    string
	Host         net.Conn
	Receivers    map[net.Conn]struct{}
	CreatedAt    time.Time
	mu           sync.Mutex
}

type RoomTable struct {
	mu    sync.Mutex
	rooms map[string]*Room
}

func NewRoomTable() *RoomTable {
	return &RoomTable{rooms: make(map[string]*Room)}
}

func (rt *RoomTable) Create(host net.Conn, passwordHash, mcVersion string) *Room {
	rt.mu.Lock()
	defer rt.mu.Unlock()
	for {
		code := randomCode()
		if _, taken := rt.rooms[code]; taken {
			continue
		}
		r := &Room{
			Code:         code,
			PasswordHash: passwordHash,
			MCVersion:    mcVersion,
			Host:         host,
			Receivers:    make(map[net.Conn]struct{}),
			CreatedAt:    time.Now(),
		}
		rt.rooms[code] = r
		return r
	}
}

func (rt *RoomTable) Get(code string) *Room {
	rt.mu.Lock()
	defer rt.mu.Unlock()
	return rt.rooms[code]
}

func (rt *RoomTable) Remove(code string) {
	rt.mu.Lock()
	defer rt.mu.Unlock()
	delete(rt.rooms, code)
}

func (rt *RoomTable) Snapshot() []roomListEntry {
	rt.mu.Lock()
	defer rt.mu.Unlock()
	out := make([]roomListEntry, 0, len(rt.rooms))
	for _, r := range rt.rooms {
		r.mu.Lock()
		out = append(out, roomListEntry{
			Code:        r.Code,
			PlayerCount: 1 + len(r.Receivers),
			MCVersion:   r.MCVersion,
		})
		r.mu.Unlock()
	}
	return out
}

func (r *Room) AddReceiver(c net.Conn) {
	r.mu.Lock()
	defer r.mu.Unlock()
	r.Receivers[c] = struct{}{}
}

func (r *Room) RemoveReceiver(c net.Conn) int {
	r.mu.Lock()
	defer r.mu.Unlock()
	delete(r.Receivers, c)
	return len(r.Receivers)
}

func (r *Room) ReceiverList() []net.Conn {
	r.mu.Lock()
	defer r.mu.Unlock()
	out := make([]net.Conn, 0, len(r.Receivers))
	for c := range r.Receivers {
		out = append(out, c)
	}
	return out
}

func randomCode() string {
	buf := make([]byte, roomCodeLen)
	raw := make([]byte, roomCodeLen)
	if _, err := rand.Read(raw); err != nil {
		panic(err)
	}
	for i, b := range raw {
		buf[i] = codeAlphabet[int(b)%len(codeAlphabet)]
	}
	return string(buf)
}