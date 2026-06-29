/* This file is part of the Spring engine (GPL v2 or later), see LICENSE.html */

package springai

import "fmt"

// AIColor represents an RGBA color.
//
// It is the Go equivalent of springai::AIColor (AI/Wrappers/Cpp/src/AIColor.h).
// Components are stored as bytes in the range [0, 255].
type AIColor struct {
	R uint8
	G uint8
	B uint8
	A uint8
}

// NullAIColor is opaque black, matching springai::AIColor::NULL_VALUE.
var NullAIColor = AIColor{R: 0, G: 0, B: 0, A: 255}

// NewAIColorBytes builds a color from byte components in the range [0, 255].
func NewAIColorBytes(r, g, b, a uint8) AIColor {
	return AIColor{R: r, G: g, B: b, A: a}
}

// NewAIColorFloat builds a color from float components in the range [0.0, 1.0].
func NewAIColorFloat(r, g, b, a float32) AIColor {
	return AIColor{
		R: floatToByte(r),
		G: floatToByte(g),
		B: floatToByte(b),
		A: floatToByte(a),
	}
}

// LoadInto3 writes R, G, B into a slice of length >= 3.
func (c AIColor) LoadInto3(rgb []int16) {
	_ = rgb[2]
	rgb[0] = int16(c.R)
	rgb[1] = int16(c.G)
	rgb[2] = int16(c.B)
}

// LoadInto4 writes R, G, B, A into a slice of length >= 4.
func (c AIColor) LoadInto4(rgba []int16) {
	_ = rgba[3]
	rgba[0] = int16(c.R)
	rgba[1] = int16(c.G)
	rgba[2] = int16(c.B)
	rgba[3] = int16(c.A)
}

// String implements fmt.Stringer, mirroring AIColor::ToString().
func (c AIColor) String() string {
	return fmt.Sprintf("rgba(%d, %d, %d, %d)", c.R, c.G, c.B, c.A)
}

func floatToByte(f float32) uint8 {
	if f <= 0 {
		return 0
	}
	if f >= 1 {
		return 255
	}
	return uint8(f*255 + 0.5)
}
