// +build !js

package main

import (
	"bytes"
	"encoding/binary"
	"fmt"
	"io"
	"io/ioutil"
	"log"
	"net"
	"net/http"
	"os"
	"sync"
	"time"

	"github.com/shurcooL/eX0/eX0-go/packet"
	"github.com/shurcooL/go-goon"
	"golang.org/x/net/websocket"
)

type server struct {
	logic *logic

	connectionsMu sync.Mutex
	connections   []*Connection

	lastPingData uint32

	// TODO: Consider moving these into *Connection or Player or something that will "go away" when that connection dies.
	//       That way no need to clear/remove these entries manually when that player is gone.
	pingSentTimesMu sync.Mutex
	pingSentTimes   []map[uint32]time.Time // Player ID -> (PingData -> Time).
	lastLatenciesMu sync.Mutex
	lastLatencies   []uint16 // Index is Player ID. Units are 0.1 ms.

	chanListener      chan *Connection
	chanListenerReply chan struct{}
}

const serverLevelFilename = "test3"

func startServer() *server {
	s := &server{
		chanListener:      make(chan *Connection),
		chanListenerReply: make(chan struct{}),
	}
	s.logic = startLogic()
	state.Lock()
	s.logic.TotalPlayerCount = 16
	s.pingSentTimes = make([]map[uint32]time.Time, s.logic.TotalPlayerCount)
	s.lastLatencies = make([]uint16, s.logic.TotalPlayerCount)
	state.Unlock()

	if level, err := newLevel(string(serverLevelFilename) + ".wwl"); err != nil {
		log.Fatalln("failed to load level:", err)
	} else {
		s.logic.level = level
	}

	go s.listenAndHandleTCP()
	go s.listenAndHandleUDP()
	go s.listenAndHandleWebSocket()
	go s.listenAndHandleChan()

	go s.broadcastPingPacket()

	time.Sleep(time.Millisecond) // HACK: Give some time for listeners to start.
	fmt.Println("Started server.")

	return s
}

func (s *server) listenAndHandleTCP() {
	ln, err := net.Listen("tcp", ":25045")
	if err != nil {
		panic(err)
	}

	for {
		tcp, err := ln.Accept()
		if err != nil {
			panic(err)
		}

		client := newConnection()
		client.tcp = tcp
		client.JoinStatus = TCP_CONNECTED
		client.dialedClient()
		s.connectionsMu.Lock()
		s.connections = append(s.connections, client)
		s.connectionsMu.Unlock()

		if shouldHandleUDPDirectly {
			go s.handleUDP(client)
		}
		go s.handleTCPConnection(client)
	}
}
func (s *server) listenAndHandleUDP() {
	udpAddr, err := net.ResolveUDPAddr("udp", ":25045")
	if err != nil {
		panic(err)
	}
	ln, err := net.ListenUDP("udp", udpAddr)
	if err != nil {
		panic(err)
	}
	mux := &Connection{udp: ln}

	s.handleUDP(mux)
}

func (s *server) listenAndHandleWebSocket() {
	h := websocket.Handler(func(conn *websocket.Conn) {
		// Why is this exported field undocumented?
		//
		// It needs to be set to websocket.BinaryFrame so that
		// the Write method sends bytes as binary rather than text frames.
		conn.PayloadType = websocket.BinaryFrame

		client := newConnection()
		client.tcp = conn
		client.JoinStatus = TCP_CONNECTED
		client.dialedClient()
		s.connectionsMu.Lock()
		s.connections = append(s.connections, client)
		s.connectionsMu.Unlock()

		if shouldHandleUDPDirectly {
			go s.handleUDP(client)
		}
		s.handleTCPConnection(client)
		// Do not return until handleTCPConnection does, else WebSocket gets closed.
	})
	err := http.ListenAndServe(":25046", h)
	if err != nil {
		panic(err)
	}
}

func (s *server) listenAndHandleChan() {
	for clientToServerConn := range s.chanListener {

		serverToClientConn := newConnection()
		// Join server <-> client channel ends together.
		serverToClientConn.sendTCP = clientToServerConn.recvTCP
		serverToClientConn.recvTCP = clientToServerConn.sendTCP
		serverToClientConn.sendUDP = clientToServerConn.recvUDP
		serverToClientConn.recvUDP = clientToServerConn.sendUDP
		serverToClientConn.JoinStatus = TCP_CONNECTED

		s.connectionsMu.Lock()
		s.connections = append(s.connections, serverToClientConn)
		s.connectionsMu.Unlock()

		if shouldHandleUDPDirectly {
			go s.handleUDP(serverToClientConn)
		}
		go s.handleTCPConnection(serverToClientConn)

		s.chanListenerReply <- struct{}{}
	}
}

func (s *server) handleUDP(mux *Connection) {
	for {
		b, c, udpAddr, err := receiveUDPPacketFrom(s, mux)
		if err != nil {
			if shouldHandleUDPDirectly { // HACK: This isn't a real mux but rather the client directly, so return.
				fmt.Println("udp conn ended with:", err)
				return
			}
			panic(err)
		}

		err = s.processUDPPacket(b, c, udpAddr, mux)
		if err != nil {
			fmt.Println("handleUDPPacket:", err)
			if c != nil {
				c.tcp.Close()
			}
		}
	}
}

func (s *server) processUDPPacket(b []byte, c *Connection, udpAddr *net.UDPAddr, mux *Connection) error {
	buf := bytes.NewReader(b)

	var udpHeader packet.UDPHeader
	err := binary.Read(buf, binary.BigEndian, &udpHeader)
	if err != nil {
		return err
	}

	if c == nil && udpHeader.Type != packet.HandshakeType {
		return fmt.Errorf("nil c, unexpected udpHeader.Type: %v", udpHeader.Type)
	}

	switch udpHeader.Type {
	case packet.HandshakeType:
		var r packet.Handshake
		err = binary.Read(buf, binary.BigEndian, &r.Signature)
		if err != nil {
			return err
		}
		{
			r2 := r
			r2.Signature = 123
			goon.Dump(r2)
		}

		s.connectionsMu.Lock()
		for _, connection := range s.connections {
			if connection.Signature == r.Signature {
				connection.JoinStatus = UDP_CONNECTED
				connection.udp = mux.udp
				connection.UDPAddr = udpAddr

				c = connection
				break
			}
		}
		s.connectionsMu.Unlock()

		if c != nil {
			var p packet.UDPConnectionEstablished
			p.Type = packet.UDPConnectionEstablishedType

			p.Length = 0

			var buf bytes.Buffer
			err := binary.Write(&buf, binary.BigEndian, &p)
			if err != nil {
				return err
			}
			err = sendTCPPacket(c, buf.Bytes())
			if err != nil {
				return err
			}
		}
	case packet.TimeRequestType:
		var r packet.TimeRequest
		err = binary.Read(buf, binary.BigEndian, &r.SequenceNumber)
		if err != nil {
			return err
		}

		{
			var p packet.TimeResponse
			p.Type = packet.TimeResponseType
			p.SequenceNumber = r.SequenceNumber
			state.Lock()
			p.Time = time.Since(s.logic.started).Seconds()
			state.Unlock()

			var buf bytes.Buffer
			err := binary.Write(&buf, binary.BigEndian, &p)
			if err != nil {
				return err
			}
			err = sendUDPPacket(c, buf.Bytes())
			if err != nil {
				return err
			}
		}
	case packet.PongType:
		localTimeAtPongReceive := time.Now()

		var r packet.Pong
		err = binary.Read(buf, binary.BigEndian, &r.PingData)
		if err != nil {
			return err
		}

		s.pingSentTimesMu.Lock()
		// Get the time sent of the matching Pong packet.
		if localTimeAtPingSend, ok := s.pingSentTimes[c.PlayerID][r.PingData]; ok {
			delete(s.pingSentTimes[c.PlayerID], r.PingData)

			latency := uint16(localTimeAtPongReceive.Sub(localTimeAtPingSend).Seconds() * 10000) // Units are 0.1 ms.
			s.lastLatenciesMu.Lock()
			s.lastLatencies[c.PlayerID] = latency
			s.lastLatenciesMu.Unlock()
		}
		s.pingSentTimesMu.Unlock()

		{
			var p packet.Pung
			p.Type = packet.PungType
			p.PingData = r.PingData
			p.Time = time.Since(s.logic.started).Seconds()

			var buf bytes.Buffer
			err := binary.Write(&buf, binary.BigEndian, &p)
			if err != nil {
				return err
			}
			err = sendUDPPacket(c, buf.Bytes())
			if err != nil {
				return err
			}
		}
	case packet.ClientCommandType:
		var r packet.ClientCommand
		err = binary.Read(buf, binary.BigEndian, &r.CommandSequenceNumber)
		if err != nil {
			return err
		}
		err = binary.Read(buf, binary.BigEndian, &r.CommandSeriesNumber)
		if err != nil {
			return err
		}
		err = binary.Read(buf, binary.BigEndian, &r.MovesCount)
		if err != nil {
			return err
		}
		movesCount := uint16(r.MovesCount) + 1 // De-normalize back to 1 (min value), prevent overflow to 0.
		r.Moves = make([]packet.Move, movesCount)
		err = binary.Read(buf, binary.BigEndian, &r.Moves)
		if err != nil {
			return err
		}
		//goon.Dump(r)

		// TODO: Properly process and authenticate new result states.
		{
			s.logic.playersStateMu.Lock()
			lastState := s.logic.playersState[c.PlayerID].LatestAuthed()
			s.logic.playersStateMu.Unlock()

			lastMove := r.Moves[len(r.Moves)-1] // There's always at least one move in a ClientCommand packet.

			// TODO: Check that state sn == command sn, etc.
			lastState.SequenceNumber = r.CommandSequenceNumber // HACK.
			newState := s.logic.nextState(lastState, lastMove)

			s.logic.playersStateMu.Lock()
			ps := s.logic.playersState[c.PlayerID]
			ps.PushAuthed(s.logic, newState)
			s.logic.playersState[c.PlayerID] = ps
			s.logic.playersStateMu.Unlock()
		}
	}
	return nil
}

func (s *server) sendServerUpdates(c *Connection) {
	for ; true; time.Sleep(time.Second / 20) {
		s.logic.playersStateMu.Lock()
		if _, ok := s.logic.playersState[c.PlayerID]; !ok { // HACK: Who should be responsible for stopping these? Currently having them quit on own in a hacky way, see what's the best way.
			s.logic.playersStateMu.Unlock()
			return
		}
		ps := s.logic.playersState[c.PlayerID]
		ps.lastServerUpdateSequenceNumber++ // First update sent should have sequence number 1.
		s.logic.playersState[c.PlayerID] = ps
		s.logic.playersStateMu.Unlock()
		lastServerUpdateSequenceNumber := ps.lastServerUpdateSequenceNumber

		// Prepare a ServerUpdate packet.
		var p packet.ServerUpdate
		p.Type = packet.ServerUpdateType
		p.CurrentUpdateSequenceNumber = lastServerUpdateSequenceNumber
		state.Lock()
		p.PlayerUpdates = make([]packet.PlayerUpdate, s.logic.TotalPlayerCount)
		state.Unlock()
		s.logic.playersStateMu.Lock()
		for id, ps := range s.logic.playersState {
			if ps.conn == nil || ps.conn.JoinStatus < IN_GAME || ps.Team == packet.Spectator {
				continue
			}
			p.PlayerUpdates[id] = packet.PlayerUpdate{
				ActivePlayer: 1,
				State: &packet.State{
					CommandSequenceNumber: ps.LatestAuthed().SequenceNumber,
					X: ps.LatestAuthed().X,
					Y: ps.LatestAuthed().Y,
					Z: ps.LatestAuthed().Z,
				},
			}
		}
		s.logic.playersStateMu.Unlock()

		var buf bytes.Buffer
		err := binary.Write(&buf, binary.BigEndian, &p.UDPHeader)
		if err != nil {
			panic(err)
		}
		err = binary.Write(&buf, binary.BigEndian, &p.CurrentUpdateSequenceNumber)
		if err != nil {
			panic(err)
		}
		for _, playerUpdate := range p.PlayerUpdates {
			err = binary.Write(&buf, binary.BigEndian, &playerUpdate.ActivePlayer)
			if err != nil {
				panic(err)
			}

			if playerUpdate.ActivePlayer != 0 {
				err = binary.Write(&buf, binary.BigEndian, playerUpdate.State)
				if err != nil {
					panic(err)
				}
			}
		}

		// Send the packet to this client.
		err = sendUDPPacket(c, buf.Bytes())
		if err != nil {
			panic(err)
		}
	}
}

func (s *server) broadcastPingPacket() {
	const BROADCAST_PING_PERIOD = 2500 * time.Millisecond // How often to broadcast the Ping packet on the server.

	for ; true; time.Sleep(BROADCAST_PING_PERIOD) {
		// Prepare a Ping packet.
		var p packet.Ping
		p.Type = packet.PingType
		p.PingData = s.lastPingData
		p.LastLatencies = make([]uint16, s.logic.TotalPlayerCount)
		copy(p.LastLatencies, s.lastLatencies)

		var buf bytes.Buffer
		err := binary.Write(&buf, binary.BigEndian, &p.UDPHeader)
		if err != nil {
			panic(err)
		}
		err = binary.Write(&buf, binary.BigEndian, &p.PingData)
		if err != nil {
			panic(err)
		}
		err = binary.Write(&buf, binary.BigEndian, &p.LastLatencies)
		if err != nil {
			panic(err)
		}

		// Broadcast the packet to all connections with at least IN_GAME.
		var cs []*Connection
		s.connectionsMu.Lock()
		for _, c := range s.connections {
			if c.JoinStatus < IN_GAME {
				continue
			}
			cs = append(cs, c)
		}
		s.connectionsMu.Unlock()
		for _, c := range cs {
			s.pingSentTimesMu.Lock()
			s.pingSentTimes[c.PlayerID][p.PingData] = time.Now()
			s.pingSentTimesMu.Unlock()

			err = sendUDPPacket(c, buf.Bytes())
			if err != nil {
				panic(err)
			}
		}

		s.lastPingData++
	}
}

func (s *server) handleTCPConnection(client *Connection) {
	err := s.handleTCPConnection2(client)
	fmt.Println("tcp conn ended with:", err)

	if client.JoinStatus >= PUBLIC_CLIENT {
		// Broadcast a PlayerLeftServer packet.
		err := func() error {
			var p packet.PlayerLeftServer
			p.Type = packet.PlayerLeftServerType

			p.Length = 1

			p.PlayerID = client.PlayerID

			var buf bytes.Buffer
			err := binary.Write(&buf, binary.BigEndian, &p.TCPHeader)
			if err != nil {
				return err
			}
			err = binary.Write(&buf, binary.BigEndian, &p.PlayerID)
			if err != nil {
				return err
			}

			// Broadcast the packet to all other connections with at least PUBLIC_CLIENT.
			var cs []*Connection
			s.connectionsMu.Lock()
			for _, c := range s.connections {
				if c.JoinStatus < PUBLIC_CLIENT || c == client {
					continue
				}
				cs = append(cs, c)
			}
			s.connectionsMu.Unlock()
			for _, c := range cs {
				err = sendTCPPacket(c, buf.Bytes())
				if err != nil {
					// TODO: This error handling is wrong. If fail to send to one client, should still send to others, etc.
					return err
				}
			}
			return nil
		}()
		if err != nil {
			fmt.Println("error while broadcasting PlayerLeftServer packet:", err)
		}
	}

	if client.JoinStatus >= ACCEPTED {
		s.pingSentTimesMu.Lock()
		// Clear the map.
		for k := range s.pingSentTimes[client.PlayerID] {
			delete(s.pingSentTimes[client.PlayerID], k)
		}
		s.pingSentTimesMu.Unlock()

		s.lastLatenciesMu.Lock()
		s.lastLatencies[client.PlayerID] = 0
		s.lastLatenciesMu.Unlock()
	}

	s.connectionsMu.Lock()
	for i, connection := range s.connections {
		if connection == client {
			// Delete without preserving order.
			s.connections[i], s.connections[len(s.connections)-1], s.connections = s.connections[len(s.connections)-1], nil, s.connections[:len(s.connections)-1]
			break
		}
	}
	s.connectionsMu.Unlock()

	s.logic.playersStateMu.Lock()
	for id, ps := range s.logic.playersState {
		if ps.conn == client {
			delete(s.logic.playersState, id)
			break
		}
	}
	s.logic.playersStateMu.Unlock()

	client.tcp.Close()
}

func (s *server) handleTCPConnection2(client *Connection) error {
	var playerID uint8

	{
		b, _, err := receiveTCPPacket(client)
		if err != nil {
			return err
		}
		buf := bytes.NewReader(b)
		var r packet.JoinServerRequest
		err = binary.Read(buf, binary.BigEndian, &r)
		if err != nil {
			return err
		}
		{
			r2 := r
			r2.Signature = 123
			goon.Dump(r2)
		}

		if r.Version != 1 ||
			r.Passphrase != [16]byte{'s', 'o', 'm', 'e', 'r', 'a', 'n', 'd', 'o', 'm', 'p', 'a', 's', 's', '0', '1'} {

			{
				var p packet.JoinServerRefuse
				p.Type = packet.JoinServerRefuseType
				p.RefuseReason = 123 // TODO.

				p.Length = 1

				var buf bytes.Buffer
				err := binary.Write(&buf, binary.BigEndian, &p)
				if err != nil {
					return err
				}
				err = sendTCPPacket(client, buf.Bytes())
				if err != nil {
					return err
				}

				return nil
			}
		}

		s.connectionsMu.Lock()
		s.logic.playersStateMu.Lock()
		playerID = func /* allocatePlayerID */ () uint8 {
			for id := uint8(0); ; id++ {
				if _, taken := s.logic.playersState[id]; !taken {
					return id
				}
			}
		}()
		serverFull := playerID >= s.logic.TotalPlayerCount
		if !serverFull {
			s.logic.playersState[playerID] = playerState{conn: client}
			client.Signature = r.Signature
			client.PlayerID = playerID
			client.JoinStatus = ACCEPTED

			s.pingSentTimesMu.Lock()
			s.pingSentTimes[client.PlayerID] = make(map[uint32]time.Time)
			s.pingSentTimesMu.Unlock()
		}
		s.logic.playersStateMu.Unlock()
		s.connectionsMu.Unlock()

		if serverFull {
			{
				var p packet.JoinServerRefuse
				p.Type = packet.JoinServerRefuseType
				p.RefuseReason = 123 // TODO.

				p.Length = 1

				var buf bytes.Buffer
				err := binary.Write(&buf, binary.BigEndian, &p)
				if err != nil {
					return err
				}
				err = sendTCPPacket(client, buf.Bytes())
				if err != nil {
					return err
				}

				return nil
			}
		}
	}

	{
		var p packet.JoinServerAccept
		p.Type = packet.JoinServerAcceptType
		p.YourPlayerID = playerID
		state.Lock()
		p.TotalPlayerCount = s.logic.TotalPlayerCount - 1
		state.Unlock()

		p.Length = 2

		var buf bytes.Buffer
		err := binary.Write(&buf, binary.BigEndian, &p)
		if err != nil {
			return err
		}
		err = sendTCPPacket(client, buf.Bytes())
		if err != nil {
			return err
		}
	}

	{
		b, _, err := receiveTCPPacket(client)
		if err != nil {
			return err
		}
		buf := bytes.NewReader(b)
		var r packet.LocalPlayerInfo
		err = binary.Read(buf, binary.BigEndian, &r.TCPHeader)
		if err != nil {
			return err
		}
		err = binary.Read(buf, binary.BigEndian, &r.NameLength)
		if err != nil {
			return err
		}
		r.Name = make([]byte, r.NameLength)
		err = binary.Read(buf, binary.BigEndian, &r.Name)
		if err != nil {
			return err
		}
		err = binary.Read(buf, binary.BigEndian, &r.CommandRate)
		if err != nil {
			return err
		}
		err = binary.Read(buf, binary.BigEndian, &r.UpdateRate)
		if err != nil {
			return err
		}
		goon.Dump(r)

		// TODO: Ensure name is at least 1 character, because Current Players Info users empty name to indicate non-players...
		if len(r.Name) == 0 {
			r.Name = []byte("Unnamed Player")
		}

		s.connectionsMu.Lock()
		s.logic.playersStateMu.Lock()
		{
			ps := s.logic.playersState[playerID]
			ps.Name = string(r.Name)
			ps.Team = packet.Spectator
			s.logic.playersState[playerID] = ps
		}
		client.JoinStatus = PUBLIC_CLIENT
		s.logic.playersStateMu.Unlock()
		s.connectionsMu.Unlock()
	}

	{
		var p packet.LoadLevel
		p.Type = packet.LoadLevelType
		p.LevelFilename = []byte(serverLevelFilename)

		p.Length = uint16(len(p.LevelFilename))

		var buf bytes.Buffer
		err := binary.Write(&buf, binary.BigEndian, &p.TCPHeader)
		if err != nil {
			return err
		}
		err = binary.Write(&buf, binary.BigEndian, &p.LevelFilename)
		if err != nil {
			return err
		}
		err = sendTCPPacket(client, buf.Bytes())
		if err != nil {
			return err
		}
	}

	// Include the client who's connecting and all clients with at least Public status.
	{
		var p packet.CurrentPlayersInfo
		p.Type = packet.CurrentPlayersInfoType
		p.Players = make([]packet.PlayerInfo, s.logic.TotalPlayerCount)
		state.Lock()
		s.logic.playersStateMu.Lock()
		p.Length += uint16(s.logic.TotalPlayerCount)
		for _, c := range s.connections {
			if c.JoinStatus < PUBLIC_CLIENT {
				continue
			}
			ps := s.logic.playersState[c.PlayerID]
			var playerInfo packet.PlayerInfo
			playerInfo.NameLength = uint8(len(ps.Name))
			if playerInfo.NameLength > 0 {
				playerInfo.Name = []byte(ps.Name)
				p.Length += uint16(playerInfo.NameLength)
				playerInfo.Team = ps.Team
				p.Length += 1
				if playerInfo.Team != packet.Spectator {
					playerInfo.State = &packet.State{
						CommandSequenceNumber: ps.LatestAuthed().SequenceNumber,
						X: ps.LatestAuthed().X,
						Y: ps.LatestAuthed().Y,
						Z: ps.LatestAuthed().Z,
					}
					p.Length += 1 + 4 + 4 + 4
				}
			}
			p.Players[c.PlayerID] = playerInfo
		}
		s.logic.playersStateMu.Unlock()
		state.Unlock()

		var buf bytes.Buffer
		err := binary.Write(&buf, binary.BigEndian, &p.TCPHeader)
		if err != nil {
			return err
		}
		for _, playerInfo := range p.Players {
			err = binary.Write(&buf, binary.BigEndian, &playerInfo.NameLength)
			if err != nil {
				return err
			}

			if playerInfo.NameLength != 0 {
				err = binary.Write(&buf, binary.BigEndian, &playerInfo.Name)
				if err != nil {
					return err
				}
				err = binary.Write(&buf, binary.BigEndian, &playerInfo.Team)
				if err != nil {
					return err
				}

				if playerInfo.Team != packet.Spectator {
					err = binary.Write(&buf, binary.BigEndian, playerInfo.State)
					if err != nil {
						return err
					}
				}
			}
		}
		err = sendTCPPacket(client, buf.Bytes())
		if err != nil {
			return err
		}
	}

	{
		var p packet.PlayerJoinedServer
		p.Type = packet.PlayerJoinedServerType

		s.logic.playersStateMu.Lock()
		ps := s.logic.playersState[playerID]
		s.logic.playersStateMu.Unlock()

		p.PlayerID = playerID
		p.NameLength = uint8(len(ps.Name))
		p.Name = []byte(ps.Name)

		p.Length = 2 + uint16(len(p.Name))

		var buf bytes.Buffer
		err := binary.Write(&buf, binary.BigEndian, &p.TCPHeader)
		if err != nil {
			return err
		}
		err = binary.Write(&buf, binary.BigEndian, &p.PlayerID)
		if err != nil {
			return err
		}
		err = binary.Write(&buf, binary.BigEndian, &p.NameLength)
		if err != nil {
			return err
		}
		err = binary.Write(&buf, binary.BigEndian, &p.Name)
		if err != nil {
			return err
		}

		// Broadcast the packet to all other connections with at least PUBLIC_CLIENT.
		var cs []*Connection
		s.connectionsMu.Lock()
		for _, c := range s.connections {
			if c.JoinStatus < PUBLIC_CLIENT || c == client {
				continue
			}
			cs = append(cs, c)
		}
		s.connectionsMu.Unlock()
		for _, c := range cs {
			err = sendTCPPacket(c, buf.Bytes())
			if err != nil {
				return err
			}
		}
	}

	{
		var p packet.EnterGamePermission
		p.Type = packet.EnterGamePermissionType

		p.Length = 0

		var buf bytes.Buffer
		err := binary.Write(&buf, binary.BigEndian, &p)
		if err != nil {
			return err
		}
		err = sendTCPPacket(client, buf.Bytes())
		if err != nil {
			return err
		}
	}

	{
		b, _, err := receiveTCPPacket(client)
		if err != nil {
			return err
		}
		buf := bytes.NewReader(b)
		var r packet.EnteredGameNotification
		err = binary.Read(buf, binary.BigEndian, &r)
		if err != nil {
			return err
		}
		goon.Dump(r)

		s.connectionsMu.Lock()
		client.JoinStatus = IN_GAME
		s.connectionsMu.Unlock()

		// TODO: Who should be responsible for stopping these? Currently having them quit on own in a hacky way, see what's the best way.
		go s.sendServerUpdates(client)
	}

	for {
		b, tcpHeader, err := receiveTCPPacket(client)
		if err != nil {
			return err
		}
		buf := bytes.NewReader(b)

		switch tcpHeader.Type {
		case packet.JoinTeamRequestType:
			var team packet.Team

			{
				var r = packet.JoinTeamRequest{TCPHeader: tcpHeader}
				_, err = io.CopyN(ioutil.Discard, buf, packet.TCPHeaderSize)
				if err != nil {
					return err
				}
				// TODO: Handle potential PlayerNumber.
				err = binary.Read(buf, binary.BigEndian, &r.Team)
				if err != nil {
					return err
				}
				goon.Dump(r)

				team = r.Team
			}

			s.logic.playersStateMu.Lock()
			{
				ps := s.logic.playersState[playerID]
				ps.Team = team
				s.logic.playersState[playerID] = ps
			}
			s.logic.playersStateMu.Unlock()

			state.Lock()
			s.logic.playersStateMu.Lock()
			logicTime := float64(s.logic.GlobalStateSequenceNumber) + (time.Since(s.logic.started).Seconds()-s.logic.NextTickTime)*commandRate
			fmt.Fprintf(os.Stderr, "%.3f: Pl#%v (%q) joined team %v at logic time %.2f/%v [server].\n", time.Since(s.logic.started).Seconds(), playerID, s.logic.playersState[playerID].Name, team, logicTime, s.logic.GlobalStateSequenceNumber)
			s.logic.playersStateMu.Unlock()
			state.Unlock()

			{
				var p packet.PlayerJoinedTeam
				p.Type = packet.PlayerJoinedTeamType
				p.PlayerID = playerID
				p.Team = team

				state.Lock()
				s.logic.playersStateMu.Lock()
				ps := s.logic.playersState[playerID]
				{
					// TODO: Proper spawn location calculation.
					playerSpawn := playerPosVel{
						X: -25,
						Y: -160,
						Z: 6.0,
					}
					playerSpawn.X += float32(playerID) * 20

					ps.NewSeries()
					ps.PushAuthed(s.logic, sequencedPlayerPosVel{
						playerPosVel:   playerSpawn,
						SequenceNumber: s.logic.GlobalStateSequenceNumber - 1,
					})
				}
				s.logic.playersState[playerID] = ps
				s.logic.playersStateMu.Unlock()
				state.Unlock()

				if p.Team != packet.Spectator {
					p.State = &packet.State{
						CommandSequenceNumber: ps.LatestAuthed().SequenceNumber,
						X: ps.LatestAuthed().X,
						Y: ps.LatestAuthed().Y,
						Z: ps.LatestAuthed().Z,
					}
				}

				p.Length = 2
				if p.State != nil {
					p.Length += 13
				}

				var buf bytes.Buffer
				err := binary.Write(&buf, binary.BigEndian, &p.TCPHeader)
				if err != nil {
					return err
				}
				err = binary.Write(&buf, binary.BigEndian, &p.PlayerID)
				if err != nil {
					return err
				}
				err = binary.Write(&buf, binary.BigEndian, &p.Team)
				if err != nil {
					return err
				}
				if p.State != nil {
					err = binary.Write(&buf, binary.BigEndian, p.State)
					if err != nil {
						return err
					}
				}

				// Broadcast the packet to all connections with at least PUBLIC_CLIENT.
				var cs []*Connection
				s.connectionsMu.Lock()
				for _, c := range s.connections {
					if c.JoinStatus < PUBLIC_CLIENT {
						continue
					}
					cs = append(cs, c)
				}
				s.connectionsMu.Unlock()
				for _, c := range cs {
					err = sendTCPPacket(c, buf.Bytes())
					if err != nil {
						return err
					}
				}
			}
		default:
			fmt.Println("[server] got unsupported tcp packet type:", tcpHeader.Type)
		}
	}
}
