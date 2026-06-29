/* This file is part of the Spring engine (GPL v2 or later), see LICENSE.html */

// Command gen generates the bulk of the Go AI wrapper (the callback binding and
// the command senders) from the C interface headers, the same role the awk
// scripts play for the C++ wrapper (AI/Wrappers/Cpp/bin).
//
// It parses:
//   - SSkirmishAICallback.h -> callback_gen.go  (one cgo bridge + Go method per
//     function pointer; AI -> engine queries)
//   - AISCommands.h         -> command_gen.go   (CommandTopic constants, one Go
//     struct + Callback sender per S*Command; AI -> engine commands)
//
// Events (AISEvents.h) are hand-written in event.go because that surface is
// small and fully enumerated.
//
// Usage (see the //go:generate directive in doc.go):
//
//	go run ./bin/gen -out <pkgDir> -headers <interfaceDir>
//
// The generator is deliberately conservative: any function or command whose
// signature it does not recognise is SKIPPED and listed in a comment at the top
// of the output (and on stderr), so the emitted code always compiles. Closing
// those gaps is a matter of teaching the classifier new patterns here.
package main

import (
	"flag"
	"fmt"
	"go/format"
	"os"
	"path/filepath"
	"strings"
)

func main() {
	out := flag.String("out", ".", "directory to write *_gen.go into")
	headers := flag.String("headers", "../../../../../rts/ExternalAI/Interface", "directory containing the AIS*.h headers")
	flag.Parse()

	cbHeader := filepath.Join(*headers, "SSkirmishAICallback.h")
	cmdHeader := filepath.Join(*headers, "AISCommands.h")

	cbSrc := mustRead(cbHeader)
	cmdSrc := mustRead(cmdHeader)

	funcs := parseCallbacks(cbSrc)
	cmdTopics := parseCommandTopics(cmdSrc)
	cmds := parseCommands(cmdSrc)

	cbOut := filepath.Join(*out, "callback_gen.go")
	cmdOut := filepath.Join(*out, "command_gen.go")

	writeFile(cbOut, emitCallbacks(funcs))
	writeFile(cmdOut, emitCommands(cmdTopics, cmds))

	fmt.Fprintf(os.Stderr, "gen: wrote %s (%d methods) and %s (%d topics, %d senders)\n",
		cbOut, countEmitted(funcs), cmdOut, len(cmdTopics), countEmittedCmds(cmds))
}

// --- header reading -----------------------------------------------------

func mustRead(path string) string {
	b, err := os.ReadFile(path)
	if err != nil {
		fmt.Fprintf(os.Stderr, "gen: cannot read %s: %v\n", path, err)
		os.Exit(1)
	}
	return string(b)
}

func writeFile(path, content string) {
	out := []byte(content)
	if formatted, err := format.Source(out); err != nil {
		// Write the unformatted source anyway so the syntax error is inspectable.
		fmt.Fprintf(os.Stderr, "gen: warning: %s is not valid Go (%v); wrote unformatted\n", path, err)
	} else {
		out = formatted
	}
	if err := os.WriteFile(path, out, 0o644); err != nil {
		fmt.Fprintf(os.Stderr, "gen: cannot write %s: %v\n", path, err)
		os.Exit(1)
	}
}

// --- common parsing -----------------------------------------------------

type param struct {
	ctype string // e.g. "int", "const char*", "float*"
	name  string // e.g. "skirmishAIId"
}

// splitParams splits a C parameter list into typed params.
func splitParams(list string) []param {
	list = strings.TrimSpace(list)
	if list == "" || list == "void" {
		return nil
	}
	var ps []param
	for _, raw := range strings.Split(list, ",") {
		raw = strings.TrimSpace(raw)
		if raw == "" {
			continue
		}
		fields := strings.Fields(raw)
		name := fields[len(fields)-1]
		ctype := strings.Join(fields[:len(fields)-1], " ")
		// Move a leading '*' on the name onto the type (e.g. "int *x").
		for strings.HasPrefix(name, "*") {
			ctype += "*"
			name = strings.TrimPrefix(name, "*")
		}
		ps = append(ps, param{ctype: strings.TrimSpace(ctype), name: name})
	}
	return ps
}

func stars(ctype string) int { return strings.Count(ctype, "*") }

// scalarBase returns the underlying scalar of a pointer type, e.g. "float*" -> "float".
func scalarBase(ctype string) string {
	s := strings.ReplaceAll(ctype, "*", "")
	s = strings.ReplaceAll(s, "const", "")
	return strings.TrimSpace(s)
}

// --- naming -------------------------------------------------------------

// goName turns a C identifier like "Engine_Version_getMajor" into Go-exported
// "EngineVersionGetMajor".
func goName(c string) string {
	var b strings.Builder
	for _, seg := range strings.Split(c, "_") {
		if seg == "" {
			continue
		}
		b.WriteString(strings.ToUpper(seg[:1]) + seg[1:])
	}
	return b.String()
}

// goFieldName cleans a C struct field / param name into an exported Go name,
// stripping the marshaling suffixes the interface uses.
func goFieldName(c string) string {
	c = strings.TrimSuffix(c, "_posF3")
	c = strings.TrimSuffix(c, "_AposF3")
	c = strings.TrimSuffix(c, "_sizeMax")
	c = strings.TrimSuffix(c, "_size")
	c = strings.TrimPrefix(c, "return_")
	c = strings.TrimSuffix(c, "_out")
	parts := strings.Split(c, "_")
	var b strings.Builder
	for _, p := range parts {
		if p == "" {
			continue
		}
		b.WriteString(strings.ToUpper(p[:1]) + p[1:])
	}
	s := b.String()
	if s == "" {
		s = "Value"
	}
	return fixInitialisms(s)
}

// goParamName makes a safe lower-camel Go parameter name.
func goParamName(c string) string {
	c = strings.TrimSuffix(c, "_posF3")
	c = strings.TrimSuffix(c, "_AposF3")
	if c == "" {
		c = "v"
	}
	switch c { // avoid Go keywords
	case "type", "func", "range", "map", "chan", "select", "default":
		c += "_"
	}
	return c
}

// fixInitialisms tidies a few common suffixes (Id -> ID) for readability.
func fixInitialisms(s string) string {
	if strings.HasSuffix(s, "Id") {
		s = s[:len(s)-2] + "ID"
	}
	return strings.ReplaceAll(s, "IdF", "IDF")
}

// scalarGo maps a C scalar type to (goType, conversion-into-C expression
// template, conversion-from-C expression template). The %s placeholder is the
// value being converted.
func scalarGo(ctype string) (goType, toC, fromC string, ok bool) {
	switch strings.TrimSpace(strings.ReplaceAll(ctype, "const", "")) {
	case "int":
		return "int", "C.int(%s)", "int(%s)", true
	case "float":
		return "float32", "C.float(%s)", "float32(%s)", true
	case "short":
		return "int16", "C.short(%s)", "int16(%s)", true
	case "char":
		return "byte", "C.char(%s)", "byte(%s)", true
	case "bool":
		// bools are funnelled through int in the bridge to avoid cgo _Bool
		// edge cases; see emit.
		return "bool", "b2i(%s)", "(%s != 0)", true
	default:
		return "", "", "", false
	}
}

// maxStringLength is the scratch buffer size used for char* out-params.
const maxStringLength = 8192
