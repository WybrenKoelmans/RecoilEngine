package internal

import "sort"

type SquadType int

const (
	// SquadTypeNone is a generic squad with no specific role.
	SquadTypeNone SquadType = iota

	// SquadTypeBaseBuilder is a squad of builders that construct the base.
	SquadTypeBaseBuilder

	// SquadTypeAttacker is a squad of units that attack the enemy.
	SquadTypeAttacker

	SquadTypeExplorer

	SquadTypeExpander
)

func (t SquadType) String() string {
	switch t {
	case SquadTypeBaseBuilder:
		return "base-builder"
	case SquadTypeAttacker:
		return "attacker"
	case SquadTypeExplorer:
		return "explorer"
	case SquadTypeExpander:
		return "expander"
	default:
		return "other"
	}
}

// SquadOrderKind identifies the kind of order last given to a squad.
type SquadOrderKind int

const (
	// SquadOrderNone means the squad has never been given an order.
	SquadOrderNone SquadOrderKind = iota
	SquadOrderMove
	SquadOrderAttack
	SquadOrderStop
	SquadOrderAssist
	SquadOrderBuild
)

// SquadOrder is a unit-agnostic record of an order given to a squad. It captures
// only the order's intent and parameters, not the units it targeted, so it can
// be re-issued to units that join the squad afterwards.
type SquadOrder struct {
	Kind      SquadOrderKind
	Pos       [3]float32
	TargetID  int
	UnitDefID int
	Facing    int
}

// unitState is the state a unit enters once this order is issued to it, so a
// freshly ordered joiner is not treated as idle and re-tasked by a goal.
func (o SquadOrder) unitState() UnitState {
	switch o.Kind {
	case SquadOrderMove:
		return UnitStateMoving
	case SquadOrderAttack:
		return UnitStateAttacking
	case SquadOrderAssist, SquadOrderBuild:
		return UnitStateBuilding
	default:
		return UnitStateIdle
	}
}

type Squad struct {
	// ID is the squad's unique identifier.
	ID int

	// Units is the set of unit IDs that belong to this squad.
	Units map[int]struct{}

	// Type is the squad's role in the army. BaseBuilders, for example, are builders that construct the base, while Attackers are units that attack the enemy.
	SquadType SquadType

	// LastOrder is the most recent order given to this squad. It is re-issued to
	// units that join the squad later so they fall in line with the rest of it.
	LastOrder SquadOrder
}

func (s *Squad) UnitIDs() []int {
	unitIDs := make([]int, 0, len(s.Units))
	for unitID := range s.Units {
		unitIDs = append(unitIDs, unitID)
	}
	sort.Ints(unitIDs)
	return unitIDs
}

type SquadManager struct {
	// squads is the set of all squads, indexed by their unique ID.
	squads map[int]*Squad

	// nextSquadID is the next unique ID to assign to a new squad.
	nextSquadID int
}

func NewSquadManager() *SquadManager {
	return &SquadManager{
		squads:      make(map[int]*Squad),
		nextSquadID: 1,
	}
}

func (sm *SquadManager) CreateSquad(squadType SquadType) *Squad {
	squad := &Squad{
		ID:        sm.nextSquadID,
		Units:     make(map[int]struct{}),
		SquadType: squadType,
	}
	sm.squads[squad.ID] = squad
	sm.nextSquadID++
	return squad
}

func (sm *SquadManager) GetSquad(id int) (*Squad, bool) {
	squad, ok := sm.squads[id]
	return squad, ok
}

func (sm *SquadManager) RemoveSquad(id int) {
	delete(sm.squads, id)
}

func (sm *SquadManager) UnitIDs(squadID int) []int {
	squad, ok := sm.squads[squadID]
	if !ok {
		return nil
	}
	return squad.UnitIDs()
}

func (sm *SquadManager) UnitIDsByType(squadType SquadType) []int {
	unitIDs := []int{}
	for _, squad := range sm.GetSquadsByType(squadType) {
		unitIDs = append(unitIDs, squad.UnitIDs()...)
	}
	sort.Ints(unitIDs)
	return unitIDs
}

func (sm *SquadManager) AddUnitToSquad(squadID, unitID int) bool {
	squad, ok := sm.squads[squadID]
	if !ok {
		return false
	}
	squad.Units[unitID] = struct{}{}
	return true
}

func (sm *SquadManager) AssignUnit(unitID int, squadType SquadType) *Squad {
	sm.RemoveUnit(unitID)

	squads := sm.GetSquadsByType(squadType)
	if len(squads) > 0 {
		squads[0].Units[unitID] = struct{}{}
		return squads[0]
	}

	squad := sm.CreateSquad(squadType)
	squad.Units[unitID] = struct{}{}
	return squad
}

func (sm *SquadManager) RemoveUnit(unitID int) bool {
	removed := false
	for _, squad := range sm.squads {
		if _, ok := squad.Units[unitID]; ok {
			delete(squad.Units, unitID)
			removed = true
		}
	}
	return removed
}

func (sm *SquadManager) RemoveUnitFromSquad(squadID, unitID int) bool {
	squad, ok := sm.squads[squadID]
	if !ok {
		return false
	}
	delete(squad.Units, unitID)
	return true
}

func (sm *SquadManager) GetSquads() []*Squad {
	squads := make([]*Squad, 0, len(sm.squads))
	for _, squad := range sm.squads {
		squads = append(squads, squad)
	}
	return squads
}

func (sm *SquadManager) GetSquadsByType(squadType SquadType) []*Squad {
	squads := make([]*Squad, 0)
	for _, squad := range sm.squads {
		if squad.SquadType == squadType {
			squads = append(squads, squad)
		}
	}
	return squads
}

func (sm *SquadManager) UnitHasSquad(unitID int) bool {
	_, ok := sm.SquadForUnit(unitID)
	return ok
}

// SquadForUnit returns the squad a unit belongs to, if any.
func (sm *SquadManager) SquadForUnit(unitID int) (*Squad, bool) {
	for _, squad := range sm.squads {
		if _, ok := squad.Units[unitID]; ok {
			return squad, true
		}
	}
	return nil, false
}
