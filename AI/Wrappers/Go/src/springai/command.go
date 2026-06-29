/* This file is part of the Spring engine (GPL v2 or later), see LICENSE.html */

package springai

// CommandTopic identifies a command sent from the AI to the engine through
// Callback.handleCommand. The concrete topic constants (Command*), the command
// structs (one per S*Command) and their Callback senders are GENERATED into
// command_gen.go by bin/gen, from rts/ExternalAI/Interface/AISCommands.h.
//
// The values there mirror enum CommandTopic and MUST NOT be renumbered (the
// header is ABI-parsed by external systems).
type CommandTopic int

// CommandToIDEngine is the toId value addressing a command to the engine
// itself, matching COMMAND_TO_ID_ENGINE in AISCommands.h.
const CommandToIDEngine = -1

// UnitCommandBuildNoFacing matches UNIT_COMMAND_BUILD_NO_FACING.
const UnitCommandBuildNoFacing = -1

// NumCmdTopics mirrors NUM_CMD_TOPICS in AISCommands.h.
const NumCmdTopics = 97

// UnitCommandOption bits, mirroring enum UnitCommandOptions. Combine into the
// Options field of unit commands.
type UnitCommandOption int16

const (
	UnitCommandOptionInternalOrder UnitCommandOption = 1 << 3
	UnitCommandOptionRightMouseKey UnitCommandOption = 1 << 4
	UnitCommandOptionShiftKey      UnitCommandOption = 1 << 5
	UnitCommandOptionControlKey    UnitCommandOption = 1 << 6
	UnitCommandOptionAltKey        UnitCommandOption = 1 << 7
)
