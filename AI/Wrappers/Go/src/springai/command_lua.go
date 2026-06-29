/* This file is part of the Spring engine (GPL v2 or later), see LICENSE.html */

package springai

/*
#cgo CFLAGS: -I${SRCDIR}/../../../../../rts -I${SRCDIR}/../../../../../rts/ExternalAI/Interface

#include <stdlib.h>
#include <stdbool.h>

#include "AISCommands.h"
#include "aidefines.h"
*/
import "C"

import "unsafe"

// The COMMAND_CALL_LUA_* commands are hand-wired here rather than generated
// into command_gen.go: they carry an engine-written ret_outData output buffer,
// which the generator deliberately skips (see the skip list at the bottom of
// command_gen.go and the "need hand-wiring" note in AI/Wrappers/Go/README.md).
// The method names mirror what the generator would emit from the Lua_callRules
// / Lua_callUI hints, so a future generator that learns this pattern can drop
// this file without changing the public API.

// LuaCallRules sends inData to the synced Lua state (LuaRules), invoking the
// gadget callin RecvSkirmishAIMessage(aiTeam, inData). It returns the string
// the Lua handler returns (empty if it returns nothing) and the engine result
// code (0 = ok).
func (cb *Callback) LuaCallRules(inData string) (string, int) {
	return cb.callLua(CommandCallLuaRules, inData)
}

// LuaCallUI sends inData to the unsynced Lua state (LuaUI / widgets), invoking
// the widget callin RecvSkirmishAIMessage(aiTeam, inData). See LuaCallRules for
// the return semantics.
func (cb *Callback) LuaCallUI(inData string) (string, int) {
	return cb.callLua(CommandCallLuaUi, inData)
}

// callLua marshals an SCallLuaRulesCommand and dispatches it. SCallLuaUICommand
// has an identical layout, so the same struct serves both topics (the topic is
// what routes the message to the synced vs unsynced Lua state engine-side).
func (cb *Callback) callLua(topic CommandTopic, inData string) (string, int) {
	cin := C.CString(inData)
	defer C.free(unsafe.Pointer(cin))

	// The engine writes the (NUL-terminated) Lua response into ret_outData,
	// which must be MAX_RESPONSE_SIZE bytes. Allocate it in C memory so we are
	// not handing cgo a Go pointer for the engine to retain past the call.
	out := (*C.char)(C.malloc(C.size_t(C.MAX_RESPONSE_SIZE)))
	defer C.free(unsafe.Pointer(out))
	*out = 0 // guard the empty-response case

	var c C.struct_SCallLuaRulesCommand
	c.inData = cin
	c.inSize = C.int(len(inData)) // exact length, so embedded NULs survive
	c.ret_outData = out

	rc := cb.handleCommand(CommandToIDEngine, -1, topic, unsafe.Pointer(&c))
	return C.GoString(out), rc
}
