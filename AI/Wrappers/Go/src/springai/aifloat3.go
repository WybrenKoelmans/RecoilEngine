/* This file is part of the Spring engine (GPL v2 or later), see LICENSE.html */

package springai

import "fmt"

// AIFloat3 represents a position or direction on the map.
//
// It is the Go equivalent of springai::AIFloat3 (AI/Wrappers/Cpp/src/AIFloat3.h).
// The C interface passes 3D vectors as `float*` pointing at three consecutive
// 32-bit floats (often named *_posF3 / dir_posF3 in the headers); this type is
// the idiomatic Go value used to carry them across the boundary. Conversion
// from the raw C pointer lives in the cgo files (see aiFloat3FromC in event.go).
type AIFloat3 struct {
	X float32
	Y float32
	Z float32
}

// NullAIFloat3 is the zero vector, matching springai::AIFloat3::NULL_VALUE.
var NullAIFloat3 = AIFloat3{}

// LoadInto writes the three components into a slice of length >= 3, matching
// the C interface convention where the AI supplies a `float*` buffer for the
// engine (or a bridge helper) to read.
func (f AIFloat3) LoadInto(xyz []float32) {
	_ = xyz[2] // bounds check hint
	xyz[0] = f.X
	xyz[1] = f.Y
	xyz[2] = f.Z
}

// String implements fmt.Stringer, mirroring AIFloat3::ToString().
func (f AIFloat3) String() string {
	return fmt.Sprintf("(%g, %g, %g)", f.X, f.Y, f.Z)
}
