package main

import (
	"fmt"
	"os"
	"sync"
	"time"

	"github.com/go-gl/mathgl/mgl32"
	"github.com/shurcooL/eX0/eX0-go/packet"
)

type server struct{}

var state = struct {
	sync.Mutex

	TotalPlayerCount uint8

	session struct {
		GlobalStateSequenceNumberTEST uint8
		NextTickTime                  float64
	}

	connections []*Connection
}{
	TotalPlayerCount: 16,
}

const commandRate = 20

// TODO: I think this should be moved into logic component (not server), yeah?
//       There's also some overlap with state.connections, shouldn't that be resolved?
var playersStateMu sync.Mutex
var playersState = map[uint8]playerState{} // Player Id -> Player State.

type playerPosVel struct {
	X, Y, Z    float32
	VelX, VelY float32
}

type sequencedPlayerPosVel struct {
	playerPosVel
	SequenceNumber uint8
}

type predictedMove struct {
	move      packet.Move
	predicted sequencedPlayerPosVel
}

// TODO: Split into positions (there will be many over time) and current name, team, connection, etc.
type playerState struct {
	authed      []sequencedPlayerPosVel
	unconfirmed []predictedMove

	Name string
	Team packet.Team

	// TODO: Move this to a better place.
	conn *Connection

	lastServerUpdateSequenceNumber uint8 // Sequence Number of last packet.ServerUpdate sent to this connection. // TODO: This should go into a serverToClient connection struct.
}

func (ps playerState) LatestAuthed() sequencedPlayerPosVel {
	return ps.authed[len(ps.authed)-1]
}
func (ps playerState) LatestPredicted() sequencedPlayerPosVel {
	if len(ps.unconfirmed) > 0 {
		return ps.unconfirmed[len(ps.unconfirmed)-1].predicted
	}
	return ps.authed[len(ps.authed)-1]
}

func (ps *playerState) PushAuthed(newState sequencedPlayerPosVel) {
	if len(ps.authed) > 0 && newState.SequenceNumber == ps.authed[len(ps.authed)-1].SequenceNumber {
		// Skip updates that are not newer.
		return
	}

	// Drop unconfirmed predicted moves once they've been authed.
	for len(ps.unconfirmed) > 0 && newState.SequenceNumber != ps.unconfirmed[0].predicted.SequenceNumber {
		//fmt.Fprintf(os.Stderr, "PushAuthed: dropping unmatched ps.unconfirmed\n")
		ps.unconfirmed = ps.unconfirmed[1:]
	}
	if len(ps.unconfirmed) > 0 && newState.SequenceNumber == ps.unconfirmed[0].predicted.SequenceNumber {
		same := mgl32.FloatEqualThreshold(newState.X, ps.unconfirmed[0].predicted.X, 0.001) &&
			mgl32.FloatEqualThreshold(newState.Y, ps.unconfirmed[0].predicted.Y, 0.001)
		if same {
			//fmt.Fprintf(os.Stderr, "PushAuthed: dropping matched ps.unconfirmed, same!\n")
		} else {
			fmt.Fprintf(os.Stderr, "PushAuthed: dropping matched ps.unconfirmed, diff by %v, %v\n", newState.X-ps.unconfirmed[0].predicted.X, newState.Y-ps.unconfirmed[0].predicted.Y)
		}

		// Keep the locally-predicted velocity.
		newState.VelX = ps.unconfirmed[0].predicted.VelX
		newState.VelY = ps.unconfirmed[0].predicted.VelY

		ps.unconfirmed = ps.unconfirmed[1:]
	}

	// TODO: GC.
	//fmt.Fprintln(os.Stderr, "PushAuthed:", newState.SequenceNumber)
	ps.authed = append(ps.authed, newState)

	// Replay remaining ones.
	prevState := newState
	for i := range ps.unconfirmed {
		ps.unconfirmed[i].predicted = nextState(prevState, ps.unconfirmed[i].move)
		prevState = ps.unconfirmed[i].predicted
	}
}

func (ps *playerState) NewSeries() {
	// TODO: Consider preserving.
	ps.authed = nil
	ps.unconfirmed = nil
}

func (ps playerState) Interpolated(playerId uint8) playerPosVel {
	desiredBStateSN := state.session.GlobalStateSequenceNumberTEST

	if components.client == nil || components_client_id != playerId {
		desiredBStateSN -= 2 // HACK: Assumes command rate of 20, this puts us 100 ms in the past (2 * 1s/20 = 100 ms).
	}

	states := append([]sequencedPlayerPosVel(nil), ps.authed...)
	for _, unconfirmed := range ps.unconfirmed {
		states = append(states, unconfirmed.predicted)
	}

	if false {
		for i, s := range ps.authed {
			fmt.Println("authed:", i, s.SequenceNumber)
		}
		for i, u := range ps.unconfirmed {
			fmt.Println("unconfirmed:", i, u.predicted.SequenceNumber)
		}
	}

	if len(states) == 1 {
		//fmt.Println("warning: using LatestAuthed because len(states) == 1")
		return states[0].playerPosVel
	}

	bi := len(states) - 1
	b := states[bi]

	// Check if we're looking for a sequence number newer than history contains.
	if int8(desiredBStateSN-b.SequenceNumber) > 0 {
		// TODO: Extrapolate from history?
		//fmt.Println("warning: using LatestAuthed because:", int8(state.session.GlobalStateSequenceNumberTEST-b.SequenceNumber))
		return states[len(states)-1].playerPosVel
	}

	for b.SequenceNumber != desiredBStateSN {
		bi--
		b = states[bi]
		if bi == 0 {
			break
		}
	}

	if bi == 0 {
		return states[0].playerPosVel
	}

	a := states[bi-1]

	interp := float32((time.Since(startedProcess).Seconds() - state.session.NextTickTime + 1.0/commandRate) * commandRate)

	return playerPosVel{
		X: (1-interp)*a.playerPosVel.X + interp*b.playerPosVel.X,
		Y: (1-interp)*a.playerPosVel.Y + interp*b.playerPosVel.Y,
		Z: (1-interp)*a.playerPosVel.Z + interp*b.playerPosVel.Z,
	}
}
