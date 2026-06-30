/* This file is part of the Spring engine (GPL v2 or later), see LICENSE.html */

package internal

import "fmt"

const (
	// goalProduceFactoryPriority sits below metal (99) and energy (95) so the
	// economy is set up first; the factory follows once that base can pay for it.
	// That ordering is what sequences the factory after the eco - the higher
	// goals consume the idle builders until they are satisfied or blocked - so
	// this goal only needs a light readiness check, not a high income bar.
	goalProduceFactoryPriority = 90

	// factoryEnergyBuffer is the fraction of energy storage the base should hold
	// before committing to a factory, so raising it does not stall power.
	factoryEnergyBuffer = float32(0.25)
)

// goalProduceFactory builds the base's first factory (lab) once the economy can
// sustain it. It places one with the base-builder squad, asking the BasePlanner
// for a slot toward the front of the base so the factory faces the map center.
type goalProduceFactory struct{}

func newGoalProduceFactory() *goalProduceFactory { return &goalProduceFactory{} }

func (g *goalProduceFactory) Name() string { return "produce-factory" }

func (g *goalProduceFactory) Priority() int { return goalProduceFactoryPriority }

func (g *goalProduceFactory) Satisfied(w *World) bool {
	if !w.planner.Anchored() {
		return true
	}
	if !g.anyBuilderCanBuildFactory(w) {
		return true
	}
	// One factory is enough for now: stop once one exists or is being raised.
	if g.haveFactory(w) {
		return true
	}
	// Wait until the economy runs a surplus and holds an energy buffer, so the
	// factory can be paid for without tipping the base into a deficit. This
	// mirrors what the eco goals build toward, so an idle post-eco base passes.
	return !g.economyReady(w)
}

// economyReady reports whether the base earns more than it spends on both
// resources and holds an energy buffer, i.e. it can afford a factory now.
func (g *goalProduceFactory) economyReady(w *World) bool {
	metalReady := w.metalIncome >= w.metalUsage
	energyReady := w.energyIncome >= w.energyUsage && w.energyAmount > w.energyCapacity*factoryEnergyBuffer
	return metalReady && energyReady
}

func (g *goalProduceFactory) Plan(w *World) []Command {
	builderID, factoryDefID, ok := g.findIdleFactoryBuilder(w)
	if !ok {
		return nil
	}

	pos, facing, ok := w.PlanBuilding(factoryDefID)
	if !ok {
		return nil
	}

	order := SquadOrder{Kind: SquadOrderBuild, UnitDefID: factoryDefID, Pos: pos, Facing: facing}

	// Gather the idle squad onto one factory: the engine merges builders given
	// the same build at the same spot, and World.onSquadBuildStarted upgrades the
	// order to an assist once it exists so late joiners help finish it.
	builderIDs := []int{builderID}
	if squad, ok := w.squads.SquadForUnit(builderID); ok {
		for _, candidateID := range squad.UnitIDs() {
			if candidateID == builderID {
				continue
			}
			if unit, ok := w.units[candidateID]; ok && unit.state == UnitStateIdle {
				builderIDs = append(builderIDs, candidateID)
			}
		}
		squad.LastOrder = order
	}

	for _, id := range builderIDs {
		w.applyOrderState(id, order)
	}

	return []Command{order.command(builderIDs)}
}

// diagnose returns a human-readable summary of every gate this goal checks, so
// debugWorld can log why the factory is or is not being built. Temporary.
func (g *goalProduceFactory) diagnose(w *World) string {
	factoryDefs := 0
	for _, def := range w.unitDefs {
		if def.IsFactory {
			factoryDefs++
		}
	}

	idleBuilders, builderCanBuild := 0, false
	states := ""
	chosenFactory := invalidUnitID
	for _, id := range w.squads.UnitIDsByType(SquadTypeBaseBuilder) {
		if unit, ok := w.units[id]; ok {
			if unit.state == UnitStateIdle {
				idleBuilders++
			}
			states += fmt.Sprintf(" [u%d st=%d assist=%d uc=%v]", id, unit.state, unit.assistTargetID, unit.underConstruction)
		}
		if option := g.bestFactoryBuildOption(w, id); option != invalidUnitID {
			builderCanBuild = true
			chosenFactory = option
		}
	}

	chosenMWD := float32(0)
	if def, ok := w.unitDefs[chosenFactory]; ok {
		chosenMWD = def.MinWaterDepth
	}

	metalSat := newGoalProduceMetal().Satisfied(w)
	energySat := newGoalProduceEnergy().Satisfied(w)

	return fmt.Sprintf(
		"factory-goal: anchored=%v factoryDefs=%d anyBuilderCanBuild=%v haveFactory=%v idleBaseBuilders=%d builderCanBuildFactory=%v chosenFactory=%d(mwd=%.0f) economyReady=%v metalSat=%v energySat=%v (mInc=%.1f mUse=%.1f eInc=%.1f eUse=%.1f eAmt=%.1f eCap=%.1f) satisfied=%v builders:%s",
		w.planner.Anchored(), factoryDefs, g.anyBuilderCanBuildFactory(w), g.haveFactory(w),
		idleBuilders, builderCanBuild, chosenFactory, chosenMWD, g.economyReady(w), metalSat, energySat,
		w.metalIncome, w.metalUsage, w.energyIncome, w.energyUsage, w.energyAmount, w.energyCapacity,
		g.Satisfied(w), states,
	)
}

// haveFactory reports whether the AI already owns a factory, including one still
// under construction, so the goal does not queue a second one.
func (g *goalProduceFactory) haveFactory(w *World) bool {
	for _, unit := range w.units {
		if def, ok := w.unitDefs[unit.unitDefID]; ok && def.IsFactory {
			return true
		}
	}
	return false
}

func (g *goalProduceFactory) anyBuilderCanBuildFactory(w *World) bool {
	for _, unit := range w.units {
		if g.bestFactoryBuildOption(w, unit.ID) != invalidUnitID {
			return true
		}
	}
	return false
}

func (g *goalProduceFactory) findIdleFactoryBuilder(w *World) (builderID, factoryDefID int, ok bool) {
	for _, candidateID := range w.squads.UnitIDsByType(SquadTypeBaseBuilder) {
		unit, hasUnit := w.units[candidateID]
		if !hasUnit || unit.state != UnitStateIdle {
			continue
		}
		if option := g.bestFactoryBuildOption(w, candidateID); option != invalidUnitID {
			return candidateID, option, true
		}
	}
	return invalidUnitID, invalidUnitID, false
}

// bestFactoryBuildOption picks the cheapest factory this builder can make, so
// the AI gets a production line up before sinking metal into pricier labs.
func (g *goalProduceFactory) bestFactoryBuildOption(w *World, builderID int) int {
	builder, hasBuilder := w.units[builderID]
	if !hasBuilder {
		return invalidUnitID
	}
	builderDef, hasBuilderDef := w.unitDefs[builder.unitDefID]
	if !hasBuilderDef {
		return invalidUnitID
	}

	bestDefID := invalidUnitID
	bestCost := float32(0)
	for optionDefID := range builderDef.BuildOptions {
		optionDef, ok := w.unitDefs[optionDefID]
		if !ok || !optionDef.IsFactory || optionDef.MetalCost <= 0 {
			continue
		}
		// Skip factories that must sit in water (e.g. shipyards): the base is
		// anchored on land, so a naval factory never finds a valid site there and
		// the build is rejected forever. Without this the cheapest option is often
		// a shipyard, which is why the factory was never built.
		if optionDef.MinWaterDepth > 0 {
			continue
		}
		if bestDefID == invalidUnitID || optionDef.MetalCost < bestCost {
			bestCost = optionDef.MetalCost
			bestDefID = optionDefID
		}
	}
	return bestDefID
}
