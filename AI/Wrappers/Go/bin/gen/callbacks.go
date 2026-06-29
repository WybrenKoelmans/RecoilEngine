/* This file is part of the Spring engine (GPL v2 or later), see LICENSE.html */

package main

import (
	"fmt"
	"regexp"
	"sort"
	"strings"
)

type cbFunc struct {
	cName   string
	retType string
	params  []param

	skip   string   // reason, if unhandled
	inputs []string // Go parameter declarations, e.g. "unitID int"
	preps  []string // statements emitted before the bridge call
	slots  []string // bridge args, len==len(params); may contain @PTR@/@SIZE@
	out    *cbOut
}

type cbOut struct {
	kind     string // "scalar", "void", "slice", "posarr", "strarr", "strbuf", "pos", "color"
	goType   string // Go return type ("" for void)
	elemC    string // C element type for slices ("int"/"float"/"short")
	elemGo   string // Go element type
	elemConv string // conversion template from C element to Go element
}

var cbLineRE = regexp.MustCompile(`(?m)^\s*([A-Za-z_][\w \*]*?)\s*\(CALLING_CONV\s*\*(\w+)\)\s*\(([^;]*)\)\s*;`)

func parseCallbacks(src string) []cbFunc {
	var funcs []cbFunc
	for _, m := range cbLineRE.FindAllStringSubmatch(src, -1) {
		f := cbFunc{
			retType: strings.TrimSpace(m[1]),
			cName:   m[2],
			params:  splitParams(m[3]),
		}
		classifyFunc(&f)
		funcs = append(funcs, f)
	}
	sort.Slice(funcs, func(i, j int) bool { return funcs[i].cName < funcs[j].cName })
	return funcs
}

func classifyFunc(f *cbFunc) {
	if len(f.params) == 0 || f.params[0].name != "skirmishAIId" {
		f.skip = "first parameter is not skirmishAIId"
		return
	}
	f.slots = make([]string, len(f.params))
	f.slots[0] = "C.int(cb.skirmishAIID)"

	i := 1
	for i < len(f.params) {
		p := f.params[i]
		hasSize := i+1 < len(f.params) && f.params[i+1].name == p.name+"_sizeMax"

		if stars(p.ctype) == 0 {
			// scalar input
			goType, toC, _, ok := scalarGo(p.ctype)
			if !ok {
				f.skip = "unhandled scalar param type " + p.ctype
				return
			}
			gn := goParamName(p.name)
			f.inputs = append(f.inputs, gn+" "+goType)
			f.slots[i] = fmt.Sprintf(toC, gn)
			i++
			continue
		}

		// pointer param
		isChar := strings.Contains(p.ctype, "char")
		switch {
		case strings.Contains(p.ctype, "void"):
			gn := goParamName(p.name)
			f.inputs = append(f.inputs, gn+" unsafe.Pointer")
			f.slots[i] = gn
			i++

		case isChar && stars(p.ctype) == 1 && strings.Contains(p.ctype, "const"):
			// const char* -> input string
			gn := goParamName(p.name)
			f.inputs = append(f.inputs, gn+" string")
			cv := "c_" + gn
			f.preps = append(f.preps,
				cv+" := C.CString("+gn+")",
				"defer C.free(unsafe.Pointer("+cv+"))")
			f.slots[i] = cv
			i++

		case isChar && stars(p.ctype) == 1 && hasSize:
			// char* + sizeMax -> output string buffer
			if !f.setOut(&cbOut{kind: "strbuf", goType: "string"}) {
				return
			}
			f.slots[i] = "@PTR@"
			f.slots[i+1] = "@SIZE@"
			i += 2

		case isChar && stars(p.ctype) == 2 && hasSize:
			// const char** + sizeMax -> output []string
			if !f.setOut(&cbOut{kind: "strarr", goType: "[]string"}) {
				return
			}
			f.slots[i] = "@PTR@"
			f.slots[i+1] = "@SIZE@"
			i += 2

		case !isChar && stars(p.ctype) == 1 && hasSize:
			base := scalarBase(p.ctype)
			if strings.Contains(p.name, "AposF3") {
				if !f.setOut(&cbOut{kind: "posarr", goType: "[]AIFloat3", elemC: "float"}) {
					return
				}
			} else {
				elemGo, _, fromC, ok := scalarGo(base)
				if !ok {
					f.skip = "unhandled array element type " + base
					return
				}
				if !f.setOut(&cbOut{kind: "slice", goType: "[]" + elemGo, elemC: base, elemGo: elemGo, elemConv: fromC}) {
					return
				}
			}
			f.slots[i] = "@PTR@"
			f.slots[i+1] = "@SIZE@"
			i += 2

		case !isChar && stars(p.ctype) == 1 && scalarBase(p.ctype) == "float" && strings.Contains(p.name, "posF3"):
			if strings.HasPrefix(p.name, "return_") {
				if !f.setOut(&cbOut{kind: "pos", goType: "AIFloat3"}) {
					return
				}
				f.slots[i] = "@PTR@"
			} else {
				gn := goParamName(p.name)
				f.inputs = append(f.inputs, gn+" AIFloat3")
				cv := "c_" + gn
				f.preps = append(f.preps,
					cv+" := [3]C.float{C.float("+gn+".X), C.float("+gn+".Y), C.float("+gn+".Z)}")
				f.slots[i] = "&" + cv + "[0]"
			}
			i++

		case !isChar && stars(p.ctype) == 1 && scalarBase(p.ctype) == "short" && strings.HasPrefix(p.name, "return_") && strings.Contains(p.name, "colorS3"):
			if !f.setOut(&cbOut{kind: "color", goType: "AIColor"}) {
				return
			}
			f.slots[i] = "@PTR@"
			i++

		default:
			f.skip = "unhandled pointer param " + p.ctype + " " + p.name
			return
		}
	}

	if f.out == nil {
		// pure-scalar return
		if f.retType == "void" {
			f.out = &cbOut{kind: "void"}
		} else {
			f.out = &cbOut{kind: "scalar", goType: returnGoType(f.retType)}
		}
	}
}

func (f *cbFunc) setOut(o *cbOut) bool {
	if f.out != nil {
		f.skip = "more than one output parameter"
		return false
	}
	f.out = o
	return true
}

// returnGoType maps a C return type to its Go type for scalar-returning funcs.
func returnGoType(ctype string) string {
	switch strings.TrimSpace(strings.ReplaceAll(ctype, "const", "")) {
	case "int":
		return "int"
	case "float":
		return "float32"
	case "bool":
		return "bool"
	case "short":
		return "int16"
	case "char":
		return "byte"
	case "char*":
		return "string"
	default:
		return "string" // const char* and the like
	}
}

// returnFromC renders the conversion of a bridge return value (named expr) to Go.
func returnFromC(ctype, expr string) string {
	switch strings.TrimSpace(strings.ReplaceAll(ctype, "const", "")) {
	case "int":
		return "int(" + expr + ")"
	case "float":
		return "float32(" + expr + ")"
	case "bool":
		return "(" + expr + " != 0)"
	case "short":
		return "int16(" + expr + ")"
	case "char":
		return "byte(" + expr + ")"
	default: // char* / const char*
		return "C.GoString(" + expr + ")"
	}
}

// bridgeRetType is the C return type of the trampoline (bool funnelled to int).
func bridgeRetType(ctype string) string {
	if strings.TrimSpace(strings.ReplaceAll(ctype, "const", "")) == "bool" {
		return "int"
	}
	return ctype
}

// emitCallbacks renders callback_gen.go.
func emitCallbacks(funcs []cbFunc) string {
	var b strings.Builder
	var skipped []string

	b.WriteString(genHeader)
	b.WriteString("package springai\n\n")
	b.WriteString("/*\n")
	b.WriteString("#cgo CFLAGS: -I${SRCDIR}/../../../../../rts -I${SRCDIR}/../../../../../rts/ExternalAI/Interface\n\n")
	b.WriteString("#include <stdlib.h>\n#include <stdbool.h>\n\n#include \"SSkirmishAICallback.h\"\n\n")
	// bridge trampolines
	for _, f := range funcs {
		if f.skip != "" {
			continue
		}
		b.WriteString(bridgeC(f))
	}
	b.WriteString("*/\nimport \"C\"\n\n")
	b.WriteString("import \"unsafe\"\n\n")
	b.WriteString("// b2i funnels a Go bool through C int for bridge calls.\n")
	b.WriteString("func b2i(v bool) C.int {\n\tif v {\n\t\treturn 1\n\t}\n\treturn 0\n}\n\n")

	for _, f := range funcs {
		if f.skip != "" {
			skipped = append(skipped, f.cName+" ("+f.skip+")")
			continue
		}
		b.WriteString(methodGo(f))
	}

	if len(skipped) > 0 {
		b.WriteString("\n// The following callbacks were not generated and need hand-wiring:\n")
		for _, s := range skipped {
			b.WriteString("//   - " + s + "\n")
		}
	}
	return b.String()
}

func bridgeC(f cbFunc) string {
	var decl []string
	var call []string
	for _, p := range f.params {
		ct := p.ctype
		if strings.TrimSpace(strings.ReplaceAll(ct, "const", "")) == "bool" {
			ct = "int"
		}
		decl = append(decl, ct+" "+p.name)
		call = append(call, p.name)
	}
	ret := bridgeRetType(f.retType)
	sig := fmt.Sprintf("static %s bridge_%s(struct SSkirmishAICallback* c, %s) {",
		ret, f.cName, strings.Join(decl, ", "))
	inner := fmt.Sprintf("c->%s(%s)", f.cName, strings.Join(call, ", "))
	if f.retType == "void" {
		return sig + "\n\t" + inner + ";\n}\n"
	}
	return sig + "\n\treturn " + inner + ";\n}\n"
}

func methodGo(f cbFunc) string {
	name := goName(f.cName)
	args := func(pass int) string {
		ss := make([]string, len(f.slots))
		copy(ss, f.slots)
		for i, s := range ss {
			switch s {
			case "@PTR@":
				if pass == 1 {
					ss[i] = "nil"
				} else {
					ss[i] = "@PTRVAL@"
				}
			case "@SIZE@":
				if pass == 1 {
					// Count query: pass a huge sizeMax with a nil buffer. The
					// engine counts matching entries without writing (it guards
					// every store with `if (ptr != nullptr)` and bounds the loop
					// by sizeMax). Passing 0 here makes getters that break at
					// `a >= sizeMax` return 0 — e.g. getTeamUnits. This mirrors
					// the C++ wrapper, which uses INT_MAX.
					ss[i] = "0x7fffffff"
				} else {
					ss[i] = "@SIZEVAL@"
				}
			}
		}
		return "cb.c, " + strings.Join(ss, ", ")
	}

	var body strings.Builder
	for _, p := range f.preps {
		body.WriteString("\t" + p + "\n")
	}

	o := f.out
	switch o.kind {
	case "void":
		body.WriteString("\tC.bridge_" + f.cName + "(" + args(1) + ")\n")
		return fmt.Sprintf("// %s wraps %s.\nfunc (cb *Callback) %s(%s) {\n%s}\n\n",
			name, f.cName, name, strings.Join(f.inputs, ", "), body.String())

	case "scalar":
		expr := "C.bridge_" + f.cName + "(" + args(1) + ")"
		body.WriteString("\treturn " + returnFromC(f.retType, expr) + "\n")

	case "strbuf":
		call := strings.NewReplacer("@PTRVAL@", "&buf[0]", "@SIZEVAL@", fmt.Sprintf("C.int(%d)", maxStringLength)).Replace(args(2))
		body.WriteString(fmt.Sprintf("\tbuf := make([]C.char, %d)\n", maxStringLength))
		body.WriteString("\tC.bridge_" + f.cName + "(" + call + ")\n")
		body.WriteString("\treturn C.GoString(&buf[0])\n")

	case "pos":
		call := strings.NewReplacer("@PTRVAL@", "&ret[0]").Replace(args(2))
		body.WriteString("\tvar ret [3]C.float\n")
		body.WriteString("\tC.bridge_" + f.cName + "(" + call + ")\n")
		body.WriteString("\treturn AIFloat3{X: float32(ret[0]), Y: float32(ret[1]), Z: float32(ret[2])}\n")

	case "color":
		call := strings.NewReplacer("@PTRVAL@", "&ret[0]").Replace(args(2))
		body.WriteString("\tvar ret [3]C.short\n")
		body.WriteString("\tC.bridge_" + f.cName + "(" + call + ")\n")
		body.WriteString("\treturn AIColor{R: uint8(ret[0]), G: uint8(ret[1]), B: uint8(ret[2]), A: 255}\n")

	case "slice":
		p2 := strings.NewReplacer("@PTRVAL@", "(*C."+o.elemC+")(&buf[0])", "@SIZEVAL@", "C.int(n)").Replace(args(2))
		body.WriteString("\tn := int(C.bridge_" + f.cName + "(" + args(1) + "))\n")
		body.WriteString("\tif n <= 0 {\n\t\treturn nil\n\t}\n")
		body.WriteString("\tbuf := make([]C." + o.elemC + ", n)\n")
		body.WriteString("\tC.bridge_" + f.cName + "(" + p2 + ")\n")
		body.WriteString("\tout := make([]" + o.elemGo + ", n)\n")
		body.WriteString("\tfor i, v := range buf {\n\t\tout[i] = " + fmt.Sprintf(o.elemConv, "v") + "\n\t}\n")
		body.WriteString("\treturn out\n")

	case "posarr":
		p2 := strings.NewReplacer("@PTRVAL@", "(*C.float)(&buf[0])", "@SIZEVAL@", "C.int(n)").Replace(args(2))
		body.WriteString("\tn := int(C.bridge_" + f.cName + "(" + args(1) + "))\n")
		body.WriteString("\tif n <= 0 {\n\t\treturn nil\n\t}\n")
		body.WriteString("\tbuf := make([]C.float, n)\n")
		body.WriteString("\tC.bridge_" + f.cName + "(" + p2 + ")\n")
		body.WriteString("\tout := make([]AIFloat3, n/3)\n")
		body.WriteString("\tfor i := range out {\n\t\tout[i] = AIFloat3{X: float32(buf[i*3]), Y: float32(buf[i*3+1]), Z: float32(buf[i*3+2])}\n\t}\n")
		body.WriteString("\treturn out\n")

	case "strarr":
		p2 := strings.NewReplacer("@PTRVAL@", "(**C.char)(&buf[0])", "@SIZEVAL@", "C.int(n)").Replace(args(2))
		body.WriteString("\tn := int(C.bridge_" + f.cName + "(" + args(1) + "))\n")
		body.WriteString("\tif n <= 0 {\n\t\treturn nil\n\t}\n")
		body.WriteString("\tbuf := make([]*C.char, n)\n")
		body.WriteString("\tC.bridge_" + f.cName + "(" + p2 + ")\n")
		body.WriteString("\tout := make([]string, n)\n")
		body.WriteString("\tfor i, v := range buf {\n\t\tout[i] = C.GoString(v)\n\t}\n")
		body.WriteString("\treturn out\n")
	}

	return fmt.Sprintf("// %s wraps %s.\nfunc (cb *Callback) %s(%s) %s {\n%s}\n\n",
		name, f.cName, name, strings.Join(f.inputs, ", "), o.goType, body.String())
}

func countEmitted(funcs []cbFunc) int {
	n := 0
	for _, f := range funcs {
		if f.skip == "" {
			n++
		}
	}
	return n
}
