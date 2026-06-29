### GOdless Go Skirmish AI
#

# The Go sources, go.mod and shim.c all live in this directory.
set(mySourceDirRel "")

if    (BUILD_Go_AIWRAPPER)
	configure_go_skirmish_ai(mySourceDirRel)
else  (BUILD_Go_AIWRAPPER)
	message("warning: GOdless Skirmish AI will not be built! (missing Go wrapper / toolchain)")
endif (BUILD_Go_AIWRAPPER)
