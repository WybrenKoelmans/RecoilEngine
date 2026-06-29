/* This file is part of the Spring engine (GPL v2 or later), see LICENSE.html */

package main

/*
#cgo CFLAGS: -I${SRCDIR}/../../../rts -I${SRCDIR}/../../../rts/ExternalAI/Interface

#include <stddef.h>
*/
import "C"

import (
	"unsafe"

	springai "github.com/beyond-all-reason/RecoilEngine/AI/Wrappers/Go/src/springai"
)

// Go sides of the C entry points (the real init/release/handleEvent symbols are
// in shim.c, which forwards here; `init` is reserved by Go so it cannot be a
// //export name). See struct SSkirmishAILibrary in
// rts/ExternalAI/Interface/SSkirmishAILibrary.h.

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
