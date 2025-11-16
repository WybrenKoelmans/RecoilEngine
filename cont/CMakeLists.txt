# required for man pages (optional) and base files (mandatory)

### Make base-files (aka sdz's)
add_subdirectory(base)

### Install base-files
install (DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/ DESTINATION ${DATADIR}
		PATTERN ".gitignore" EXCLUDE
		PATTERN "base" EXCLUDE
		PATTERN "freedesktop" EXCLUDE
		PATTERN "CMakeLists.txt" EXCLUDE
		PATTERN "CMakeCache" EXCLUDE
		PATTERN "CMakeFiles" EXCLUDE
		PATTERN "Makefile" EXCLUDE
		PATTERN "cmake_install.cmake" EXCLUDE
		PATTERN "springrc-template-insource.txt" EXCLUDE
	)

### Install freedesktop-files, mime-types etc.
if    (UNIX)
	install (
		FILES
			${CMAKE_CURRENT_SOURCE_DIR}/freedesktop/applications/spring.desktop
		DESTINATION
			${APPLICATIONS_DIR}
		)
	install (
		FILES
			${CMAKE_CURRENT_SOURCE_DIR}/freedesktop/mime/spring.xml
		DESTINATION
			${MIME_DIR}/packages
		)
	install (
		FILES
			${CMAKE_CURRENT_SOURCE_DIR}/freedesktop/pixmaps/spring.png
			${CMAKE_CURRENT_SOURCE_DIR}/freedesktop/pixmaps/application-x-spring-demo.png
		DESTINATION
			${PIXMAPS_DIR}
		)
endif (UNIX)

if (NOT EXISTS ${Spring_BINARY_DIR}/springsettings.cfg)
	if(WIN32)
		set(PLACEHOLDER ";")
	else()
		set(PLACEHOLDER ":")
	endif()
	set("SpringDataDirInSource" "${Spring_BINARY_DIR}${PLACEHOLDER}${Spring_SOURCE_DIR}/cont")
	set("FontFile" "${Spring_SOURCE_DIR}/cont/fonts/FreeSansBold.otf")
	set("SmallFontFile" "${Spring_SOURCE_DIR}/cont/fonts/FreeSansBold.otf")
	configure_file(springrc-template-insource.txt ${Spring_BINARY_DIR}/springsettings.cfg)
else()
	message(STATUS "${Spring_BINARY_DIR}/springsettings.cfg already exists!")
endif()

### Install dependencies (windows, mingwlibs DLLs)
if    (MINGW)
	install (DIRECTORY ${MINGWLIBS}/dll/ DESTINATION ${BINDIR} PATTERN "*.dll")

	#execute_process(COMMAND ${CMAKE_CXX_COMPILER} "-print-file-name=libstdc++-6.dll" OUTPUT_VARIABLE STDCXX_LIBRARY OUTPUT_STRIP_TRAILING_WHITESPACE)
	#install(FILES ${STDCXX_LIBRARY} DESTINATION ${BINDIR})

	# pthreadGC2 is a dependency of ThreadPool
#	find_library(PTHREADGC2_LIBRARY NAMES pthreadGC2)
#	if(NOT EXISTS ${PTHREADGC2_LIBRARY})
#		message(SEND_ERROR "pthreadGC2 not found!")
#	endif()
#	install(FILES ${PTHREADGC2_LIBRARY} DESTINATION ${BINDIR})

	# Try to install OpenXR loader DLL from common locations
	set(OPENXR_DLL_PATH "/usr/x86_64-w64-mingw32/bin/libopenxr_loader.dll")
	if (EXISTS ${OPENXR_DLL_PATH})
		message(STATUS "Installing OpenXR loader from: ${OPENXR_DLL_PATH}")
		install(FILES ${OPENXR_DLL_PATH} DESTINATION ${BINDIR})
	else()
		message(WARNING "OpenXR loader DLL not found at ${OPENXR_DLL_PATH} - VR may not work at runtime")
	endif()

	# OpenXR loader depends on libstdc++-6.dll
	set(LIBSTDCPP_DLL_PATH "/usr/lib/gcc/x86_64-w64-mingw32/13-posix/libstdc++-6.dll")
	if (EXISTS ${LIBSTDCPP_DLL_PATH})
		message(STATUS "Installing libstdc++-6.dll for OpenXR: ${LIBSTDCPP_DLL_PATH}")
		install(FILES ${LIBSTDCPP_DLL_PATH} DESTINATION ${BINDIR})
	endif()
endif (MINGW)

