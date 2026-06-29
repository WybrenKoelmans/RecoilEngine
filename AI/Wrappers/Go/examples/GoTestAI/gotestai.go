/* This file is part of the Spring engine (GPL v2 or later), see LICENSE.html */

// Command GoTestAI is a minimal Skirmish AI built on the Go wrapper
// (AI/Wrappers/Go). It is the Go analogue of AI/Skirmish/CppTestAI and exists
// to exercise the wrapper end-to-end.
//
// Build as a C-shared library so the native (C) AI Interface can load it:
//
//	go build -buildmode=c-shared -o data/SkirmishAI.so .
//
// The resulting library exports init/release/handleEvent (see shim.c) which the
// engine resolves via dlsym, exactly like a C++ Skirmish AI.
package main

import (
	"fmt"

	springai "github.com/beyond-all-reason/RecoilEngine/AI/Wrappers/Go/src/springai"
)

// goTestAI is the AI instance. It just keeps its callback and reacts to a
// couple of events, like CppTestAI does.
type goTestAI struct {
	cb *springai.Callback
}

func (ai *goTestAI) HandleEvent(ev springai.Event) int {
	switch e := ev.(type) {
	case springai.InitEvent:
		// The callback from the init event is the same one we were
		// constructed with; nothing extra to do here.
		_ = e
	case springai.UnitFinishedEvent:
		// GameGetCurrentFrame and GameSendTextMessage are generated from the
		// interface headers (callback_gen.go / command_gen.go).
		msg := fmt.Sprintf("/say Hello Engine (from GoTestAI), unit %d finished at frame %d",
			e.Unit, ai.cb.GameGetCurrentFrame())
		ai.cb.GameSendTextMessage(springai.SendTextMessageCommand{Text: msg, Zone: 0})
	}
	return 0
}

func init() {
	springai.RegisterFactory(func(skirmishAIID int, cb *springai.Callback) springai.AI {
		return &goTestAI{cb: cb}
	})
}

// main is required for a Go executable/library but is never called for a
// c-shared build.
func main() {}
