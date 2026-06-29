/* This file is part of the Spring engine (GPL v2 or later), see LICENSE.html */

package internal

type MetalSpot struct {
	Position [3]float32
	Richness float32
}

// World is the AI's private model of the game, rebuilt entirely from the plain
// event structs the engine delivers. It holds no engine pointers, so the
// processor goroutine can read and mutate it freely without touching the
// callback or racing the engine thread.
type World struct {
	units            map[int]Unit
	enemies          map[int]Unit
	unitDefs         map[int]UnitDef
	squads           *SquadManager
	planner          *BasePlanner
	frame            int
	teamID           int
	metalResourceID  int
	energyResourceID int
	metalSpots       []MetalSpot
	avgWind          float32
	mapWidth         int // elmos
	mapHeight        int // elmos
	startPos         [3]float32
	hasStartPos      bool
	metalIncome      float32
	metalUsage       float32
	energyIncome     float32
	energyUsage      float32
	metalAmount      float32
	energyAmount     float32
	metalCapacity    float32
	energyCapacity   float32
}

// NewWorld returns an empty world model.
func NewWorld() *World {
	return &World{
		units:    make(map[int]Unit),
		enemies:  make(map[int]Unit),
		unitDefs: make(map[int]UnitDef),
		squads:   NewSquadManager(),
		planner:  NewBasePlanner(),
	}
}

func (w *World) addUnit(id int, pos [3]float32, unitDefID, builderID int) {
	w.units[id] = Unit{ID: id, position: pos, unitDefID: unitDefID, builderID: builderID, assistTargetID: invalidUnitID, underConstruction: builderID != invalidUnitID}
	w.maybeSetStartPos(pos)
}
func (w *World) removeUnit(id int) {
	delete(w.units, id)
	w.squads.RemoveUnit(id)

	// Free anyone that was assisting the removed unit: their target is gone, so
	// leaving them in the building state would strand them. This mirrors
	// finishConstruction, but for a target that died instead of completing.
	for helperID, helper := range w.units {
		if helper.assistTargetID == id {
			w.setUnitState(helperID, UnitStateIdle)
		}
	}
}

func (w *World) MoveSquad(squadID int, pos [3]float32) []Command {
	return w.orderSquad(squadID, SquadOrder{Kind: SquadOrderMove, Pos: pos})
}

func (w *World) MoveSquadsByType(squadType SquadType, pos [3]float32) []Command {
	return w.orderSquadsByType(squadType, SquadOrder{Kind: SquadOrderMove, Pos: pos})
}

func (w *World) AttackWithSquad(squadID int, targetID int) []Command {
	if targetID == invalidUnitID {
		return nil
	}
	return w.orderSquad(squadID, SquadOrder{Kind: SquadOrderAttack, TargetID: targetID})
}

func (w *World) AttackWithSquadsByType(squadType SquadType, targetID int) []Command {
	if targetID == invalidUnitID {
		return nil
	}
	return w.orderSquadsByType(squadType, SquadOrder{Kind: SquadOrderAttack, TargetID: targetID})
}

func (w *World) StopSquad(squadID int) []Command {
	return w.orderSquad(squadID, SquadOrder{Kind: SquadOrderStop})
}

// orderSquad records order as the squad's last order and returns the command
// that carries it out for the squad's current units. Recording it here is what
// lets units joining later be brought in line via Squad.LastOrder.
func (w *World) orderSquad(squadID int, order SquadOrder) []Command {
	squad, ok := w.squads.GetSquad(squadID)
	if !ok {
		return nil
	}
	squad.LastOrder = order
	if cmd := order.command(squad.UnitIDs()); cmd != nil {
		return []Command{cmd}
	}
	return nil
}

// orderSquadsByType issues order to every squad of the given type, recording it
// per squad so each remembers it independently for its own future joiners.
func (w *World) orderSquadsByType(squadType SquadType, order SquadOrder) []Command {
	var cmds []Command
	for _, squad := range w.squads.GetSquadsByType(squadType) {
		cmds = append(cmds, w.orderSquad(squad.ID, order)...)
	}
	return cmds
}

func (w *World) addEnemy(id int)    { w.enemies[id] = Unit{ID: id} }
func (w *World) removeEnemy(id int) { delete(w.enemies, id) }

// setUnitState updates an existing unit's state, leaving the rest untouched. It
// is a no-op for units the world has never seen.
func (w *World) setUnitState(id int, state UnitState) {
	if u, ok := w.units[id]; ok {
		u.state = state
		if state == UnitStateIdle {
			u.assistTargetID = invalidUnitID
		}
		w.units[id] = u
	}
}

// setUnitAssisting marks a unit as actively assisting (building) targetID, so a
// later tick does not treat it as idle and finishConstruction / removeUnit can
// free it when targetID completes or dies.
func (w *World) setUnitAssisting(id, targetID int) {
	if u, ok := w.units[id]; ok {
		u.state = UnitStateBuilding
		u.assistTargetID = targetID
		w.units[id] = u
	}
}

// applyOrderState records, on unitID, the state implied by having just been
// issued order. It is the single place that maps a SquadOrder to unit state, so
// every issuer (a goal starting a job, a goal pulling idle members back in, a
// unit joining a squad) tracks state - and assist targets - identically.
func (w *World) applyOrderState(unitID int, order SquadOrder) {
	if order.Kind == SquadOrderAssist {
		w.setUnitAssisting(unitID, order.TargetID)
		return
	}
	w.setUnitState(unitID, order.unitState())
}

// onSquadBuildStarted upgrades a squad's standing order the moment one of its
// builders actually creates the structure it was sent to build: from "build
// <def> at <pos>" to "assist <the new unit>".
//
// This is the virtual-to-real handoff. While the structure does not yet exist
// the build target is "virtual" - members are given a build-at-position order
// and the engine merges them onto one nanoframe. Once the unit exists, the only
// order that helps finish it is an assist (repairing an incomplete unit feeds it
// build power); re-issuing the build order instead would make a late joiner
// start a duplicate beside it, because the spot now reads as occupied.
func (w *World) onSquadBuildStarted(builderID, newUnitID, newUnitDefID int) {
	squad, ok := w.squads.SquadForUnit(builderID)
	if !ok {
		return
	}
	if squad.LastOrder.Kind != SquadOrderBuild || squad.LastOrder.UnitDefID != newUnitDefID {
		return
	}

	squad.LastOrder = SquadOrder{Kind: SquadOrderAssist, TargetID: newUnitID}

	// Point every member already raising this structure at the new unit so they
	// are all released together when it finishes, not just the one builder the
	// engine credited with starting it.
	for _, id := range squad.UnitIDs() {
		if u, ok := w.units[id]; ok && u.state == UnitStateBuilding {
			w.setUnitAssisting(id, newUnitID)
		}
	}
}

// finishConstruction marks a build site complete and frees the builder that
// created it. UnitIdle normally does this too, but some games do not emit it
// promptly after a build finishes, so UnitFinished is the reliable hand-off.
func (w *World) finishConstruction(id int) {
	u, ok := w.units[id]
	if !ok {
		return
	}
	u.underConstruction = false
	w.units[id] = u
	if u.builderID != invalidUnitID {
		w.setUnitState(u.builderID, UnitStateIdle)
	}
	for helperID, helper := range w.units {
		if helper.assistTargetID == id {
			w.setUnitState(helperID, UnitStateIdle)
		}
	}
}

// maybeSetStartPos remembers the first own-unit position the world ever sees,
// which (the commander spawning) marks the initial spawn location.
func (w *World) maybeSetStartPos(pos [3]float32) {
	if !w.hasStartPos {
		w.startPos = pos
		w.hasStartPos = true
		w.planner.SetAnchor(pos, w.mapWidth, w.mapHeight)
	}
}

// StartPos returns the initial spawn location and whether it is known yet.
func (w *World) StartPos() (pos [3]float32, ok bool) { return w.startPos, w.hasStartPos }

// PlanBuilding asks the base planner where to place a building of the given def
// and returns its world position and facing. The planner is stateless, so it is
// fed every building currently on the map and packs the new slot around them.
// ok is false until the spawn anchor is known or when no slot fits.
func (w *World) PlanBuilding(unitDefID int) (pos [3]float32, facing int, ok bool) {
	def, hasDef := w.unitDefs[unitDefID]
	if !hasDef {
		return pos, 0, false
	}
	return w.planner.PlanBuilding(def.Role(), def.FootprintX, def.FootprintZ, w.buildings())
}

// PlanNano returns where to place an immobile constructor so it reaches as many
// existing buildings as possible.
func (w *World) PlanNano(unitDefID int, reachRadius float32) (pos [3]float32, facing int, ok bool) {
	def, hasDef := w.unitDefs[unitDefID]
	if !hasDef {
		return pos, 0, false
	}
	return w.planner.PlanNano(def.FootprintX, def.FootprintZ, reachRadius, w.buildings())
}

// buildings snapshots every structure currently in the world model so the
// stateless planner can pack new slots around what already exists.
func (w *World) buildings() []Building {
	out := make([]Building, 0, len(w.units))
	for _, u := range w.units {
		def, ok := w.unitDefs[u.unitDefID]
		if !ok || (def.FootprintX <= 0 && def.FootprintZ <= 0) {
			continue
		}
		out = append(out, Building{Pos: u.position, FootprintX: def.FootprintX, FootprintZ: def.FootprintZ, Role: def.Role()})
	}
	return out
}
