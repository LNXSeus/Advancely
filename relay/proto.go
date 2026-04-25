package main

// Wire format: [4B type BE] [4B length BE] [payload]
// Types 1-13 are Advancely COOP_MSG_* frames forwarded transparently by the relay
// (see source/coop_net.h). Types 100+ are relay control frames defined here.

const (
	MsgCoopHeartbeat    = 1
	MsgCoopDisconnect   = 3
	MsgCoopStateUpdate  = 4
	MsgCoopJoinRequest  = 6
	MsgCoopPlayerStates = 12

	// Relay control frames
	MsgListRooms      = 100 // client -> relay: empty payload, requests room list
	MsgRoomListResp   = 101 // relay -> client: JSON {rooms:[{code, player_count, mc_version}]}
	MsgCreateRoom     = 102 // client -> relay: JSON {password_hash, mc_version}
	MsgRoomCreated    = 103 // relay -> client: JSON {code}
	MsgJoinRoom       = 104 // client -> relay: JSON {code, password_hash}
	MsgJoinRoomOK     = 105 // relay -> client: empty; client is now a receiver in that room
	MsgJoinRoomDenied = 106 // relay -> client: string reason, then close
	MsgRoomClosed     = 107 // relay -> client: empty; host disconnected or closed room
)

type createRoomReq struct {
	PasswordHash string `json:"password_hash"`
	MCVersion    string `json:"mc_version"`
}

type roomCreatedResp struct {
	Code string `json:"code"`
}

type joinRoomReq struct {
	Code         string `json:"code"`
	PasswordHash string `json:"password_hash"`
}

type roomListEntry struct {
	Code        string `json:"code"`
	PlayerCount int    `json:"player_count"`
	MCVersion   string `json:"mc_version"`
}

type roomListResp struct {
	Rooms []roomListEntry `json:"rooms"`
}