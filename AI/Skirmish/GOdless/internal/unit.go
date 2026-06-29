package internal

type UnitState int

const (
	UnitStateIdle UnitState = iota
	UnitStateMoving
	UnitStateAttacking
	UnitStateBuilding
)

const invalidUnitID = -1

type Unit struct {
	ID                int
	frame             int
	position          [3]float32
	state             UnitState
	unitDefID         int
	builderID         int
	assistTargetID    int
	underConstruction bool // built by another unit and not yet finished
}

type UnitDef struct {
	ID               int
	BuildOptions     map[int]struct{} // building unitDef IDs this unit can build
	MetalCost        float32
	EnergyCost       float32
	MetalExtraction  float32
	WindGeneration   float32
	EnergyGeneration float32
	IsFactory        bool
	FootprintX       int
	FootprintZ       int
}

// Role classifies what a unit def is for, so the base planner can decide where
// to place it without re-deriving the same flags everywhere.
func (d UnitDef) Role() BuildingRole {
	switch {
	case d.MetalExtraction > 0:
		return RoleMetalExtractor
	case d.IsFactory:
		return RoleFactory
	case d.WindGeneration > 0:
		return RoleWindGenerator
	case d.EnergyGeneration > 0:
		return RoleSolarCollector
	default:
		return RoleOther
	}
}
