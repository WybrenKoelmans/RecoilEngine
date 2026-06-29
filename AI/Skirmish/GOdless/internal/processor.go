/* This file is part of the Spring engine (GPL v2 or later), see LICENSE.html */

package internal

import (
	"sync"

	springai "github.com/beyond-all-reason/RecoilEngine/AI/Wrappers/Go/src/springai"
)

type Command interface {
	Execute(cb *springai.Callback) []springai.Event
}

type Processor struct {
	events        chan springai.Event
	commands      chan Command
	world         *World
	decisionMaker *DecisionMaker
	AIID          int
	closeOnce     sync.Once
}

func NewProcessor() *Processor {
	return &Processor{
		events:        make(chan springai.Event, 1000),
		commands:      make(chan Command, 1000),
		world:         NewWorld(),
		decisionMaker: NewDecisionMaker(),
	}
}

func (p *Processor) Submit(ev springai.Event) bool {
	select {
	case p.events <- ev:
		return true
	default:
		return false
	}
}

// Commands is the channel the engine thread drains to execute produced
// commands. It is closed once Run has handled every queued event.
func (p *Processor) Commands() <-chan Command { return p.commands }

// Run is the processor goroutine loop: read the event bucket until it is closed,
// updating the world and emitting commands, then close the command bucket.
func (p *Processor) Run() {
	for ev := range p.events {
		p.handle(ev)
	}
	close(p.commands)
}

// Close signals the processor to stop. Run drains any remaining events first.
// It is safe to call more than once; only the first call closes the channel.
func (p *Processor) Close() { p.closeOnce.Do(func() { close(p.events) }) }

// emit queues a command for the engine thread. Never blocks: a full buffer
// means the engine is not draining fast enough, so the command is dropped.
func (p *Processor) emit(cmd Command) {
	select {
	case p.commands <- cmd:
	default:
	}
}

const decisionTickFrames = 30

func (p *Processor) handle(ev springai.Event) {
	switch e := ev.(type) {
	case springai.UpdateEvent:
		p.world.frame = e.Frame

		if p.world.frame%decisionTickFrames == 0 {
			for _, cmd := range p.decisionMaker.Tick(p.world) {
				p.emit(cmd)
			}
		}

		if p.world.frame%300 == 0 {
			p.debugWorld()
		}

		// Request the engine to send us the eco stats every 30 frames, so we can make decisions based on them.
		if p.world.frame%60 == 0 {
			p.emit(ecoStatsRequestCommand{
				teamID:           p.world.teamID,
				metalResourceID:  p.world.metalResourceID,
				energyResourceID: p.world.energyResourceID,
			})
		}
	case EconomyStats:
		p.world.metalIncome = e.MetalIncome
		p.world.metalUsage = e.MetalUsage
		p.world.metalAmount = e.MetalCurrent
		p.world.metalCapacity = e.MetalCapacity
		p.world.energyIncome = e.EnergyIncome
		p.world.energyUsage = e.EnergyUsage
		p.world.energyAmount = e.EnergyCurrent
		p.world.energyCapacity = e.EnergyCapacity
	case UnitCreated:
		p.world.addUnit(e.Unit, e.Position, e.UnitDefID, e.Builder)
		if e.Builder == invalidUnitID {
			p.assignSquad(e.Unit)
		} else {
			// It's possibly a builder that is finished and the squad needs to notified.
			p.world.onSquadBuildStarted(e.Builder, e.Unit, e.UnitDefID)
		}
	case springai.UnitFinishedEvent:
		p.world.finishConstruction(e.Unit)

		if !p.world.squads.UnitHasSquad(e.Unit) {
			p.assignSquad(e.Unit)
			return
		}
	case springai.UnitDestroyedEvent:
		p.world.removeUnit(e.Unit)
	case springai.EnemyEnterLOSEvent:
		p.world.addEnemy(e.Enemy)
	case springai.EnemyDestroyedEvent:
		p.world.removeEnemy(e.Enemy)
	case UnitIdle:
		unit := p.world.units[e.Unit]
		unit.position = e.Position
		unit.state = UnitStateIdle
		p.world.units[e.Unit] = unit
	case Init:
		p.AIID = e.SkirmishAIID
		p.world.metalSpots = e.MetalSpots
		p.world.unitDefs = e.UnitDefs
		p.world.avgWind = e.AvgWind
		p.world.mapWidth = e.MapWidth
		p.world.mapHeight = e.MapHeight
		p.world.teamID = e.TeamID
		p.world.metalResourceID = e.MetalResourceID
		p.world.energyResourceID = e.EnergyResourceID
	}
}

func (p *Processor) assignSquad(unitID int) {
	squadType := p.decisionMaker.SquadTypeFor(p.world, unitID)
	if squadType == SquadTypeNone {
		return
	}

	squad := p.world.squads.AssignUnit(unitID, squadType)

	// A unit joining a squad mid-game carries no orders of its own. Re-issue the
	// squad's last order to it so it matches the rest of the squad, and mark its
	// state to match so a goal does not immediately re-task it as idle.
	if cmd := squad.LastOrder.command([]int{unitID}); cmd != nil {
		p.world.applyOrderState(unitID, squad.LastOrder)
		p.emit(cmd)
	}
}

// func formatFloat(f float32) string {
// 	return strconv.FormatFloat(float64(f), 'f', 8, 32)
// }

func (p *Processor) debugWorld() {

	// We send a lua message that the game can use to visualize the world model.
	// -- "godless_mrk_add:<pos.x> <pos.z> <radius> <color.r> <color.g> <color.b> <color.a> <text>"
	// p.emit(debugCommand{message: "godless_mrk_add:0 0 100 1 1 1 1 world"})

	// set markers on top of the metal spots
	// for _, spot := range p.world.metalSpots {
	// 	p.emit(debugCommand{message: "godless_mrk_add:" +
	// 		formatFloat(spot[0]) + " " + formatFloat(spot[2]) + " 10 1 1 0 1 metal"})
	// }

	// for _, squad := range p.world.squads.squads {
	// 	p.emit(logCommand{message: fmt.Sprintf("squad %d (%s) has units %v", squad.ID, squad.SquadType, squad.UnitIDs())})
	// }
	p.emit(logCommand{message: "1"})
}
