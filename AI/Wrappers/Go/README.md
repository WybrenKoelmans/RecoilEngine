# Go AI Wrapper

This is the **Go wrapper** for the Spring/Recoil Skirmish AI interface, the Go
counterpart of the C++ wrapper in [`AI/Wrappers/Cpp`](../Cpp). It lets a
Skirmish AI be written in Go.

## How it fits together

A Skirmish AI is a native shared library that the engine's **C AI Interface**
([`AI/Interfaces/C`](../../Interfaces/C)) loads and resolves three symbols from
via `dlsym`/`GetProcAddress` (see
[`rts/ExternalAI/Interface/SSkirmishAILibrary.h`](../../../rts/ExternalAI/Interface/SSkirmishAILibrary.h)):

```c
int init(int skirmishAIId, const struct SSkirmishAICallback* callback);
int release(int skirmishAIId);
int handleEvent(int skirmishAIId, int topic, const void* data);   // required
```

Go can produce exactly such a library with `go build -buildmode=c-shared` (cgo).
So a **Go Skirmish AI is just a native AI** as far as the engine is concerned;
it uses the existing **C** AI Interface — no new AI Interface is required, only
this wrapper plus the Go toolchain.

```
engine ──► C AI Interface ──► SkirmishAI.so (Go, c-shared)
                                 │  init/release/handleEvent  (shim.c)
                                 ▼
                              springai (this wrapper)
                                 │  decode events, wrap callback
                                 ▼
                              your AI (implements springai.AI)
```

## What maps to what (C++ wrapper → Go wrapper)

| C++ wrapper artifact | Go wrapper equivalent | Status |
|---|---|---|
| `src/AIFloat3.{h,cpp}` | [`src/springai/aifloat3.go`](src/springai/aifloat3.go) | ✅ hand-written |
| `src/AIColor.{h,cpp}` | [`src/springai/aicolor.go`](src/springai/aicolor.go) | ✅ hand-written |
| `src/AIEvent.h` + per-event handling | [`src/springai/event.go`](src/springai/event.go) | ✅ hand-written (all 28 events decoded) |
| awk generators in `bin/` | [`bin/gen`](bin/gen) (Go generator) | ✅ implemented |
| `enum CommandTopic` + `S*Command` structs | `src/springai/command_gen.go` | ✅ generated (96 topics, 76 senders) |
| generated `CombinedCallbackBridge.c` | cgo bridge trampolines in `callback_gen.go` | ✅ generated |
| generated `OOAICallback` / `WrappOOAICallback` | `Callback` methods in `callback_gen.go` | ✅ generated (587 methods) |
| `Callback` core / dispatch | [`src/springai/callback.go`](src/springai/callback.go), [`command.go`](src/springai/command.go) | ✅ hand-written core |
| `AIExport.cpp` (per-AI glue) | [`src/springai/ai.go`](src/springai/ai.go) runtime + the AI's `shim.c`/`aiexport.go` | ✅ done (in wrapper + example) |

`init`/`release`/`handleEvent` cannot be Go `//export` names directly (`init`
is reserved by Go), so the example AI defines them in a tiny C shim
([`examples/GoTestAI/shim.c`](examples/GoTestAI/shim.c)) that forwards to
`//export`ed Go functions.

## Code generation

`callback_gen.go` and `command_gen.go` are produced by the generator in
[`bin/gen`](bin/gen) from the C interface headers — the Go analogue of the awk
scripts in [`AI/Wrappers/Cpp/bin`](../Cpp/bin). Regenerate after an interface
change:

```sh
go generate ./...        # from the wrapper root (or let CMake do it)
```

The generated files are **not committed** (see `.gitignore`); CMake runs the
generator as a `generateSources` step before any Go AI is built.

The generator is deliberately conservative: signatures it does not recognise are
skipped and listed in a comment at the top of the generated file. Current gaps:

- **3 callbacks** with parallel `const char** keys, const char** values`
  map-style out-params (`*Def_getCustomParams`).
- **14 commands** carrying engine-written `ret_*` output fields (e.g.
  `SCreateGroupCommand.ret_groupId`, `SCallLuaRulesCommand.ret_outData`). These
  need a sender variant that returns the output; the topic constants for them
  are still generated, so they can be sent via the low-level
  `Callback.handleCommand` in the meantime.

Closing a gap means teaching the classifier a new pattern in
[`bin/gen/callbacks.go`](bin/gen/callbacks.go) /
[`bin/gen/commands.go`](bin/gen/commands.go).

Events (engine → AI) are hand-written rather than generated because that surface
is small and fully enumerated in `AISEvents.h`.

## Building

The wrapper itself is Go source; nothing is compiled until a Go AI is built.
CMake auto-detects it (gated on the `go` toolchain being on `PATH`) and exposes:

- `BUILD_Go_AIWRAPPER`
- `Go_AIWRAPPER_SRC_DIR`, `Go_AIWRAPPER_MODULE`
- the `configure_go_skirmish_ai(<srcDirRelVar>)` CMake macro for Go AIs

A Go Skirmish AI's `CMakeLists.txt` then looks like:

```cmake
set(mySourceDirRel "")   # the dir containing the Go main package + shim.c
configure_go_skirmish_ai(mySourceDirRel)
```

To build the example AI by hand (from `examples/GoTestAI`):

```sh
go generate ../../src/springai   # produce *_gen.go (once / after header changes)
go build -buildmode=c-shared -o data/SkirmishAI.so .
```

> Verified with Go 1.26 + gcc: the generator emits 587 callback methods, 96
> command topics and 76 senders; the `springai` package and the `GoTestAI`
> example both compile, and the resulting `SkirmishAI.so` exports the required
> `init`/`release`/`handleEvent` symbols.

## Layout

```
Go/
├── CMakeLists.txt          auto-discovered; toolchain detection, codegen + build macro
├── go.mod                  module github.com/.../AI/Wrappers/Go
├── README.md               this file
├── bin/gen/                the code generator (parses headers -> *_gen.go)
│   ├── main.go             entry point + shared parsing/naming helpers
│   ├── callbacks.go        SSkirmishAICallback.h -> callback_gen.go
│   └── commands.go         AISCommands.h        -> command_gen.go
├── src/springai/           the wrapper package
│   ├── doc.go              package doc + //go:generate directive
│   ├── aifloat3.go         AIFloat3 (fixed type, hand-written)
│   ├── aicolor.go          AIColor (fixed type, hand-written)
│   ├── event.go            EventTopic + all event structs + DecodeEvent (hand, cgo)
│   ├── command.go          CommandTopic core types/consts (hand)
│   ├── callback.go         Callback type + NewCallback + handleCommand (hand, cgo)
│   ├── ai.go               AI interface + Init/Release/HandleEvent runtime (hand)
│   ├── callback_gen.go     587 query methods + bridges        (generated, gitignored)
│   └── command_gen.go      96 topics + 76 command senders      (generated, gitignored)
└── examples/GoTestAI/      a minimal Go Skirmish AI (analogue of CppTestAI)
    ├── gotestai.go         AI logic + factory registration
    ├── aiexport.go         //export goInit/goRelease/goHandleEvent
    ├── shim.c              C entry points init/release/handleEvent
    └── data/               AIInfo.lua, AIOptions.lua
```
