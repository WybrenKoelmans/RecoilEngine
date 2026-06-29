# GOdless

A minimal Skirmish AI written in Go, built on the Go wrapper
([`AI/Wrappers/Go`](../../Wrappers/Go)).

Its entire behaviour: when it is initialised, it writes a single line to the
engine log to prove it loaded and ran. That's the whole AI — it exists to show
the Go wrapper loads and runs inside the engine.

## Layout

```
GOdless/
├── CMakeLists.txt   calls configure_go_skirmish_ai (from the Go wrapper)
├── go.mod           own module; replace -> ../../Wrappers/Go
├── godless.go       the AI: logs a line on init to prove it loaded
├── aiexport.go      //export goInit/goRelease/goHandleEvent
├── shim.c           C entry points init/release/handleEvent -> the Go exports
├── VERSION          AI version
└── data/            AIInfo.lua, AIOptions.lua
```

## Building

It builds with the rest of the engine via CMake (the Go wrapper's
`configure_go_skirmish_ai` macro runs `go build -buildmode=c-shared`), provided a
Go toolchain is on `PATH`.

To build just this AI by hand:

```sh
go build -buildmode=c-shared -o data/SkirmishAI.so .
```

The resulting `SkirmishAI.so` exports `init`/`release`/`handleEvent` and is
loaded by the native **C** AI Interface, exactly like a C++ Skirmish AI.
