### Go AI Wrapper
#
# Global variables set in this file:
# * BUILD_Go_AIWRAPPER
# * Go_AIWRAPPER_SRC_DIR     - directory containing the springai Go package
# * Go_AIWRAPPER_MODULE      - Go module path of the wrapper
#
# Macros defined for use by Go Skirmish AIs (AI/Skirmish/*/CMakeLists.txt):
# * configure_go_skirmish_ai
#
# Unlike the C++ and Java wrappers, the Go wrapper is consumed as Go SOURCE: a
# Go Skirmish AI imports the `springai` package and is compiled with the Go
# toolchain (`go build -buildmode=c-shared`) into a native library that the
# C AI Interface loads. Therefore this file does NOT build a C/C++ static
# library target; it only validates the toolchain, exposes the wrapper location,
# and provides the build macro for Go AIs.
#


set(myName         "Go")
set(mySourceDirRel "src/springai")


# Check if the user wants to compile the wrapper (native AIs use the C interface)
if    ("${AI_TYPES}" STREQUAL "ALL" OR "${AI_TYPES}" STREQUAL "NATIVE")
	set(AIWRAPPERS_NATIVE TRUE)
else  ("${AI_TYPES}" STREQUAL "ALL" OR "${AI_TYPES}" STREQUAL "NATIVE")
	set(AIWRAPPERS_NATIVE FALSE)
endif ("${AI_TYPES}" STREQUAL "ALL" OR "${AI_TYPES}" STREQUAL "NATIVE")


# The Go toolchain is the only hard dependency of this wrapper.
find_program(GO_EXECUTABLE NAMES go DOC "The Go toolchain (https://go.dev)")


# Check dependencies of the wrapper are met
if    (AIWRAPPERS_NATIVE AND GO_EXECUTABLE AND NOT myName MATCHES "${AI_EXCLUDE_REGEX}")
	set_global(BUILD_${myName}_AIWRAPPER TRUE)
else  (AIWRAPPERS_NATIVE AND GO_EXECUTABLE AND NOT myName MATCHES "${AI_EXCLUDE_REGEX}")
	set_global(BUILD_${myName}_AIWRAPPER FALSE)
	if    (NOT GO_EXECUTABLE)
		message("warning: Go AI Wrapper will not be built! (Go toolchain not found)")
	else  (NOT GO_EXECUTABLE)
		message("warning: Go AI Wrapper will not be built!")
	endif (NOT GO_EXECUTABLE)
endif (AIWRAPPERS_NATIVE AND GO_EXECUTABLE AND NOT myName MATCHES "${AI_EXCLUDE_REGEX}")


# Setup
if    (BUILD_${myName}_AIWRAPPER)
	set(myDir "${CMAKE_CURRENT_SOURCE_DIR}")
	get_last_path_part(dirName ${myDir})
	set(myName "${dirName}")
	make_absolute(mySourceDir "${myDir}" "${mySourceDirRel}")

	ai_wrapper_message(STATUS "Found AI Wrapper: ${myName} (Go toolchain: ${GO_EXECUTABLE})")

	set_global(${myName}_AIWRAPPER_SRC_DIR "${mySourceDir}")
	set_global(${myName}_AIWRAPPER_MODULE  "github.com/beyond-all-reason/RecoilEngine/AI/Wrappers/Go")

	set(springAIInterfaceSourceDir "${PROJECT_SOURCE_DIR}/rts/ExternalAI/Interface")

	# Generate the callback/command bindings from the C interface headers
	# (bin/gen), the Go analogue of the C++ wrapper's awk generateSources step.
	# Unlike C++, the output must live in the Go package source tree so the Go
	# toolchain finds it when a Go AI imports the module; the files are
	# .gitignore'd. A stamp file drives regeneration when a header changes.
	set(myGenStamp "${CMAKE_CURRENT_BINARY_DIR}/go-generate.stamp")
	add_custom_command(
		OUTPUT "${myGenStamp}"
		COMMAND "${GO_EXECUTABLE}" run "./bin/gen"
			-out "${mySourceDir}"
			-headers "${springAIInterfaceSourceDir}"
		COMMAND "${CMAKE_COMMAND}" -E touch "${myGenStamp}"
		DEPENDS
			"${springAIInterfaceSourceDir}/SSkirmishAICallback.h"
			"${springAIInterfaceSourceDir}/AISCommands.h"
			"${myDir}/bin/gen/main.go"
			"${myDir}/bin/gen/callbacks.go"
			"${myDir}/bin/gen/commands.go"
		WORKING_DIRECTORY "${myDir}"
		COMMENT "  ${myName}-AIWrapper: generating callback/command bindings" VERBATIM)
	add_custom_target(${myName}-AIWrapper-generate DEPENDS "${myGenStamp}")
	add_dependencies(generateSources ${myName}-AIWrapper-generate)

	# A convenience target that vets the wrapper package once generated, so
	# wrapper-level mistakes are caught even without a Go AI present.
	add_custom_target(${myName}-AIWrapper-vet
		COMMAND "${CMAKE_COMMAND}" -E env "CGO_ENABLED=1"
			"${GO_EXECUTABLE}" vet "./..."
		WORKING_DIRECTORY "${myDir}"
		COMMENT "  ${myName}-AIWrapper: go vet ./..." VERBATIM)
	add_dependencies(${myName}-AIWrapper-vet ${myName}-AIWrapper-generate)
endif (BUILD_${myName}_AIWRAPPER)


# Native Go Skirmish AI configuration macro.
# Called from a Go AI's AI/Skirmish/<Name>/CMakeLists.txt, it compiles the AI
# (which imports the springai wrapper package) into a C-shared library and
# installs it next to its AIInfo.lua/AIOptions.lua data files.
#
# Arguments mirror configure_native_skirmish_ai for familiarity, but only the
# source dir is used (Go resolves its own dependencies via the module):
#   configure_go_skirmish_ai(mySourceDirRel_var)
macro    (configure_go_skirmish_ai mySourceDirRel_var)
	set(myDir                  "${CMAKE_CURRENT_SOURCE_DIR}")
	get_last_path_part(dirName ${myDir})
	set(myName                 "${dirName}")
	set(mySourceDirRel         "${${mySourceDirRel_var}}")

	if    (BUILD_Go_AIWRAPPER AND NOT myName MATCHES "${AI_EXCLUDE_REGEX}")
		set(BUILD_THIS_SKIRMISHAI TRUE)
	else  (BUILD_Go_AIWRAPPER AND NOT myName MATCHES "${AI_EXCLUDE_REGEX}")
		set(BUILD_THIS_SKIRMISHAI FALSE)
		message("warning: ${myName} Go Skirmish AI will not be built!")
	endif (BUILD_Go_AIWRAPPER AND NOT myName MATCHES "${AI_EXCLUDE_REGEX}")

	if    (BUILD_THIS_SKIRMISHAI)
		get_version_plus_dep_file(myVersion myVersionDepFile)
		set(myTarget      "${myName}")
		set(myInstLibsDir "${SKIRMISH_AI_LIBS}/${myName}/${myVersion}")
		set(myInstDataDir "${SKIRMISH_AI_DATA}/${myName}/${myVersion}")
		make_absolute(myAISourceDir "${myDir}" "${mySourceDirRel}")

		skirmish_ai_message(STATUS "Found Skirmish AI: ${myName} ${myVersion} (Go)")

		# Platform-specific shared library name the engine will dlopen.
		set(myLibName "SkirmishAI${CMAKE_SHARED_LIBRARY_SUFFIX}")
		set(myOutDir  "${CMAKE_CURRENT_BINARY_DIR}/data")
		set(myLibFile "${myOutDir}/${myLibName}")

		# Map the CMake target platform to Go's GOOS/GOARCH so cross-builds
		# (e.g. the mingw-w64 Windows build) use the right cgo runtime and
		# produce a matching library, rather than defaulting to the host OS.
		if    (WIN32)
			set(myGoOS "windows")
		elseif (APPLE)
			set(myGoOS "darwin")
		else  ()
			set(myGoOS "linux")
		endif ()
		if    (CMAKE_SYSTEM_PROCESSOR MATCHES "aarch64|arm64|ARM64")
			set(myGoArch "arm64")
		elseif (CMAKE_SIZEOF_VOID_P EQUAL 8)
			set(myGoArch "amd64")
		else  ()
			set(myGoArch "386")
		endif ()

		# Build the C-shared library with the Go toolchain. CC is forwarded so
		# cgo uses the same compiler (and target) as the rest of the engine build.
		#
		# This is an always-run target rather than an add_custom_command keyed on
		# "${myLibFile}": CMake cannot enumerate the Go dependency graph (the AI's
		# own sources plus the imported springai wrapper package and its generated
		# bindings), so an output-keyed command would go stale and silently reuse a
		# previously built library when sources change — masking even syntax errors.
		# `go build` consults Go's own build cache, so re-running it every build is
		# cheap and is the only reliable way to detect any source change.
		add_custom_target(${myTarget} ALL
			COMMAND "${CMAKE_COMMAND}" -E make_directory "${myOutDir}"
			COMMAND "${CMAKE_COMMAND}" -E env
				"CGO_ENABLED=1"
				"GOOS=${myGoOS}"
				"GOARCH=${myGoArch}"
				"CC=${CMAKE_C_COMPILER}"
				"${GO_EXECUTABLE}" build -buildmode=c-shared
				-o "${myLibFile}" "."
			BYPRODUCTS "${myLibFile}"
			WORKING_DIRECTORY "${myAISourceDir}"
			COMMENT "  ${myName}: go build -buildmode=c-shared (${myGoOS}/${myGoArch})" VERBATIM)
		# Ensure the wrapper bindings are generated before this AI is compiled.
		add_dependencies(${myTarget} Go-AIWrapper-generate)

		# Install data files and the produced library, matching the native AI layout.
		install(DIRECTORY "${myDir}/data/" DESTINATION "${myInstLibsDir}"
			FILES_MATCHING PATTERN REGEX "AI(Info|Options)\\.lua$")
		install(FILES "${myLibFile}" DESTINATION "${myInstLibsDir}")
	endif (BUILD_THIS_SKIRMISHAI)
endmacro (configure_go_skirmish_ai mySourceDirRel_var)
