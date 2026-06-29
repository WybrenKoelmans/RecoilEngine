/* This file is part of the Spring engine (GPL v2 or later), see LICENSE.html */

// Package springai is the Go wrapper for the Spring/Recoil Skirmish AI
// interface.
//
// It is the Go counterpart of the C++ wrapper found in AI/Wrappers/Cpp.
// A Go Skirmish AI is compiled with cgo into a C-shared library
// (`go build -buildmode=c-shared`) that exports the three C entry points the
// native (C) AI Interface expects:
//
//	init(skirmishAIId int, callback *SSkirmishAICallback) int
//	release(skirmishAIId int) int
//	handleEvent(skirmishAIId int, topic int, data unsafe.Pointer) int
//
// (see struct SSkirmishAILibrary in
// rts/ExternalAI/Interface/SSkirmishAILibrary.h).
//
// This package provides the machinery that those thin exported functions
// delegate to:
//
//   - Fixed, hand-written value types: AIFloat3, AIColor (this package),
//     mirroring AI/Wrappers/Cpp/src/AIFloat3.* and AIColor.*.
//   - Event decoding: the EventTopic constants and one Go struct per engine
//     event, decoded from the opaque `void* data` pointer (see event.go),
//     mirroring rts/ExternalAI/Interface/AISEvents.h.
//   - Command topics and command structs sent back to the engine through the
//     callback's Engine_handleCommand bridge (see command.go), mirroring
//     rts/ExternalAI/Interface/AISCommands.h.
//   - The Callback type: an OO wrapper over the C SSkirmishAICallback struct
//     of function pointers (see callback.go), mirroring the generated
//     WrappOOAICallback / OOAICallback classes of the C++ wrapper.
//   - A small runtime that registers an AI factory and dispatches the C entry
//     points to a user-implemented AI (see ai.go), mirroring the role of
//     AIExport.cpp in a C++ Skirmish AI.
//
// The callback query methods (callback_gen.go) and the command topics/structs/
// senders (command_gen.go) are generated from the C interface headers by
// bin/gen. Regenerate them with `go generate ./...` from the wrapper root, or
// directly via the directive below.
//
// See README.md in this directory for the mapping between the C++ wrapper
// artifacts and their Go equivalents.
//
//go:generate go run ../../bin/gen -out . -headers ../../../../../rts/ExternalAI/Interface
package springai
