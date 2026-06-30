package internal

import (
	"fmt"
	"math"

	springai "github.com/beyond-all-reason/RecoilEngine/AI/Wrappers/Go/src/springai"
)

// command materialises a SquadOrder into the engine command that carries it out
// for the given units, or nil when there is nothing to issue (no recorded order,
// or no units to receive it).
func (o SquadOrder) command(unitIDs []int) Command {
	if len(unitIDs) == 0 {
		return nil
	}
	switch o.Kind {
	case SquadOrderMove:
		return squadMoveCommand{unitIDs: unitIDs, toPos: o.Pos}
	case SquadOrderAttack:
		return squadAttackCommand{unitIDs: unitIDs, targetID: o.TargetID}
	case SquadOrderStop:
		return squadStopCommand{unitIDs: unitIDs}
	case SquadOrderAssist:
		return squadAssistCommand{unitIDs: unitIDs, targetID: o.TargetID}
	case SquadOrderBuild:
		return squadBuildCommand{unitIDs: unitIDs, unitDefID: o.UnitDefID, pos: o.Pos, facing: o.Facing}
	default:
		return nil
	}
}

type squadMoveCommand struct {
	unitIDs []int
	toPos   [3]float32
}

func (a squadMoveCommand) Execute(cb *springai.Callback) []springai.Event {
	for _, unitID := range a.unitIDs {
		cb.UnitMoveTo(springai.MoveUnitCommand{
			UnitID:  unitID,
			GroupID: -1,
			ToPos:   springai.AIFloat3{X: a.toPos[0], Y: a.toPos[1], Z: a.toPos[2]},
			Options: 0,
			TimeOut: math.MaxInt32,
		})
	}
	return nil
}

type squadAttackCommand struct {
	unitIDs  []int
	targetID int
}

func (a squadAttackCommand) Execute(cb *springai.Callback) []springai.Event {
	for _, unitID := range a.unitIDs {
		cb.UnitAttack(springai.AttackUnitCommand{
			UnitID:         unitID,
			GroupID:        -1,
			ToAttackUnitID: a.targetID,
			Options:        0,
			TimeOut:        math.MaxInt32,
		})
	}
	return nil
}

type squadStopCommand struct {
	unitIDs []int
}

func (a squadStopCommand) Execute(cb *springai.Callback) []springai.Event {
	for _, unitID := range a.unitIDs {
		cb.UnitStop(springai.StopUnitCommand{
			UnitID:  unitID,
			GroupID: -1,
			Options: 0,
			TimeOut: math.MaxInt32,
		})
	}
	return nil
}

// squadAssistCommand orders builders to help finish another unit's construction.
// The engine has no distinct "assist" order for build power; repairing a unit
// that is not yet complete contributes build power toward finishing it, which
// is exactly the assist behaviour we want.
type squadAssistCommand struct {
	unitIDs  []int
	targetID int
}

func (a squadAssistCommand) Execute(cb *springai.Callback) []springai.Event {
	for _, unitID := range a.unitIDs {
		cb.UnitRepair(springai.RepairUnitCommand{
			UnitID:         unitID,
			GroupID:        -1,
			Options:        0,
			TimeOut:        math.MaxInt32,
			ToRepairUnitID: a.targetID,
		})
	}
	return nil
}

// buildSearchRadius is how far (in elmos) squadBuildCommand looks for a valid build
// site when the intended position is not buildable. It is generous so large
// structures (factories) still find a spot when the exact planned slot is taken
// or on bad ground.
const buildSearchRadius = 640

// squadBuildCommand orders builder units to construct unitDefID at a world position.
// The intended position comes from the off-thread layout planner; this runs on
// the engine thread, so it is also where terrain validity is settled.
type squadBuildCommand struct {
	unitIDs   []int
	unitDefID int
	pos       [3]float32
	facing    int
}

func (a squadBuildCommand) Execute(cb *springai.Callback) []springai.Event {
	pos := springai.AIFloat3{X: a.pos[0], Y: a.pos[1], Z: a.pos[2]}

	// Keep the planned position on clear ground (this preserves the tight
	// windmill rows and solar spacing) but nudge it to the nearest valid site
	// when it is unbuildable - sloped, under water, or already occupied.
	if !cb.MapIsPossibleToBuildAt(a.unitDefID, pos, a.facing) {
		site := cb.MapFindClosestBuildSite(a.unitDefID, pos, buildSearchRadius, 0, a.facing)
		// A failed search returns an unusable position (e.g. the map origin), so
		// confirm the result is actually buildable before using it. Without this
		// the builder is sent to construct in a corner or handed a doomed order
		// that never starts - which strands it and means the structure (e.g. a
		// factory whose planned slot is blocked) is never built.
		if !cb.MapIsPossibleToBuildAt(a.unitDefID, site, a.facing) {
			cb.LogLog(fmt.Sprintf("build: no valid site for def %d near (%.0f,%.0f) within %.0f - skipping", a.unitDefID, pos.X, pos.Z, float32(buildSearchRadius)))
			return nil
		}
		pos = site
	}

	for _, unitID := range a.unitIDs {
		cb.UnitBuild(springai.BuildUnitCommand{
			UnitID:           unitID,
			GroupID:          -1,
			Options:          0,
			TimeOut:          math.MaxInt32,
			ToBuildUnitDefID: a.unitDefID,
			BuildPos:         pos,
			Facing:           a.facing,
		})
	}
	return nil
}

// reconcileBuildersCommand asks the engine for the real command state of the
// given builders and reports which are idle (no current commands). This heals
// the world model when a builder was optimistically marked Building for an order
// that never produced a follow-up event to clear it.
// type reconcileBuildersCommand struct {
// 	unitIDs []int
// }

// func (a reconcileBuildersCommand) Execute(cb *springai.Callback) []springai.Event {
// 	idle := make([]int, 0, len(a.unitIDs))
// 	for _, unitID := range a.unitIDs {
// 		if cb.UnitGetCurrentCommands(unitID) == 0 {
// 			idle = append(idle, unitID)
// 		}
// 	}
// 	return []springai.Event{BuilderStates{IdleUnitIDs: idle}}
// }

type debugCommand struct {
	message string
}

func (a debugCommand) Execute(cb *springai.Callback) []springai.Event {
	cb.LuaCallRules(a.message)
	return nil
}

type logCommand struct {
	message string
}

func (a logCommand) Execute(cb *springai.Callback) []springai.Event {
	cb.LogLog(a.message)
	return nil
}

type ecoStatsRequestCommand struct {
	teamID           int
	metalResourceID  int
	energyResourceID int
}

func (a ecoStatsRequestCommand) Execute(cb *springai.Callback) []springai.Event {
	metalCurrent := cb.GameGetTeamResourceCurrent(a.teamID, a.metalResourceID)
	metalCapacity := cb.GameGetTeamResourceStorage(a.teamID, a.metalResourceID)
	metalIncome := cb.GameGetTeamResourceIncome(a.teamID, a.metalResourceID)
	metalUsage := cb.GameGetTeamResourceUsage(a.teamID, a.metalResourceID)

	energyCurrent := cb.GameGetTeamResourceCurrent(a.teamID, a.energyResourceID)
	energyCapacity := cb.GameGetTeamResourceStorage(a.teamID, a.energyResourceID)
	energyIncome := cb.GameGetTeamResourceIncome(a.teamID, a.energyResourceID)
	energyUsage := cb.GameGetTeamResourceUsage(a.teamID, a.energyResourceID)

	return []springai.Event{EconomyStats{
		MetalCurrent:  metalCurrent,
		MetalIncome:   metalIncome,
		MetalCapacity: metalCapacity,
		MetalUsage:    metalUsage,

		EnergyCurrent:  energyCurrent,
		EnergyIncome:   energyIncome,
		EnergyCapacity: energyCapacity,
		EnergyUsage:    energyUsage,
	}}
}
