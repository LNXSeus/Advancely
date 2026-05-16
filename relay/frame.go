// Copyright (c) 2026 LNXSeus. All Rights Reserved.
//
// This project is proprietary software. You are granted a license to use the software as-is.
// You may not copy, distribute, modify, reverse-engineer, maintain a fork, or use this software
// or its source code in any way without the express written permission of the copyright holder.

package main

import (
	"encoding/binary"
	"io"
	"net"
	"time"
)

const (
	frameHeaderSize = 8

	// Base write deadline applied to every frame regardless of size.
	writeTimeoutBase = 15 * time.Second
	// Extra deadline budget per MB of payload. A ~1 Mbps residential uplink
	// needs ~8s to drain a megabyte; we double it for packet-loss headroom.
	writeTimeoutPerMB = 16 * time.Second
)

// No hard cap on transferred frame size. The uint32 length field is the
// only natural limit; the relay forwards frames opaquely and does not
// allocate against attacker-controlled sizes beyond what the wire encoding
// already allows.

type Frame struct {
	Type    uint32
	Payload []byte
}

func writeDeadlineFor(payloadLen int) time.Duration {
	if payloadLen < 0 {
		payloadLen = 0
	}
	extra := time.Duration(payloadLen) * writeTimeoutPerMB / (1024 * 1024)
	return writeTimeoutBase + extra
}

func readFrame(conn net.Conn) (Frame, error) {
	var hdr [frameHeaderSize]byte
	if _, err := io.ReadFull(conn, hdr[:]); err != nil {
		return Frame{}, err
	}
	msgType := binary.BigEndian.Uint32(hdr[0:4])
	length := binary.BigEndian.Uint32(hdr[4:8])
	payload := make([]byte, length)
	if length > 0 {
		if _, err := io.ReadFull(conn, payload); err != nil {
			return Frame{}, err
		}
	}
	return Frame{Type: msgType, Payload: payload}, nil
}

func writeFrame(conn net.Conn, msgType uint32, payload []byte) error {
	_ = conn.SetWriteDeadline(time.Now().Add(writeDeadlineFor(len(payload))))
	defer conn.SetWriteDeadline(time.Time{})

	var hdr [frameHeaderSize]byte
	binary.BigEndian.PutUint32(hdr[0:4], msgType)
	binary.BigEndian.PutUint32(hdr[4:8], uint32(len(payload)))
	if _, err := conn.Write(hdr[:]); err != nil {
		return err
	}
	if len(payload) > 0 {
		if _, err := conn.Write(payload); err != nil {
			return err
		}
	}
	return nil
}