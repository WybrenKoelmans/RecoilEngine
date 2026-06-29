/* This file is part of the Spring engine (GPL v2 or later), see LICENSE.html */

package internal

const (
	goalProduceMetalPriority = 99

	// spotClaimRadius marks whether a metal spot is already occupied by a mex.
	spotClaimRadius      = float32(64)
	spotClaimRadiusSq    = spotClaimRadius * spotClaimRadius
	reservationTTLFrames = 30 * 20

	// baseAnchorRadius keeps the base builders anchored near the spawn: only
	// metal spots within this distance (elmos) of the start position are
	// claimed, so the squad expands the base outward instead of wandering across
	// the map chasing the nearest free spot.
	baseAnchorRadius   = float32(600)
	baseAnchorRadiusSq = baseAnchorRadius * baseAnchorRadius
)

// goalProduceMetal keeps the economy alive by expanding onto free metal spots.
// It places one extractor order per tick using an idle base-builder.
type goalProduceMetal struct {
	reservedSpots map[int]int // spot index -> frame reserved
}

func newGoalProduceMetal() *goalProduceMetal {
	return &goalProduceMetal{reservedSpots: make(map[int]int)}
}

func (g *goalProduceMetal) Name() string { return "produce-metal" }

func (g *goalProduceMetal) Priority() int { return goalProduceMetalPriority }

func (g *goalProduceMetal) Satisfied(w *World) bool {
	g.cleanReservations(w)

	if len(w.metalSpots) == 0 {
		return true
	}

	if !g.anyBuilderCanBuildExtractor(w) {
		return true
	}

	return len(g.freeSpotIndexes(w)) == 0
}

func (g *goalProduceMetal) Plan(w *World) []Command {
	g.cleanReservations(w)

	builderID, extractorDefID, ok := g.findIdleExtractorBuilder(w)
	if !ok {
		return nil
	}

	spotIdx, ok := g.closestFreeSpotTo(w, w.units[builderID].position)
	if !ok {
		return nil
	}

	spotPos := w.metalSpots[spotIdx].Position
	g.reservedSpots[spotIdx] = w.frame

	order := SquadOrder{Kind: SquadOrderBuild, UnitDefID: extractorDefID, Pos: spotPos}

	// Send the whole idle squad to raise this one extractor together: the engine
	// merges builders issued the same build at the same spot onto a single
	// nanoframe (one builds, the rest assist). builderID is already idle, so
	// gather the squad's other idle members alongside it.
	builderIDs := []int{builderID}
	squad, squadFound := w.squads.SquadForUnit(builderID)
	if squadFound {
		for _, candidateID := range squad.UnitIDs() {
			if candidateID == builderID {
				continue
			}
			if unit, ok := w.units[candidateID]; ok && unit.state == UnitStateIdle {
				builderIDs = append(builderIDs, candidateID)
			}
		}

		// Remember the order so members that go idle later are routed to the same
		// job by goalAssistSquad. This stays a build-at-position order only until
		// the extractor unit exists; World.onSquadBuildStarted then upgrades it to
		// "assist that unit", so late joiners help finish it instead of starting a
		// duplicate beside it.
		squad.LastOrder = order
	}

	for _, id := range builderIDs {
		w.applyOrderState(id, order)
	}

	return []Command{order.command(builderIDs)}
}

func (g *goalProduceMetal) anyBuilderCanBuildExtractor(w *World) bool {
	for _, unit := range w.units {
		if g.bestExtractorBuildOption(w, unit.ID) != invalidUnitID {
			return true
		}
	}
	return false
}

func (g *goalProduceMetal) findIdleExtractorBuilder(w *World) (builderID int, extractorDefID int, ok bool) {
	builderID = invalidUnitID
	extractorDefID = invalidUnitID

	for _, candidateID := range w.squads.UnitIDsByType(SquadTypeBaseBuilder) {
		unit, hasUnit := w.units[candidateID]
		if !hasUnit || unit.state != UnitStateIdle {
			continue
		}

		if option := g.bestExtractorBuildOption(w, candidateID); option != invalidUnitID {
			return candidateID, option, true
		}
	}

	return builderID, extractorDefID, false
}

// bestExtractorBuildOption picks the cheapest extractor this builder can make.
func (g *goalProduceMetal) bestExtractorBuildOption(w *World, builderID int) int {
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
		optionDef, hasOption := w.unitDefs[optionDefID]
		if !hasOption || optionDef.MetalExtraction <= 0 {
			continue
		}
		if optionDef.MetalCost < bestCost || bestDefID == invalidUnitID {
			bestCost = optionDef.MetalCost
			bestDefID = optionDefID
		}
	}

	return bestDefID
}

func (g *goalProduceMetal) freeSpotIndexes(w *World) []int {
	anchor, anchored := w.StartPos()

	free := make([]int, 0, len(w.metalSpots))
	for i := range w.metalSpots {
		if g.spotClaimed(w, i) {
			continue
		}
		// Stay anchored to the spawn: skip spots beyond the base radius so the
		// squad does not range across the map. Until the start position is known
		// (it is set from the commander spawn) every spot is eligible.
		if anchored && distanceSq(w.metalSpots[i].Position, anchor) > baseAnchorRadiusSq {
			continue
		}
		free = append(free, i)
	}
	return free
}

func (g *goalProduceMetal) closestFreeSpotTo(w *World, from [3]float32) (int, bool) {
	bestIdx := -1
	bestDist := float32(-1)

	for _, idx := range g.freeSpotIndexes(w) {
		spot := w.metalSpots[idx].Position
		d := distanceSq(from, spot)
		if bestIdx == -1 || d < bestDist {
			bestIdx = idx
			bestDist = d
		}
	}

	return bestIdx, bestIdx != -1
}

func (g *goalProduceMetal) spotClaimed(w *World, spotIdx int) bool {
	if _, reserved := g.reservedSpots[spotIdx]; reserved {
		return true
	}

	spot := w.metalSpots[spotIdx]
	for _, unit := range w.units {
		unitDef, ok := w.unitDefs[unit.unitDefID]
		if !ok || unitDef.MetalExtraction <= 0 {
			continue
		}

		if distanceSq(unit.position, spot.Position) <= spotClaimRadiusSq {
			return true
		}
	}

	return false
}

func (g *goalProduceMetal) cleanReservations(w *World) {
	for spotIdx, reservedAtFrame := range g.reservedSpots {
		if w.frame-reservedAtFrame > reservationTTLFrames {
			delete(g.reservedSpots, spotIdx)
			continue
		}

		if g.spotOccupiedByExtractor(w, spotIdx) {
			delete(g.reservedSpots, spotIdx)
		}
	}
}

func (g *goalProduceMetal) spotOccupiedByExtractor(w *World, spotIdx int) bool {
	spot := w.metalSpots[spotIdx]
	for _, unit := range w.units {
		unitDef, ok := w.unitDefs[unit.unitDefID]
		if !ok || unitDef.MetalExtraction <= 0 {
			continue
		}
		if distanceSq(unit.position, spot.Position) <= spotClaimRadiusSq {
			return true
		}
	}
	return false
}

func distanceSq(a, b [3]float32) float32 {
	dx := a[0] - b[0]
	dz := a[2] - b[2]
	return dx*dx + dz*dz
}
