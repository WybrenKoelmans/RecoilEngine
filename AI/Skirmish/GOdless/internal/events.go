package internal

import (
	springai "github.com/beyond-all-reason/RecoilEngine/AI/Wrappers/Go/src/springai"
)

type UnitCreated struct {
	Unit      int
	Position  [3]float32
	UnitDefID int
	Builder   int // unit that is constructing this one, or -1 if pre-placed/given
}

func (UnitCreated) Topic() springai.EventTopic { return springai.EventUnitCreated }

type BuildingRole int

const (
	RoleOther BuildingRole = iota
	RoleMetalExtractor
	RoleWindGenerator
	RoleSolarCollector
	RoleFactory
)

type Init struct {
	SkirmishAIID     int
	TeamID           int
	MetalResourceID  int
	EnergyResourceID int
	MetalSpots       []MetalSpot
	UnitDefs         map[int]UnitDef
	AvgWind          float32
	MapWidth         int // map width in elmos
	MapHeight        int // map height in elmos
}

func (Init) Topic() springai.EventTopic { return springai.EventInit }

type UnitIdle struct {
	Unit     int
	Position [3]float32
}

func (UnitIdle) Topic() springai.EventTopic { return springai.EventUnitIdle }

type EconomyStats struct {
	MetalIncome   float32
	MetalUsage    float32
	MetalCapacity float32
	MetalCurrent  float32

	EnergyIncome   float32
	EnergyUsage    float32
	EnergyCapacity float32
	EnergyCurrent  float32
}

func (EconomyStats) Topic() springai.EventTopic { return springai.EventUpdate }
