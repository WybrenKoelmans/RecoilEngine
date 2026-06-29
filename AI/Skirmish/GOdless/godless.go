/* This file is part of the Spring engine (GPL v2 or later), see LICENSE.html */

// Command GOdless is a minimal Skirmish AI written in Go, built on the Go
// wrapper (AI/Wrappers/Go). The engine entry points (shim.c / aiexport.go) hand
// every event to HandleEvent on the engine thread; GOdless captures each event
// as a plain struct, queues it for an asynchronous Processor goroutine, and
// drains the commands that goroutine produces back to the engine - all without
// blocking the simulation.
package main

import (
	"github.com/beyond-all-reason/RecoilEngine/AI/Skirmish/GOdless/internal"
	springai "github.com/beyond-all-reason/RecoilEngine/AI/Wrappers/Go/src/springai"
)

const metalResourceID = 0
const energyResourceID = 1
const squareSize = 8

type godlessAI struct {
	cb   *springai.Callback
	proc *internal.Processor
}

// newGodlessAI builds an AI instance and starts its processor goroutine.
func newGodlessAI(cb *springai.Callback) *godlessAI {
	ai := &godlessAI{
		cb:   cb,
		proc: internal.NewProcessor(),
	}
	go ai.proc.Run()
	return ai
}

// HandleEvent runs on the engine thread. It hands the (pointer-free) event to
// the async processor and then executes any commands the processor has produced
// so far. The engine callback is only ever touched here, never from the
// processor goroutine.
func (ai *godlessAI) HandleEvent(ev springai.Event) int {
	ai.proc.Submit(ai.enrich(ev))

	if _, ok := ev.(springai.UpdateEvent); ok {
		ai.drainCommands(true)
	}

	if _, ok := ev.(springai.ReleaseEvent); ok {
		ai.shutdown()
	}
	return 0
}

// Release is called by the wrapper from the engine's C release() entry point.
// It mirrors BARb's release() (delete ai/callback): GOdless must stop its
// processor goroutine here so a reload does not leave it running against a
// torn-down engine, which crashes the game. Close is idempotent, so the earlier
// ReleaseEvent path is harmless if both fire.
func (ai *godlessAI) Release() {
	ai.shutdown()
}

// shutdown stops the processor goroutine and drains any final commands. Safe to
// call multiple times.
func (ai *godlessAI) shutdown() {
	ai.proc.Close()
	ai.drainCommands(false)
}

// enrich attaches data that the processor goroutine cannot fetch itself,
// because reading it requires the engine callback, which may only be touched on
// this (the engine) thread. It captures the value at the event boundary and
// lets it ride into the processor as a plain, pointer-free struct.
func (ai *godlessAI) enrich(ev springai.Event) springai.Event {
	if e, ok := ev.(springai.UnitCreatedEvent); ok {
		p := ai.cb.UnitGetPos(e.Unit)
		unitDefID := ai.cb.UnitGetDef(e.Unit)

		return internal.UnitCreated{Unit: e.Unit, Position: [3]float32{p.X, p.Y, p.Z}, UnitDefID: unitDefID, Builder: e.Builder}
	}

	if e, ok := ev.(springai.InitEvent); ok {
		return enrichInit(ai, e)
	}

	if e, ok := ev.(springai.UnitIdleEvent); ok {
		p := ai.cb.UnitGetPos(e.Unit)

		return internal.UnitIdle{Unit: e.Unit, Position: [3]float32{p.X, p.Y, p.Z}}
	}

	return ev
}

func enrichInit(ai *godlessAI, e springai.InitEvent) springai.Event {
	metalSpotsRaw := ai.cb.MapGetResourceMapSpotsPositions(metalResourceID)
	metalSpots := make([]internal.MetalSpot, len(metalSpotsRaw))
	for i, spot := range metalSpotsRaw {
		elevation := ai.cb.MapGetElevationAt(spot.X, spot.Z)
		metalSpots[i] = internal.MetalSpot{Position: [3]float32{spot.X, elevation, spot.Z}, Richness: spot.Y}
	}

	metalResourceID := ai.cb.GetResourceByName("Metal")
	if metalResourceID < 0 {
		metalResourceID = 0
	}

	energyResourceID := ai.cb.GetResourceByName("Energy")
	if energyResourceID < 0 {
		energyResourceID = 1
	}

	unitDefs := make(map[int]internal.UnitDef)
	for _, unitDefID := range ai.cb.GetUnitDefs() {
		buildOptionIDs := ai.cb.UnitDefGetBuildOptions(unitDefID)

		buildOptions := make(map[int]struct{})
		for _, buildOptionID := range buildOptionIDs {
			buildOptions[buildOptionID] = struct{}{}
		}

		unitDefs[unitDefID] = internal.UnitDef{
			ID:               unitDefID,
			BuildOptions:     buildOptions,
			MetalCost:        ai.cb.UnitDefGetCost(unitDefID, metalResourceID),
			EnergyCost:       ai.cb.UnitDefGetCost(unitDefID, energyResourceID),
			MetalExtraction:  ai.cb.UnitDefGetExtractsResource(unitDefID, metalResourceID),
			WindGeneration:   ai.cb.UnitDefGetWindResourceGenerator(unitDefID, energyResourceID),
			EnergyGeneration: ai.cb.UnitDefGetResourceMake(unitDefID, energyResourceID),
			IsFactory:        len(buildOptions) > 0 && ai.cb.UnitDefGetSpeed(unitDefID) <= 0,
			FootprintX:       ai.cb.UnitDefGetXSize(unitDefID),
			FootprintZ:       ai.cb.UnitDefGetZSize(unitDefID),
		}
	}

	avgWind := (ai.cb.MapGetMinWind() + ai.cb.MapGetMaxWind()) / 2

	return internal.Init{
		SkirmishAIID:     e.SkirmishAIID,
		TeamID:           ai.cb.SkirmishAIGetTeamId(),
		MetalResourceID:  metalResourceID,
		EnergyResourceID: energyResourceID,
		MetalSpots:       metalSpots,
		UnitDefs:         unitDefs,
		AvgWind:          avgWind,
		MapWidth:         ai.cb.MapGetWidth() * squareSize,
		MapHeight:        ai.cb.MapGetHeight() * squareSize,
	}
}

// drainCommands executes every command ready right now, without blocking.
func (ai *godlessAI) drainCommands(submitResponses bool) {
	for {
		select {
		case cmd, ok := <-ai.proc.Commands():
			if !ok {
				return
			}
			// ai.cb.LogLog(fmt.Sprintf("drainCommands: executing command %T", cmd))
			for _, ev := range cmd.Execute(ai.cb) {
				if submitResponses {
					ai.proc.Submit(ev)
				}
			}
		default:
			return
		}
	}
}

func init() {
	springai.RegisterFactory(func(skirmishAIID int, cb *springai.Callback) springai.AI {
		return newGodlessAI(cb)
	})
}

// main is required for a Go library but is never called for a c-shared build.
func main() {}
