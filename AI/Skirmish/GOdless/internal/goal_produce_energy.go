/* This file is part of the Spring engine (GPL v2 or later), see LICENSE.html */

package internal

const (
	goalProduceEnergyPriority = 95

	// energyTargetIncome is the surplus the goal builds toward; below it the
	// goal keeps adding generators, above it it rests so metal can take over.
	energyTargetIncome = float32(30)
)

// goalProduceEnergy keeps the base powered by placing energy generators for the
// base-builder squad. It asks the BasePlanner where each one goes, so they pack
// behind the base without blocking paths.
type goalProduceEnergy struct{}

func newGoalProduceEnergy() *goalProduceEnergy { return &goalProduceEnergy{} }

func (g *goalProduceEnergy) Name() string { return "produce-energy" }

func (g *goalProduceEnergy) Priority() int { return goalProduceEnergyPriority }

func (g *goalProduceEnergy) Satisfied(w *World) bool {
	if !w.planner.Anchored() {
		return true
	}
	if !g.anyBuilderCanBuildEnergy(w) {
		return true
	}
	// Healthy power: comfortable surplus and not draining storage.
	return w.energyIncome > w.energyUsage+energyTargetIncome && w.energyAmount > w.energyCapacity*0.5
}

func (g *goalProduceEnergy) Plan(w *World) []Command {
	builderID, energyDefID, ok := g.findIdleEnergyBuilder(w)
	if !ok {
		return nil
	}

	pos, facing, ok := w.PlanBuilding(energyDefID)
	if !ok {
		return nil
	}

	order := SquadOrder{Kind: SquadOrderBuild, UnitDefID: energyDefID, Pos: pos, Facing: facing}

	// Gather the idle squad onto one generator: the engine merges builders given
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

func (g *goalProduceEnergy) anyBuilderCanBuildEnergy(w *World) bool {
	for _, unit := range w.units {
		if g.bestEnergyBuildOption(w, unit.ID) != invalidUnitID {
			return true
		}
	}
	return false
}

func (g *goalProduceEnergy) findIdleEnergyBuilder(w *World) (builderID, energyDefID int, ok bool) {
	for _, candidateID := range w.squads.UnitIDsByType(SquadTypeBaseBuilder) {
		unit, hasUnit := w.units[candidateID]
		if !hasUnit || unit.state != UnitStateIdle {
			continue
		}
		if option := g.bestEnergyBuildOption(w, candidateID); option != invalidUnitID {
			return candidateID, option, true
		}
	}
	return invalidUnitID, invalidUnitID, false
}

// bestEnergyBuildOption picks the generator with the best income per metal cost.
// It favours wind when the map's average wind exceeds a solar's flat output,
// otherwise solar, so the goal suits both windy and still maps.
func (g *goalProduceEnergy) bestEnergyBuildOption(w *World, builderID int) int {
	builder, hasBuilder := w.units[builderID]
	if !hasBuilder {
		return invalidUnitID
	}
	builderDef, hasBuilderDef := w.unitDefs[builder.unitDefID]
	if !hasBuilderDef {
		return invalidUnitID
	}

	bestDefID := invalidUnitID
	bestValue := float32(0)
	for optionDefID := range builderDef.BuildOptions {
		optionDef, ok := w.unitDefs[optionDefID]
		if !ok || optionDef.MetalCost <= 0 {
			continue
		}
		output := optionDef.EnergyGeneration
		if optionDef.WindGeneration > 0 {
			output += w.avgWind
		}
		if output <= 0 {
			continue
		}
		value := output / optionDef.MetalCost
		if value > bestValue {
			bestValue = value
			bestDefID = optionDefID
		}
	}
	return bestDefID
}
