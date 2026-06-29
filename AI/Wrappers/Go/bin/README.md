# Go wrapper code generation

[`gen/`](gen) holds the generator that produces the bulk of the Go wrapper from
the C interface headers — the Go analogue of the awk scripts in
[`AI/Wrappers/Cpp/bin`](../../Cpp/bin).

## Run it

```sh
# from the wrapper root (AI/Wrappers/Go)
go generate ./...
# or directly:
go run ./bin/gen -out ./src/springai -headers ../../../rts/ExternalAI/Interface
```

CMake also runs it automatically as a `generateSources` step (see
[`../CMakeLists.txt`](../CMakeLists.txt)) before any Go Skirmish AI is built.

## Inputs

- [`SSkirmishAICallback.h`](../../../../rts/ExternalAI/Interface/SSkirmishAICallback.h)
  — function-pointer surface (AI → engine queries).
- [`AISCommands.h`](../../../../rts/ExternalAI/Interface/AISCommands.h)
  — command topics and `S*Command` structs.

Events ([`AISEvents.h`](../../../../rts/ExternalAI/Interface/AISEvents.h)) are
hand-written in `src/springai/event.go`; that surface is small and closed.

## Outputs (into `src/springai/`, gitignored)

- `callback_gen.go` — for each `SSkirmishAICallback` function pointer: a `static`
  cgo bridge trampoline (like the C++ wrapper's `CombinedCallbackBridge.c`) plus
  an idiomatic `Callback` method. Handles scalar returns, `const char*` strings,
  `char*`+`_sizeMax` out-buffers, `T*`+`_sizeMax` arrays (two-pass count query),
  `*_posF3` positions (in and out), `*_AposF3` position arrays,
  `return_colorS3_out` colors, `const char**` string arrays and `void*` opaques.
- `command_gen.go` — the `CommandTopic` constants, one Go struct per `S*Command`
  and a `Callback` sender that marshals it and calls `handleCommand`.

## Design

The generator (`main.go` + `callbacks.go` + `commands.go`) parses the regular
header layout with a few regexes, classifies each parameter / struct field, and
emits Go (formatted via `go/format`). It is **conservative**: any signature it
does not recognise is skipped and listed in a comment block at the top of the
generated file, so the output always compiles. The current skips are documented
in [`../README.md`](../README.md#code-generation).

To extend coverage, add a case to the classifier in `callbacks.go`
(`classifyFunc`) or `commands.go` (`commandGo`).
