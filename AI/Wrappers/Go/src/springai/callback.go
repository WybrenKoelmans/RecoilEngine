/* This file is part of the Spring engine (GPL v2 or later), see LICENSE.html */

package springai

/*
#cgo CFLAGS: -I${SRCDIR}/../../../../../rts -I${SRCDIR}/../../../../../rts/ExternalAI/Interface

#include <stdlib.h>
#include <stdbool.h>

#include "SSkirmishAICallback.h"

// Bridge for the one callback every command sender needs. The full set of
// query bridges/methods is generated into callback_gen.go (see bin/gen). All
// bridge trampolines are `static` so the per-file C objects never clash at link
// time.
static int bridge_Engine_handleCommand(struct SSkirmishAICallback* c, int aiId, int toId, int cmdId, int cmdTopic, void* cmdData) {
	return c->Engine_handleCommand(aiId, toId, cmdId, cmdTopic, cmdData);
}
*/
import "C"

import "unsafe"

// Callback is the OO wrapper over the engine's SSkirmishAICallback struct of
// function pointers. It is the Go analogue of the generated
// springai::OOAICallback / WrappOOAICallback of the C++ wrapper, and is the
// AI's single handle for querying engine state and issuing commands.
//
// The query methods (one per SSkirmishAICallback function pointer) and command
// senders (one per S*Command) are GENERATED into callback_gen.go and
// command_gen.go by bin/gen. This file holds only the hand-written core.
type Callback struct {
	c            *C.struct_SSkirmishAICallback
	skirmishAIID int
}

// NewCallback wraps the raw SSkirmishAICallback* the engine supplies in the
// init() entry point / SInitEvent. The pointer is passed across the package
// boundary as unsafe.Pointer because cgo C types are not identical between
// packages; both sides include the same header, so the layout matches.
func NewCallback(raw unsafe.Pointer, skirmishAIID int) *Callback {
	return &Callback{
		c:            (*C.struct_SSkirmishAICallback)(raw),
		skirmishAIID: skirmishAIID,
	}
}

// SkirmishAIID returns the id of the AI instance this callback belongs to.
func (cb *Callback) SkirmishAIID() int { return cb.skirmishAIID }

// handleCommand is the low-level command dispatch used by the generated command
// senders. commandData must point at the C S*Command struct matching cmdTopic.
func (cb *Callback) handleCommand(toID int, cmdID int, cmdTopic CommandTopic, commandData unsafe.Pointer) int {
	return int(C.bridge_Engine_handleCommand(cb.c,
		C.int(cb.skirmishAIID),
		C.int(toID),
		C.int(cmdID),
		C.int(cmdTopic),
		commandData))
}
