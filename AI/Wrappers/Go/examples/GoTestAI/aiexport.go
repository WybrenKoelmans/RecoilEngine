/* This file is part of the Spring engine (GPL v2 or later), see LICENSE.html */

package main

/*
#cgo CFLAGS: -I${SRCDIR}/../../../../../rts -I${SRCDIR}/../../../../../rts/ExternalAI/Interface

#include <stddef.h>
*/
import "C"

import (
	"unsafe"

	springai "github.com/beyond-all-reason/RecoilEngine/AI/Wrappers/Go/src/springai"
)

// These are the Go sides of the C entry points the engine requires. They cannot
// be named init/release/handleEvent directly: `init` is reserved by Go and
// //export uses the Go function name as the C symbol. Instead, the real C
// symbols are defined in shim.c and forward to these.
//
// See struct SSkirmishAILibrary in
// rts/ExternalAI/Interface/SSkirmishAILibrary.h for the required signatures.

//export goInit
func goInit(skirmishAIID C.int, callback unsafe.Pointer) C.int {
	return C.int(springai.Init(int(skirmishAIID), callback))
}

//export goRelease
func goRelease(skirmishAIID C.int) C.int {
	return C.int(springai.Release(int(skirmishAIID)))
}

//export goHandleEvent
func goHandleEvent(skirmishAIID C.int, topic C.int, data unsafe.Pointer) C.int {
	return C.int(springai.HandleEvent(int(skirmishAIID), int(topic), data))
}
