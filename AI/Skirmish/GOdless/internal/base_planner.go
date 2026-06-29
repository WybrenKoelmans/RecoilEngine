/* This file is part of the Spring engine (GPL v2 or later), see LICENSE.html */

package internal

import "math"

// elmosPerSquare is the world size (in elmos) of one footprint square. Unit
// footprints (FootprintX/Z) are measured in these squares, so a footprint of
// N spans N*elmosPerSquare elmos.
const elmosPerSquare = 8

// Building facings, matching the engine's convention (south/east/north/west).
const (
	facingSouth = 0
	facingEast  = 1
	facingNorth = 2
	facingWest  = 3
)

// planner spacing/zone tuning, all in elmos.
const (
	// walkGap leaves room to path between non-wind buildings.
	walkGap = 32
	// windBlockCols/Rows define the 2x6 wind block; clusters are separated by a
	// one-windmill gap so units can path between blocks.
	windBlockCols = 2
	windBlockRows = 6
	windClusters  = 3 // wind clusters laid out side by side before stacking back
	// energyBackOffset / factoryFrontOffset push energy behind and factories
	// in front of the spawn anchor before the first ring of slots.
	energyBackOffset   = 96
	factoryFrontOffset = 160
	ringStart          = 64
)

// Building is a footprint already on the map. The planner is stateless: every
// request supplies the current buildings so it packs around whatever exists -
// the starting base, mexes, and anything it placed earlier - rather than
// tracking its own history.
type Building struct {
	Pos        [3]float32
	FootprintX int
	FootprintZ int
	Role       BuildingRole
}

// rect is an axis-aligned footprint in the planner's local (u, v) frame, where
// u points toward the map center (front) and v points to the right. Centre and
// half-extents are in elmos.
type rect struct {
	u, v  float32
	halfU float32
	halfV float32
}

func (r rect) overlaps(o rect, margin float32) bool {
	return math.Abs(float64(r.u-o.u)) < float64(r.halfU+o.halfU+margin) &&
		math.Abs(float64(r.v-o.v)) < float64(r.halfV+o.halfV+margin)
}

// BasePlanner decides where new buildings should go. It works in a local frame
// anchored at the spawn, with +u toward the map center (where the enemy most
// likely is) and +v to the right. Energy goes toward the back (-u), factories
// toward and facing the center (+u); everything is packed without overlapping,
// leaving walk gaps so the base stays compact but passable. It holds no
// placement state: the caller passes the existing buildings on every call.
type BasePlanner struct {
	anchor   [3]float32
	front    [2]float32 // unit vector toward map center
	right    [2]float32 // unit vector 90deg right of front
	anchored bool
}

func NewBasePlanner() *BasePlanner { return &BasePlanner{} }

func (bp *BasePlanner) Anchored() bool { return bp.anchored }

// SetAnchor fixes the planner's origin at the spawn and orients +u toward the
// map center.
func (bp *BasePlanner) SetAnchor(start [3]float32, mapWidth, mapHeight int) {
	dx := float32(mapWidth)/2 - start[0]
	dz := float32(mapHeight)/2 - start[2]
	length := float32(math.Hypot(float64(dx), float64(dz)))
	if length < 1 {
		dx, dz, length = 0, 1, 1 // map center == spawn: pick an arbitrary axis
	}
	bp.anchor = start
	bp.front = [2]float32{dx / length, dz / length}
	bp.right = [2]float32{-bp.front[1], bp.front[0]}
	bp.anchored = true
}

// PlanBuilding returns where a building of the given role and footprint should
// go, the facing it should use, and whether a slot was found. existing is every
// building currently on the map, so the slot never collides with them.
func (bp *BasePlanner) PlanBuilding(role BuildingRole, footprintX, footprintZ int, existing []Building) (pos [3]float32, facing int, ok bool) {
	if !bp.anchored {
		return pos, facingSouth, false
	}

	halfU, halfV := bp.halfExtents(footprintX, footprintZ)
	occupied := bp.occupiedRects(existing)

	switch role {
	case RoleWindGenerator:
		return bp.planWind(halfU, halfV, occupied)
	case RoleFactory:
		return bp.planFactory(halfU, halfV, occupied)
	default:
		return bp.planRing(halfU, halfV, occupied)
	}
}

// PlanNano returns the spot within reach of the most existing buildings, so an
// immobile constructor assists as many of them as possible.
func (bp *BasePlanner) PlanNano(footprintX, footprintZ int, reachRadius float32, existing []Building) ([3]float32, int, bool) {
	occupied := bp.occupiedRects(existing)
	if !bp.anchored || len(occupied) == 0 {
		return bp.PlanBuilding(RoleOther, footprintX, footprintZ, existing)
	}

	bestIdx, bestCount := -1, -1
	for i, c := range occupied {
		count := 0
		for _, o := range occupied {
			du, dv := c.u-o.u, c.v-o.v
			if du*du+dv*dv <= reachRadius*reachRadius {
				count++
			}
		}
		if count > bestCount {
			bestIdx, bestCount = i, count
		}
	}

	halfU, halfV := bp.halfExtents(footprintX, footprintZ)
	center := occupied[bestIdx]
	return bp.searchFrom(halfU, halfV, center.u, center.v, occupied), bp.faceCenter(), true
}

// planWind lays windmills in 2x6 blocks separated by a one-windmill gap, packed
// toward the back of the base, skipping any block slot already taken.
func (bp *BasePlanner) planWind(halfU, halfV float32, occupied []rect) ([3]float32, int, bool) {
	stepV := halfV * 2
	stepU := halfU * 2
	for n := 0; n < windBlockCols*windBlockRows*windClusters*8; n++ {
		cluster := n / (windBlockCols * windBlockRows)
		inBlock := n % (windBlockCols * windBlockRows)
		col := inBlock % windBlockCols
		row := inBlock / windBlockCols
		clusterCol := cluster % windClusters
		clusterRow := cluster / windClusters

		vCell := float32(clusterCol*(windBlockCols+1) + col)
		uCell := float32(clusterRow*(windBlockRows+1) + row)
		v := (vCell - float32(windClusters*(windBlockCols+1))/2) * stepV
		u := -energyBackOffset - uCell*stepU

		cand := rect{u: u, v: v, halfU: halfU, halfV: halfV}
		if bp.free(cand, occupied, 0) {
			return bp.worldPos(u, v), bp.faceCenter(), true
		}
	}
	return [3]float32{}, facingSouth, false
}

func (bp *BasePlanner) planFactory(halfU, halfV float32, occupied []rect) ([3]float32, int, bool) {
	return bp.search(halfU, halfV, factoryFrontOffset, +1, occupied)
}

func (bp *BasePlanner) planRing(halfU, halfV float32, occupied []rect) ([3]float32, int, bool) {
	return bp.search(halfU, halfV, ringStart, -1, occupied)
}

// search raster-scans free slots outward from the anchor along dir (+1 front,
// -1 back), returning the first that does not overlap an existing building.
func (bp *BasePlanner) search(halfU, halfV, offset float32, dir int, occupied []rect) ([3]float32, int, bool) {
	stepU := halfU*2 + walkGap
	stepV := halfV*2 + walkGap
	for ring := 0; ring < 40; ring++ {
		u := float32(dir) * (offset + float32(ring)*stepU)
		for k := 0; k <= ring*2; k++ {
			lane := (k + 1) / 2
			if k%2 == 1 {
				lane = -lane
			}
			v := float32(lane) * stepV
			cand := rect{u: u, v: v, halfU: halfU, halfV: halfV}
			if bp.free(cand, occupied, walkGap) {
				return bp.worldPos(u, v), bp.faceCenter(), true
			}
		}
	}
	return [3]float32{}, facingSouth, false
}

// searchFrom finds the nearest free slot around a starting (u, v), used to seat
// a nano beside the densest cluster without sitting on a building.
func (bp *BasePlanner) searchFrom(halfU, halfV, u0, v0 float32, occupied []rect) [3]float32 {
	stepU := halfU*2 + walkGap
	stepV := halfV*2 + walkGap
	for ring := 0; ring < 12; ring++ {
		for du := -ring; du <= ring; du++ {
			for dv := -ring; dv <= ring; dv++ {
				u := u0 + float32(du)*stepU
				v := v0 + float32(dv)*stepV
				cand := rect{u: u, v: v, halfU: halfU, halfV: halfV}
				if bp.free(cand, occupied, walkGap) {
					return bp.worldPos(u, v)
				}
			}
		}
	}
	return bp.worldPos(u0, v0)
}

func (bp *BasePlanner) free(c rect, occupied []rect, margin float32) bool {
	for _, o := range occupied {
		if c.overlaps(o, margin) {
			return false
		}
	}
	return true
}

func (bp *BasePlanner) halfExtents(footprintX, footprintZ int) (halfU, halfV float32) {
	halfU = float32(footprintZ) * elmosPerSquare / 2
	halfV = float32(footprintX) * elmosPerSquare / 2
	if halfU < elmosPerSquare {
		halfU = elmosPerSquare
	}
	if halfV < elmosPerSquare {
		halfV = elmosPerSquare
	}
	return halfU, halfV
}

// occupiedRects projects every existing building into the planner's local frame
// so collision tests are a simple axis-aligned check.
func (bp *BasePlanner) occupiedRects(existing []Building) []rect {
	rects := make([]rect, 0, len(existing))
	for _, b := range existing {
		halfU, halfV := bp.halfExtents(b.FootprintX, b.FootprintZ)
		u, v := bp.localPos(b.Pos)
		rects = append(rects, rect{u: u, v: v, halfU: halfU, halfV: halfV})
	}
	return rects
}

// worldPos converts a local (u, v) slot back to a world position.
func (bp *BasePlanner) worldPos(u, v float32) [3]float32 {
	return [3]float32{
		bp.anchor[0] + bp.front[0]*u + bp.right[0]*v,
		bp.anchor[1],
		bp.anchor[2] + bp.front[1]*u + bp.right[1]*v,
	}
}

// localPos projects a world position into the planner's (u, v) frame.
func (bp *BasePlanner) localPos(p [3]float32) (u, v float32) {
	dx := p[0] - bp.anchor[0]
	dz := p[2] - bp.anchor[2]
	return dx*bp.front[0] + dz*bp.front[1], dx*bp.right[0] + dz*bp.right[1]
}

// faceCenter returns the cardinal facing whose direction best points toward the
// map center, so factory exits open toward the enemy.
func (bp *BasePlanner) faceCenter() int {
	dx, dz := bp.front[0], bp.front[1]
	if math.Abs(float64(dx)) >= math.Abs(float64(dz)) {
		if dx >= 0 {
			return facingEast
		}
		return facingWest
	}
	if dz >= 0 {
		return facingSouth
	}
	return facingNorth
}
