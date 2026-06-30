/* This file is part of the Spring engine (GPL v2 or later), see LICENSE.html */

package internal

// DecisionMaker is the AI's "brain". On each tick it reads the World model,
// walks its goals in priority order, and returns the commands that make
// progress toward the most important goal that still has work to do.
//
// It deliberately runs on the processor goroutine, NOT the engine thread: that
// keeps it off the simulation thread (so it never blocks the engine and never
// touches the engine callback) while still sharing single-threaded access to
// the World with the rest of the processor - so it needs no locks and cannot
// race the world updates that feed it. The commands it returns are handed back
// to the engine thread to execute, the same as every other command.
type DecisionMaker struct {
	goals []Goal
}

// Goal is one thing the AI wants to achieve. Goals are ranked by Priority and
// evaluated against the (processor-owned) World model.
type Goal interface {
	// Name identifies the goal in logs.
	Name() string
	// Priority ranks the goal against the others; higher is more important.
	Priority() int
	// Satisfied reports whether the goal needs no further action for now.
	Satisfied(w *World) bool
	// Plan returns commands that make progress toward the goal, or nil when the
	// goal currently has nothing it can do (e.g. it is waiting on a busy
	// builder). Plan may record its own intent and update the World to reflect
	// the orders it issues, so a later tick does not re-issue them.
	Plan(w *World) []Command
}

// NewDecisionMaker builds the brain with its initial set of goals, ordered once
// by descending priority (priorities are static for now).
func NewDecisionMaker() *DecisionMaker {
	return &DecisionMaker{
		goals: []Goal{
			newGoalAssistSquad(),
			newGoalProduceMetal(),
			newGoalProduceEnergy(),
			newGoalProduceFactory(),
		},
	}
}

// Tick evaluates goals in priority order and returns the commands of the
// highest-priority goal that still has work to do this tick. It focuses on a
// single goal per tick so goals never fight over the same idle builder; the
// next tick re-evaluates from the top. Returns nil when every goal is satisfied
// or blocked.
func (d *DecisionMaker) Tick(w *World) []Command {
	for _, g := range d.goals {
		if g.Satisfied(w) {
			continue
		}
		if cmds := g.Plan(w); len(cmds) > 0 {
			return cmds
		}
	}
	return nil
}

// SquadTypeFor chooses the squad role a unit should enter. The processor owns
// applying this decision to World state.
func (d *DecisionMaker) SquadTypeFor(w *World, unitID int) SquadType {
	unit := w.units[unitID]
	unitDef := w.unitDefs[unit.unitDefID]

	// for now put all builders in the base-builder squad and all other units in a generic squad
	if len(unitDef.BuildOptions) > 0 {
		return SquadTypeBaseBuilder
	}

	return SquadTypeNone
}
