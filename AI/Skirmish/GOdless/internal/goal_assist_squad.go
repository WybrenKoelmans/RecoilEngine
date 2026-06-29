/* This file is part of the Spring engine (GPL v2 or later), see LICENSE.html */

package internal

const goalAssistSquadPriority = 100

type goalAssistSquad struct{}

func newGoalAssistSquad() *goalAssistSquad { return &goalAssistSquad{} }

func (g *goalAssistSquad) Name() string { return "assist-squad" }

func (g *goalAssistSquad) Priority() int { return goalAssistSquadPriority }

func (g *goalAssistSquad) Satisfied(w *World) bool {
	for _, squad := range w.squads.GetSquadsByType(SquadTypeBaseBuilder) {
		if g.hasBusyMember(w, squad) && len(g.idleMembers(w, squad)) > 0 {
			return false
		}
	}
	return true
}

func (g *goalAssistSquad) Plan(w *World) []Command {
	var cmds []Command

	for _, squad := range w.squads.GetSquadsByType(SquadTypeBaseBuilder) {
		if !g.hasBusyMember(w, squad) {
			continue
		}

		idle := g.idleMembers(w, squad)
		if len(idle) == 0 {
			continue
		}

		cmd := squad.LastOrder.command(idle)
		if cmd == nil {
			continue
		}

		// Record the order's resulting state on the rejoining members so a later
		// tick does not see them as idle and re-issue it. Assist orders also
		// remember their target, so the members are freed when it finishes.
		for _, id := range idle {
			w.applyOrderState(id, squad.LastOrder)
		}

		cmds = append(cmds, cmd)
	}

	return cmds
}

// hasBusyMember reports whether any member of the squad is doing something other
// than standing idle.
func (g *goalAssistSquad) hasBusyMember(w *World, squad *Squad) bool {
	for id := range squad.Units {
		if unit, ok := w.units[id]; ok && unit.state != UnitStateIdle {
			return true
		}
	}
	return false
}

// idleMembers returns the squad's idle members in stable ID order.
func (g *goalAssistSquad) idleMembers(w *World, squad *Squad) []int {
	ids := make([]int, 0, len(squad.Units))
	for _, id := range squad.UnitIDs() {
		if unit, ok := w.units[id]; ok && unit.state == UnitStateIdle {
			ids = append(ids, id)
		}
	}
	return ids
}
