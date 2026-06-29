/* This file is part of the Spring engine (GPL v2 or later), see LICENSE.html */

package springai

import (
	"sync"
	"unsafe"
)

// AI is the interface a Go Skirmish AI implements. It is the rough analogue of
// the user-written C++ AI class (e.g. cpptestai::CCppTestAI): the wrapper owns
// the C entry points and the event decoding, and forwards each decoded event
// here.
//
// HandleEvent returns 0 on success and non-zero to signal an error to the
// engine, matching the convention of SSkirmishAILibrary.handleEvent.
type AI interface {
	HandleEvent(ev Event) int
}

// Releaser is an optional interface an AI may implement to clean up resources
// (e.g. stop background goroutines) when the engine tears it down. It is the
// Go analogue of the C++ AI's destructor invoked from AIExport.cpp's release().
// Release is called from the C release() entry point exactly once per instance.
type Releaser interface {
	Release()
}

// Factory builds a new AI instance for a given skirmish AI id and its callback.
// It is registered once via RegisterFactory, typically from the Skirmish AI's
// package init() function.
type Factory func(skirmishAIID int, cb *Callback) AI

var (
	mu        sync.Mutex
	factory   Factory
	instances = map[int]AI{}
)

// RegisterFactory installs the constructor used to create AI instances. A
// Skirmish AI calls this exactly once (usually from init()).
func RegisterFactory(f Factory) {
	mu.Lock()
	defer mu.Unlock()
	factory = f
}

// The Init/Release/HandleEvent functions below are the bridge between the C
// entry points a Skirmish AI exports (init/release/handleEvent, via cgo
// //export in its main package) and the registered AI. They replace the
// hand-written bookkeeping in AIExport.cpp.

// Init creates and registers an AI instance for skirmishAIID. rawCallback is
// the SSkirmishAICallback* the engine passed to the C init() entry point.
// Returns 0 on success.
func Init(skirmishAIID int, rawCallback unsafe.Pointer) int {
	mu.Lock()
	f := factory
	mu.Unlock()
	if f == nil {
		return errNoFactory
	}

	cb := NewCallback(rawCallback, skirmishAIID)
	ai := f(skirmishAIID, cb)
	if ai == nil {
		return errNilAI
	}

	mu.Lock()
	instances[skirmishAIID] = ai
	mu.Unlock()
	return 0
}

// Release tears down the AI instance for skirmishAIID. Returns 0 on success.
func Release(skirmishAIID int) int {
	mu.Lock()
	ai := instances[skirmishAIID]
	delete(instances, skirmishAIID)
	mu.Unlock()

	if r, ok := ai.(Releaser); ok {
		r.Release()
	}
	return 0
}

// HandleEvent decodes the engine event and forwards it to the AI instance.
// data is the opaque event pointer from the C handleEvent entry point.
// Unknown/dataless topics are accepted and reported as success.
func HandleEvent(skirmishAIID int, topic int, data unsafe.Pointer) int {
	mu.Lock()
	ai := instances[skirmishAIID]
	mu.Unlock()
	if ai == nil {
		return errNoInstance
	}

	ev, ok := DecodeEvent(EventTopic(topic), data)
	if !ok {
		// Topic carries no decodable payload (or is unknown to this wrapper
		// version); nothing to forward, treat as handled.
		return 0
	}
	return ai.HandleEvent(ev)
}

// Error codes returned across the C boundary. Kept distinct and non-zero so a
// failing entry point is diagnosable engine-side.
const (
	errNoFactory  = 101
	errNilAI      = 102
	errNoInstance = 103
)
