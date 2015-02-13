// Package packet is for TCP and UDP packets used in eX0 networking protocol.
package packet

const MAX_UDP_SIZE = 1448

type Type uint8

const (
	JoinServerRequestType        Type = 1
	JoinServerAcceptType         Type = 2
	JoinServerRefuseType         Type = 3
	UdpConnectionEstablishedType Type = 5
	EnterGamePermissionType      Type = 6
	EnteredGameNotificationType  Type = 7
	LoadLevelType                Type = 20
	CurrentPlayersInfoType       Type = 21
	LocalPlayerInfoType          Type = 30

	HandshakeType Type = 100
	PingType      Type = 10
	PongType      Type = 11
	PungType      Type = 12
)

//go:generate stringer -type=Type

type TcpHeader struct {
	Length uint16
	Type   Type
}

type JoinServerRequest struct {
	TcpHeader

	Version    uint16
	Passphrase [16]byte
	Signature  uint64
}

type JoinServerAccept struct {
	TcpHeader

	YourPlayerId     uint8
	TotalPlayerCount uint8
}

type JoinServerRefuse struct {
	TcpHeader

	RefuseReason uint8
}

type UdpConnectionEstablished struct {
	TcpHeader
}

type LoadLevel struct {
	TcpHeader

	LevelFilename []byte
}

type CurrentPlayersInfo struct {
	TcpHeader

	Players []PlayerInfo
}

type PlayerInfo struct {
	NameLength uint8
	Name       []byte
	Team       uint8
	State      *State // If Team != 2.
}

type State struct {
	LastCommandSequenceNumber uint8
	X                         float32
	Y                         float32
	Z                         float32
}

type EnterGamePermission struct {
	TcpHeader
}

type EnteredGameNotification struct {
	TcpHeader
}

type LocalPlayerInfo struct {
	TcpHeader

	NameLength  uint8
	Name        []byte
	CommandRate uint8
	UpdateRate  uint8
}

type UdpHeader struct {
	Type Type
}

type Handshake struct {
	UdpHeader

	Signature uint64
}

type Ping struct {
	UdpHeader

	PingData    [4]byte
	LastLatency []uint16
}

type Pong struct {
	UdpHeader

	PingData [4]byte
}

type Pung struct {
	UdpHeader

	PingData [4]byte
}