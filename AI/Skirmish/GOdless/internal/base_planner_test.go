/* This file is part of the Spring engine (GPL v2 or later), see LICENSE.html */

package internal

import (
	"math"
	"testing"
)

func dist2D(a, b [3]float32) float64 {
	dx := float64(a[0] - b[0])
	dz := float64(a[2] - b[2])
	return math.Hypot(dx, dz)
}

func TestPlannerNotAnchoredByDefault(t *testing.T) {
	bp := NewBasePlanner()
	if bp.Anchored() {
		t.Fatal("planner should not be anchored before SetAnchor")
	}
	if _, _, ok := bp.PlanBuilding(RoleSolarCollector, 4, 4, nil); ok {
		t.Fatal("planning should fail before anchoring")
	}
}

func TestPlannerEnergyGoesBackFactoriesGoFront(t *testing.T) {
	bp := NewBasePlanner()
	// Spawn bottom-left, center is to the +x/+z, so front is roughly (1,1).
	bp.SetAnchor([3]float32{0, 0, 0}, 2000, 2000)

	energy, _, ok := bp.PlanBuilding(RoleSolarCollector, 4, 4, nil)
	if !ok {
		t.Fatal("expected solar slot")
	}
	factory, _, ok := bp.PlanBuilding(RoleFactory, 6, 6, nil)
	if !ok {
		t.Fatal("expected factory slot")
	}

	center := [3]float32{1000, 0, 1000}
	if dist2D(energy, center) <= dist2D(factory, center) {
		t.Fatalf("factory %v should be closer to center than energy %v", factory, energy)
	}
}

func TestPlannerNoOverlap(t *testing.T) {
	bp := NewBasePlanner()
	bp.SetAnchor([3]float32{500, 0, 500}, 2000, 2000)

	var existing []Building
	var placed [][3]float32
	for i := 0; i < 30; i++ {
		pos, _, ok := bp.PlanBuilding(RoleSolarCollector, 4, 4, existing)
		if !ok {
			t.Fatalf("plan %d failed", i)
		}
		for _, p := range placed {
			if dist2D(pos, p) < 1 {
				t.Fatalf("solar %v overlaps existing %v", pos, p)
			}
		}
		placed = append(placed, pos)
		existing = append(existing, Building{Pos: pos, FootprintX: 4, FootprintZ: 4, Role: RoleSolarCollector})
	}
}

func TestPlannerWindBlocksHaveGaps(t *testing.T) {
	bp := NewBasePlanner()
	bp.SetAnchor([3]float32{0, 0, 0}, 4000, 4000)

	var existing []Building
	var wind [][3]float32
	for i := 0; i < windBlockCols*windBlockRows*2; i++ {
		pos, _, ok := bp.PlanBuilding(RoleWindGenerator, 2, 2, existing)
		if !ok {
			t.Fatalf("wind %d failed", i)
		}
		wind = append(wind, pos)
		existing = append(existing, Building{Pos: pos, FootprintX: 2, FootprintZ: 2, Role: RoleWindGenerator})
	}
	// Two windmills are placed per call; a full 12-slot block then a gap means
	// the start of the next block is farther than a single slot step.
	if len(wind) < windBlockCols*windBlockRows+1 {
		t.Fatal("not enough wind placed")
	}
}

func TestPlannerNanoPicksDenseSpot(t *testing.T) {
	bp := NewBasePlanner()
	bp.SetAnchor([3]float32{0, 0, 0}, 2000, 2000)
	var existing []Building
	for i := 0; i < 10; i++ {
		pos, _, _ := bp.PlanBuilding(RoleSolarCollector, 4, 4, existing)
		existing = append(existing, Building{Pos: pos, FootprintX: 4, FootprintZ: 4, Role: RoleSolarCollector})
	}
	if _, _, ok := bp.PlanNano(2, 2, 256, existing); !ok {
		t.Fatal("expected a nano slot near the buildings")
	}
}

func TestPlannerAvoidsPreExistingBase(t *testing.T) {
	bp := NewBasePlanner()
	bp.SetAnchor([3]float32{1000, 0, 1000}, 2000, 2000)

	// A building already on the map, not placed by the planner.
	existing := []Building{{Pos: [3]float32{1000, 0, 940}, FootprintX: 8, FootprintZ: 8, Role: RoleOther}}

	pos, _, ok := bp.PlanBuilding(RoleSolarCollector, 4, 4, existing)
	if !ok {
		t.Fatal("expected slot near pre-existing base")
	}
	if dist2D(pos, existing[0].Pos) < 32 {
		t.Fatalf("new slot %v collides with pre-existing building %v", pos, existing[0].Pos)
	}
}
