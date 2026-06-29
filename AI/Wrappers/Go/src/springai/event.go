/* This file is part of the Spring engine (GPL v2 or later), see LICENSE.html */

package springai

/*
#cgo CFLAGS: -I${SRCDIR}/../../../../../rts -I${SRCDIR}/../../../../../rts/ExternalAI/Interface

#include <stdbool.h>
#include <stddef.h>

#include "AISEvents.h"
*/
import "C"

import "unsafe"

// EventTopic identifies an event sent from the engine to the AI.
// The values mirror enum EventTopic in
// rts/ExternalAI/Interface/AISEvents.h and MUST NOT be renumbered.
type EventTopic int

const (
	EventNull            EventTopic = C.EVENT_NULL
	EventInit            EventTopic = C.EVENT_INIT
	EventRelease         EventTopic = C.EVENT_RELEASE
	EventUpdate          EventTopic = C.EVENT_UPDATE
	EventMessage         EventTopic = C.EVENT_MESSAGE
	EventUnitCreated     EventTopic = C.EVENT_UNIT_CREATED
	EventUnitFinished    EventTopic = C.EVENT_UNIT_FINISHED
	EventUnitIdle        EventTopic = C.EVENT_UNIT_IDLE
	EventUnitMoveFailed  EventTopic = C.EVENT_UNIT_MOVE_FAILED
	EventUnitDamaged     EventTopic = C.EVENT_UNIT_DAMAGED
	EventUnitDestroyed   EventTopic = C.EVENT_UNIT_DESTROYED
	EventUnitGiven       EventTopic = C.EVENT_UNIT_GIVEN
	EventUnitCaptured    EventTopic = C.EVENT_UNIT_CAPTURED
	EventEnemyEnterLOS   EventTopic = C.EVENT_ENEMY_ENTER_LOS
	EventEnemyLeaveLOS   EventTopic = C.EVENT_ENEMY_LEAVE_LOS
	EventEnemyEnterRadar EventTopic = C.EVENT_ENEMY_ENTER_RADAR
	EventEnemyLeaveRadar EventTopic = C.EVENT_ENEMY_LEAVE_RADAR
	EventEnemyDamaged    EventTopic = C.EVENT_ENEMY_DAMAGED
	EventEnemyDestroyed  EventTopic = C.EVENT_ENEMY_DESTROYED
	EventWeaponFired     EventTopic = C.EVENT_WEAPON_FIRED
	EventPlayerCommand   EventTopic = C.EVENT_PLAYER_COMMAND
	EventSeismicPing     EventTopic = C.EVENT_SEISMIC_PING
	EventCommandFinished EventTopic = C.EVENT_COMMAND_FINISHED
	EventLoad            EventTopic = C.EVENT_LOAD
	EventSave            EventTopic = C.EVENT_SAVE
	EventEnemyCreated    EventTopic = C.EVENT_ENEMY_CREATED
	EventEnemyFinished   EventTopic = C.EVENT_ENEMY_FINISHED
	EventLuaMessage      EventTopic = C.EVENT_LUA_MESSAGE
)

// Event is implemented by every decoded engine event. It is the Go analogue of
// the springai::AIEvent marker class (AI/Wrappers/Cpp/src/AIEvent.h), with a
// Topic accessor so a type switch is not strictly required.
type Event interface {
	Topic() EventTopic
}

// --- One Go struct per C event struct in AISEvents.h ---

// InitEvent corresponds to struct SInitEvent. The Callback pointer is the raw
// SSkirmishAICallback*; wrap it with NewCallback to use it.
type InitEvent struct {
	SkirmishAIID int
	Callback     unsafe.Pointer
	SavedGame    bool
}

// ReleaseEvent corresponds to struct SReleaseEvent.
type ReleaseEvent struct{ Reason int }

// UpdateEvent corresponds to struct SUpdateEvent.
type UpdateEvent struct{ Frame int }

// MessageEvent corresponds to struct SMessageEvent.
type MessageEvent struct {
	Player  int
	Message string
}

// LuaMessageEvent corresponds to struct SLuaMessageEvent.
type LuaMessageEvent struct{ InData string }

// UnitCreatedEvent corresponds to struct SUnitCreatedEvent.
type UnitCreatedEvent struct {
	Unit    int
	Builder int
}

// UnitFinishedEvent corresponds to struct SUnitFinishedEvent.
type UnitFinishedEvent struct{ Unit int }

// UnitIdleEvent corresponds to struct SUnitIdleEvent.
type UnitIdleEvent struct{ Unit int }

// UnitMoveFailedEvent corresponds to struct SUnitMoveFailedEvent.
type UnitMoveFailedEvent struct{ Unit int }

// UnitDamagedEvent corresponds to struct SUnitDamagedEvent.
type UnitDamagedEvent struct {
	Unit        int
	Attacker    int // may be -1
	Damage      float32
	Dir         AIFloat3
	WeaponDefID int
	Paralyzer   bool
}

// UnitDestroyedEvent corresponds to struct SUnitDestroyedEvent.
type UnitDestroyedEvent struct {
	Unit        int
	Attacker    int // may be -1
	WeaponDefID int
}

// UnitGivenEvent corresponds to struct SUnitGivenEvent.
type UnitGivenEvent struct {
	UnitID    int
	OldTeamID int
	NewTeamID int
}

// UnitCapturedEvent corresponds to struct SUnitCapturedEvent.
type UnitCapturedEvent struct {
	UnitID    int
	OldTeamID int
	NewTeamID int
}

// EnemyEnterLOSEvent corresponds to struct SEnemyEnterLOSEvent.
type EnemyEnterLOSEvent struct{ Enemy int }

// EnemyLeaveLOSEvent corresponds to struct SEnemyLeaveLOSEvent.
type EnemyLeaveLOSEvent struct{ Enemy int }

// EnemyEnterRadarEvent corresponds to struct SEnemyEnterRadarEvent.
type EnemyEnterRadarEvent struct{ Enemy int }

// EnemyLeaveRadarEvent corresponds to struct SEnemyLeaveRadarEvent.
type EnemyLeaveRadarEvent struct{ Enemy int }

// EnemyDamagedEvent corresponds to struct SEnemyDamagedEvent.
type EnemyDamagedEvent struct {
	Enemy       int
	Attacker    int // may be -1
	Damage      float32
	Dir         AIFloat3
	WeaponDefID int
	Paralyzer   bool
}

// EnemyDestroyedEvent corresponds to struct SEnemyDestroyedEvent.
type EnemyDestroyedEvent struct {
	Enemy    int
	Attacker int // may be -1
}

// WeaponFiredEvent corresponds to struct SWeaponFiredEvent.
type WeaponFiredEvent struct {
	UnitID      int
	WeaponDefID int
}

// PlayerCommandEvent corresponds to struct SPlayerCommandEvent.
type PlayerCommandEvent struct {
	UnitIDs        []int
	CommandTopicID int
	PlayerID       int
}

// SeismicPingEvent corresponds to struct SSeismicPingEvent.
type SeismicPingEvent struct {
	Pos      AIFloat3
	Strength float32
}

// CommandFinishedEvent corresponds to struct SCommandFinishedEvent.
type CommandFinishedEvent struct {
	UnitID         int
	CommandID      int
	CommandTopicID int
}

// LoadEvent corresponds to struct SLoadEvent.
type LoadEvent struct{ File string }

// SaveEvent corresponds to struct SSaveEvent.
type SaveEvent struct{ File string }

// EnemyCreatedEvent corresponds to struct SEnemyCreatedEvent.
type EnemyCreatedEvent struct{ Enemy int }

// EnemyFinishedEvent corresponds to struct SEnemyFinishedEvent.
type EnemyFinishedEvent struct{ Enemy int }

// Topic accessors.
func (InitEvent) Topic() EventTopic            { return EventInit }
func (ReleaseEvent) Topic() EventTopic         { return EventRelease }
func (UpdateEvent) Topic() EventTopic          { return EventUpdate }
func (MessageEvent) Topic() EventTopic         { return EventMessage }
func (LuaMessageEvent) Topic() EventTopic      { return EventLuaMessage }
func (UnitCreatedEvent) Topic() EventTopic     { return EventUnitCreated }
func (UnitFinishedEvent) Topic() EventTopic    { return EventUnitFinished }
func (UnitIdleEvent) Topic() EventTopic        { return EventUnitIdle }
func (UnitMoveFailedEvent) Topic() EventTopic  { return EventUnitMoveFailed }
func (UnitDamagedEvent) Topic() EventTopic     { return EventUnitDamaged }
func (UnitDestroyedEvent) Topic() EventTopic   { return EventUnitDestroyed }
func (UnitGivenEvent) Topic() EventTopic       { return EventUnitGiven }
func (UnitCapturedEvent) Topic() EventTopic    { return EventUnitCaptured }
func (EnemyEnterLOSEvent) Topic() EventTopic   { return EventEnemyEnterLOS }
func (EnemyLeaveLOSEvent) Topic() EventTopic   { return EventEnemyLeaveLOS }
func (EnemyEnterRadarEvent) Topic() EventTopic { return EventEnemyEnterRadar }
func (EnemyLeaveRadarEvent) Topic() EventTopic { return EventEnemyLeaveRadar }
func (EnemyDamagedEvent) Topic() EventTopic    { return EventEnemyDamaged }
func (EnemyDestroyedEvent) Topic() EventTopic  { return EventEnemyDestroyed }
func (WeaponFiredEvent) Topic() EventTopic     { return EventWeaponFired }
func (PlayerCommandEvent) Topic() EventTopic   { return EventPlayerCommand }
func (SeismicPingEvent) Topic() EventTopic     { return EventSeismicPing }
func (CommandFinishedEvent) Topic() EventTopic { return EventCommandFinished }
func (LoadEvent) Topic() EventTopic            { return EventLoad }
func (SaveEvent) Topic() EventTopic            { return EventSave }
func (EnemyCreatedEvent) Topic() EventTopic    { return EventEnemyCreated }
func (EnemyFinishedEvent) Topic() EventTopic   { return EventEnemyFinished }

// aiFloat3FromC reads three consecutive C floats pointed at by p.
// Lives here (a cgo file) because aifloat3.go is kept free of cgo.
func aiFloat3FromC(p *C.float) AIFloat3 {
	if p == nil {
		return NullAIFloat3
	}
	xyz := unsafe.Slice((*float32)(unsafe.Pointer(p)), 3)
	return AIFloat3{X: xyz[0], Y: xyz[1], Z: xyz[2]}
}

func intsFromC(p *C.int, n C.int) []int {
	if p == nil || n <= 0 {
		return nil
	}
	src := unsafe.Slice((*int32)(unsafe.Pointer(p)), int(n))
	out := make([]int, int(n))
	for i, v := range src {
		out[i] = int(v)
	}
	return out
}

// DecodeEvent converts the opaque `void* data` the engine passes to
// handleEvent into a typed Go Event. It returns (nil, false) for unknown or
// dataless topics. This is the Go analogue of the topic switch the C++ wrapper
// leaves to the AI in CCppTestAI::HandleEvent.
func DecodeEvent(topic EventTopic, data unsafe.Pointer) (Event, bool) {
	switch topic {
	case EventInit:
		e := (*C.struct_SInitEvent)(data)
		return InitEvent{
			SkirmishAIID: int(e.skirmishAIId),
			Callback:     unsafe.Pointer(e.callback),
			SavedGame:    bool(e.savedGame),
		}, true
	case EventRelease:
		e := (*C.struct_SReleaseEvent)(data)
		return ReleaseEvent{Reason: int(e.reason)}, true
	case EventUpdate:
		e := (*C.struct_SUpdateEvent)(data)
		return UpdateEvent{Frame: int(e.frame)}, true
	case EventMessage:
		e := (*C.struct_SMessageEvent)(data)
		return MessageEvent{Player: int(e.player), Message: C.GoString(e.message)}, true
	case EventLuaMessage:
		e := (*C.struct_SLuaMessageEvent)(data)
		return LuaMessageEvent{InData: C.GoString(e.inData)}, true
	case EventUnitCreated:
		e := (*C.struct_SUnitCreatedEvent)(data)
		return UnitCreatedEvent{Unit: int(e.unit), Builder: int(e.builder)}, true
	case EventUnitFinished:
		e := (*C.struct_SUnitFinishedEvent)(data)
		return UnitFinishedEvent{Unit: int(e.unit)}, true
	case EventUnitIdle:
		e := (*C.struct_SUnitIdleEvent)(data)
		return UnitIdleEvent{Unit: int(e.unit)}, true
	case EventUnitMoveFailed:
		e := (*C.struct_SUnitMoveFailedEvent)(data)
		return UnitMoveFailedEvent{Unit: int(e.unit)}, true
	case EventUnitDamaged:
		e := (*C.struct_SUnitDamagedEvent)(data)
		return UnitDamagedEvent{
			Unit:        int(e.unit),
			Attacker:    int(e.attacker),
			Damage:      float32(e.damage),
			Dir:         aiFloat3FromC(e.dir_posF3),
			WeaponDefID: int(e.weaponDefId),
			Paralyzer:   bool(e.paralyzer),
		}, true
	case EventUnitDestroyed:
		e := (*C.struct_SUnitDestroyedEvent)(data)
		return UnitDestroyedEvent{
			Unit:        int(e.unit),
			Attacker:    int(e.attacker),
			WeaponDefID: int(e.weaponDefID),
		}, true
	case EventUnitGiven:
		e := (*C.struct_SUnitGivenEvent)(data)
		return UnitGivenEvent{UnitID: int(e.unitId), OldTeamID: int(e.oldTeamId), NewTeamID: int(e.newTeamId)}, true
	case EventUnitCaptured:
		e := (*C.struct_SUnitCapturedEvent)(data)
		return UnitCapturedEvent{UnitID: int(e.unitId), OldTeamID: int(e.oldTeamId), NewTeamID: int(e.newTeamId)}, true
	case EventEnemyEnterLOS:
		e := (*C.struct_SEnemyEnterLOSEvent)(data)
		return EnemyEnterLOSEvent{Enemy: int(e.enemy)}, true
	case EventEnemyLeaveLOS:
		e := (*C.struct_SEnemyLeaveLOSEvent)(data)
		return EnemyLeaveLOSEvent{Enemy: int(e.enemy)}, true
	case EventEnemyEnterRadar:
		e := (*C.struct_SEnemyEnterRadarEvent)(data)
		return EnemyEnterRadarEvent{Enemy: int(e.enemy)}, true
	case EventEnemyLeaveRadar:
		e := (*C.struct_SEnemyLeaveRadarEvent)(data)
		return EnemyLeaveRadarEvent{Enemy: int(e.enemy)}, true
	case EventEnemyDamaged:
		e := (*C.struct_SEnemyDamagedEvent)(data)
		return EnemyDamagedEvent{
			Enemy:       int(e.enemy),
			Attacker:    int(e.attacker),
			Damage:      float32(e.damage),
			Dir:         aiFloat3FromC(e.dir_posF3),
			WeaponDefID: int(e.weaponDefId),
			Paralyzer:   bool(e.paralyzer),
		}, true
	case EventEnemyDestroyed:
		e := (*C.struct_SEnemyDestroyedEvent)(data)
		return EnemyDestroyedEvent{Enemy: int(e.enemy), Attacker: int(e.attacker)}, true
	case EventWeaponFired:
		e := (*C.struct_SWeaponFiredEvent)(data)
		return WeaponFiredEvent{UnitID: int(e.unitId), WeaponDefID: int(e.weaponDefId)}, true
	case EventPlayerCommand:
		e := (*C.struct_SPlayerCommandEvent)(data)
		return PlayerCommandEvent{
			UnitIDs:        intsFromC(e.unitIds, e.unitIds_size),
			CommandTopicID: int(e.commandTopicId),
			PlayerID:       int(e.playerId),
		}, true
	case EventSeismicPing:
		e := (*C.struct_SSeismicPingEvent)(data)
		return SeismicPingEvent{Pos: aiFloat3FromC(e.pos_posF3), Strength: float32(e.strength)}, true
	case EventCommandFinished:
		e := (*C.struct_SCommandFinishedEvent)(data)
		return CommandFinishedEvent{UnitID: int(e.unitId), CommandID: int(e.commandId), CommandTopicID: int(e.commandTopicId)}, true
	case EventLoad:
		e := (*C.struct_SLoadEvent)(data)
		return LoadEvent{File: C.GoString(e.file)}, true
	case EventSave:
		e := (*C.struct_SSaveEvent)(data)
		return SaveEvent{File: C.GoString(e.file)}, true
	case EventEnemyCreated:
		e := (*C.struct_SEnemyCreatedEvent)(data)
		return EnemyCreatedEvent{Enemy: int(e.enemy)}, true
	case EventEnemyFinished:
		e := (*C.struct_SEnemyFinishedEvent)(data)
		return EnemyFinishedEvent{Enemy: int(e.enemy)}, true
	default:
		return nil, false
	}
}
