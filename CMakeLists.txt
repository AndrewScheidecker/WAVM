cmake_minimum_required(VERSION 3.8.0)

# Parse the VERSION file.
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS VERSION)
file(READ "VERSION" VERSION_FILE_CONTENTS)
string(REGEX MATCH "([0-9]+)\.([0-9]+)\.([0-9]+)(-[a-zA-Z0-9]+)?" WAVM_VERSION_STRING ${VERSION_FILE_CONTENTS})
if(NOT WAVM_VERSION_STRING)
	message(FATAL_ERROR "Could not parse 'VERSION' file")
endif()
set(WAVM_VERSION_MAJOR  ${CMAKE_MATCH_1})
set(WAVM_VERSION_MINOR  ${CMAKE_MATCH_2})
set(WAVM_VERSION_PATCH  ${CMAKE_MATCH_3})
set(WAVM_VERSION_SUFFIX ${CMAKE_MATCH_4})

# Declare the CMake project
project(
	WAVM
	VERSION "${WAVM_VERSION_MAJOR}.${WAVM_VERSION_MINOR}.${WAVM_VERSION_PATCH}"
	LANGUAGES C CXX ASM
)

if(MSVC)
	enable_language(ASM_MASM)
endif()

# Include some modules we use. GNUInstallDirs must be included after the project is declared, since it
# uses configuration variables initialized when the project is declared (CMAKE_SIZEOF_VOID_P).
include(CheckCCompilerFlag)
include(CheckCXXCompilerFlag)
include(GNUInstallDirs)

# Configure CPack
set(CPACK_PACKAGE_NAME "WAVM")
set(CPACK_PACKAGE_VENDOR "Andrew Scheidecker")
set(CPACK_PACKAGE_CONTACT "andrew@scheidecker.net")
set(CPACK_PACKAGE_VERSION_MAJOR ${WAVM_VERSION_MAJOR})
set(CPACK_PACKAGE_VERSION_MINOR ${WAVM_VERSION_MINOR})
set(CPACK_PACKAGE_VERSION_PATCH ${WAVM_VERSION_PATCH})
set(CPACK_PACKAGE_VERSION ${WAVM_VERSION_STRING})
set(CPACK_PACKAGE_DESCRIPTION "WebAssembly virtual machine")
set(CPACK_PACKAGE_HOMEPAGE_URL "https://wavm.github.io")
set(CPACK_RESOURCE_FILE_LICENSE "${CMAKE_CURRENT_SOURCE_DIR}/LICENSE.txt")
set(CPACK_PACKAGE_INSTALL_DIRECTORY "WAVM")
set(CPACK_INCLUDE_TOPLEVEL_DIRECTORY OFF)

set(CPACK_COMPONENT_RUNTIME_DISPLAY_NAME "WAVM Runtime")
set(CPACK_COMPONENT_RUNTIME_DESCRIPTION "The WebAssembly Virtual Machine runtime")
set(CPACK_COMPONENT_RUNTIME_REQUIRED YES)
set(CPACK_COMPONENT_DEVEL_DISPLAY_NAME "WAVM Libraries and C/C++ Headers")
set(CPACK_COMPONENT_DEVEL_DESCRIPTION "The libraries and C/C++ headers needed to use the WAVM runtime in your application")

# Configure NSIS
set(CPACK_NSIS_MODIFY_PATH ON)

# Configure Debian package
set(CPACK_DEBIAN_PACKAGE_DEPENDS "libc6 (>= 2.15), libstdc++6 (>= 5.1)")
if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
	set(CPACK_DEBIAN_PACKAGE_DEPENDS "${CPACK_DEBIAN_PACKAGE_DEPENDS}, libgcc1 (>= 1:${GCC_MAJOR}.${GCC_MINOR})")
endif()
set(CPACK_DEBIAN_PACKAGE_HOMEPAGE "https://wavm.github.io")

include(CPack)

# WAVM configuration options

option(WAVM_ENABLE_STATIC_LINKING "use static linking instead of dynamic for the WAVM libraries" OFF)
option(WAVM_ENABLE_RELEASE_ASSERTS "enable assertions in release builds" 0)
set(WAVM_ENABLE_LTO "OFF" CACHE STRING "enable link-time optimization (off, on, or thin)")

option(WAVM_ENABLE_FUZZ_TARGETS "build the fuzz targets" ON)

if(CMAKE_CXX_COMPILER_ID MATCHES "Clang" OR CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
	# The sanitizers are only available when compiling with Clang and GCC.
	option(WAVM_ENABLE_ASAN "enable ASAN" OFF)
	option(WAVM_ENABLE_UBSAN "enable UBSAN" OFF)
	option(WAVM_ENABLE_TSAN "enable TSAN" OFF)
else()
	set(WAVM_ENABLE_ASAN OFF)
	set(WAVM_ENABLE_UBSAN OFF)
	set(WAVM_ENABLE_TSAN OFF)
endif()

if(CMAKE_CXX_COMPILER_ID MATCHES "Clang")
	# libfuzzer is only available when compiling with Clang.
	option(WAVM_ENABLE_LIBFUZZER "compile WAVM for use by clang/LLVM's libfuzzer" OFF)
else()
	set(WAVM_ENABLE_LIBFUZZER OFF)
endif()

if(CMAKE_SIZEOF_VOID_P EQUAL 4)
	# Disable the runtime on 32-bit platforms.
	set(WAVM_ENABLE_RUNTIME OFF)
else()
	# Allow disabling the runtime on 64-bit platforms.
	option(WAVM_ENABLE_RUNTIME "enables the runtime components of WAVM" ON)
endif()

# On GCC/Clang, provide an option to compile the static libraries as PIC: Position-Independent Code.
# With this option disabled, the static libraries may not be linked into shared libraries.
if(CMAKE_CXX_COMPILER_ID MATCHES "Clang" OR CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
	if(WAVM_ENABLE_STATIC_LINKING)
		option(WAVM_ENABLE_STATIC_LIBRARY_PIC "create position-independent static libraries" ON)
	endif()
endif()

if(WAVM_ENABLE_RUNTIME)
  # TODO(Harrm): seems like LLVM version in Hunter is currently ancient so this doesn't work
	# hunter_add_package(LLVM)

	# Find an installed build of LLVM
	find_package(LLVM REQUIRED CONFIG)

	if(LLVM_VERSION_MAJOR LESS 6)
		message(FATAL_ERROR "WAVM requires LLVM version 6.0 or newer")
	endif()

	# Convert LLVM_DEFINITIONS and LLVM_INCLUDE_DIRS from strings of space-separated elements to
	# CMake lists (strings with semicolon-separated elements).
	separate_arguments(LLVM_DEFINITIONS)
	separate_arguments(LLVM_INCLUDE_DIRS)

	# LLVM on Windows includes a bunch of definitions in LLVM_DEFINITIONS to disable secure CRT
	# warnings, which prevents the warnings from triggering in WAVM code as well. Remove them, since
	# they doesn't seem to be necessary just to include LLVM.
	list(REMOVE_ITEM LLVM_DEFINITIONS "-D_CRT_SECURE_NO_WARNINGS")
	list(REMOVE_ITEM LLVM_DEFINITIONS "-D_CRT_SECURE_NO_DEPRECATE")
	list(REMOVE_ITEM LLVM_DEFINITIONS "-D_CRT_NONSTDC_NO_DEPRECATE")
	list(REMOVE_ITEM LLVM_DEFINITIONS "-D_CRT_NONSTDC_NO_WARNINGS")
	list(REMOVE_ITEM LLVM_DEFINITIONS "-D_SCL_SECURE_NO_DEPRECATE")
	list(REMOVE_ITEM LLVM_DEFINITIONS "-D_SCL_SECURE_NO_WARNINGS")

	option(WAVM_ENABLE_UNWIND "enables printing stack traces" ON)
else()
	set(WAVM_ENABLE_UNWIND OFF)
endif()

# Tell MASM to create SAFESEH-compatible object files on Win32.
if(MSVC AND CMAKE_SIZEOF_VOID_P EQUAL 4)
	set(CMAKE_ASM_MASM_FLAGS "${CMAKE_ASM_MASM_FLAGS} /safeseh")
endif()

# Bind some variables to useful paths.
set(WAVM_SOURCE_DIR ${CMAKE_CURRENT_LIST_DIR})
set(WAVM_INCLUDE_DIR ${WAVM_SOURCE_DIR}/Include/WAVM)

# If no build type is specified, default to RelWithDebInfo
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
	set(CMAKE_BUILD_TYPE "RelWithDebInfo" CACHE STRING "The type of build (Debug, Release, RelWithDebInfo, or MinSizeRel" FORCE)
endif()

# Enable MAXOSX_RPATH by default
cmake_policy(SET CMP0042 NEW)

# Enable cmake's testing infrastructure
enable_testing()

# Enable folders when generating Visual Studio solutions
set_property(GLOBAL PROPERTY USE_FOLDERS ON)

# Put executables/DLLs in the <build>/bin directory.
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${PROJECT_BINARY_DIR}/bin)

# If MSVC LTO is enabled, remove the /INCREMENTAL option from the link flags to avoid link warnings.
function(REMOVE_INCREMENTAL_FLAG INOUT_FLAGS)
	set(INOUT_FLAGS ${OUT_FLAGS_LOCAL} PARENT_SCOPE)
endfunction()
if(MSVC AND WAVM_ENABLE_LTO)
	string(REGEX REPLACE "[-/]INCREMENTAL" "/INCREMENTAL:NO " CMAKE_SHARED_LINKER_FLAGS_RELWITHDEBINFO ${CMAKE_SHARED_LINKER_FLAGS_RELWITHDEBINFO})
	string(REGEX REPLACE "[-/]INCREMENTAL" "/INCREMENTAL:NO " CMAKE_SHARED_LINKER_FLAGS_DEBUG ${CMAKE_SHARED_LINKER_FLAGS_DEBUG})
	string(REGEX REPLACE "[-/]INCREMENTAL" "/INCREMENTAL:NO " CMAKE_EXE_LINKER_FLAGS_RELWITHDEBINFO ${CMAKE_EXE_LINKER_FLAGS_RELWITHDEBINFO})
	string(REGEX REPLACE "[-/]INCREMENTAL" "/INCREMENTAL:NO " CMAKE_EXE_LINKER_FLAGS_DEBUG ${CMAKE_EXE_LINKER_FLAGS_DEBUG})
endif()

# Install Include/WAVM to <install root>/include/WAVM
install(DIRECTORY ${WAVM_SOURCE_DIR}/Include/WAVM
		COMPONENT devel
		DESTINATION include
		PATTERN *.txt EXCLUDE
		PATTERN *.h.in EXCLUDE)

# Generate Inline/Config.h in the build+install directories from Inline/Config.h.in in the source
configure_file(${WAVM_SOURCE_DIR}/Include/WAVM/Inline/Config.h.in
			   ${PROJECT_BINARY_DIR}/Include/WAVM/Inline/Config.h)
install(FILES ${PROJECT_BINARY_DIR}/Include/WAVM/Inline/Config.h
		COMPONENT devel
		DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/WAVM/Inline)

# Generate Inline/Version.h in the build+install directories from Inline/Version.h.in in the source
configure_file(${WAVM_SOURCE_DIR}/Include/WAVM/Inline/Version.h.in
			   ${PROJECT_BINARY_DIR}/Include/WAVM/Inline/Version.h)
install(FILES ${PROJECT_BINARY_DIR}/Include/WAVM/Inline/Version.h
		COMPONENT devel
		DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/WAVM/Inline)

# Install LICENSE to <install root> on Windows and <install root>/share/wavm on other systems.
install(FILES ${WAVM_SOURCE_DIR}/LICENSE.txt
		COMPONENT runtime
		DESTINATION $<IF:$<PLATFORM_ID:Windows>,.,${CMAKE_INSTALL_DATAROOTDIR}/wavm>)

set(WAVM_C_CXX_FLAGS)
set(WAVM_CXX_FLAGS)
set(WAVM_LTO_C_CXX_FLAGS)

set(WAVM_LINKER_FLAGS)
set(WAVM_STATIC_LIBRARY_FLAGS)

# CMake 3.12+ has a list(JOIN) operation, but as of 2019/9, the newest Ubuntu LTS (18.04) only ships with CMake 3.10.
function(wavm_list_join STRING_LIST SEPARATOR VARIABLE_NAME)
	string(REPLACE ";" ${SEPARATOR} JOINED_STRING "${STRING_LIST}")
	set(${VARIABLE_NAME} ${JOINED_STRING} PARENT_SCOPE)
endfunction()

function(wavm_get_config_var_name_for_flag FLAG VAR_NAME)
	string(SUBSTRING ${FLAG} 1 -1 FLAG_NAME)
	string(MAKE_C_IDENTIFIER ${FLAG_NAME} FLAG_NAME)
	string(TOUPPER ${FLAG_NAME} FLAG_NAME)
	set(${VAR_NAME} ${FLAG_NAME} PARENT_SCOPE)
endfunction()

function(wavm_check_cxx_compiler_flag FLAG VARIABLE_NAME)
	wavm_list_join("${WAVM_C_CXX_FLAGS};${WAVM_CXX_FLAGS};${WAVM_LTO_C_CXX_FLAGS}" " " WAVM_CXX_FLAGS_SPACE_SEPARATED)
	check_cxx_compiler_flag("${WAVM_CXX_FLAGS_SPACE_SEPARATED} ${FLAG}" ${VARIABLE_NAME})
endfunction()

function(wavm_add_cxx_flag_if_supported OPTIONAL_FLAG)
	wavm_get_config_var_name_for_flag(${OPTIONAL_FLAG} FLAG_VAR)
	wavm_check_cxx_compiler_flag("${OPTIONAL_FLAG}" "CXX_HAS_${FLAG_VAR}")
	if(CXX_HAS_${FLAG_VAR})
		list(APPEND WAVM_CXX_FLAGS ${OPTIONAL_FLAG})
		set(WAVM_CXX_FLAGS ${WAVM_CXX_FLAGS} PARENT_SCOPE)
	endif()
endfunction()

function(wavm_add_c_cxx_flag_if_supported OPTIONAL_FLAG)
	wavm_get_config_var_name_for_flag(${OPTIONAL_FLAG} FLAG_VAR)
	wavm_check_cxx_compiler_flag("${OPTIONAL_FLAG}" "CXX_HAS_${FLAG_VAR}")
	if(CXX_HAS_${FLAG_VAR})
		list(APPEND WAVM_C_CXX_FLAGS ${OPTIONAL_FLAG})
		set(WAVM_C_CXX_FLAGS ${WAVM_C_CXX_FLAGS} PARENT_SCOPE)
	endif()
endfunction()

function(wavm_check_linker_flag FLAG VARIABLE_NAME)
	wavm_list_join("${WAVM_LINKER_FLAGS}" " " WAVM_LINKER_FLAGS_SPACE_SEPARATED)
	set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} ${WAVM_LINKER_FLAGS_SPACE_SEPARATED} ${FLAG}")
	wavm_check_cxx_compiler_flag("" ${VARIABLE_NAME})
endfunction()

function(wavm_add_linker_flag_if_supported OPTIONAL_FLAG)
	wavm_get_config_var_name_for_flag(${OPTIONAL_FLAG} FLAG_VAR)
	wavm_check_linker_flag(${OPTIONAL_FLAG} "LINKER_HAS_${FLAG_VAR}")
	if(LINKER_HAS_${FLAG_VAR})
		list(APPEND WAVM_LINKER_FLAGS ${OPTIONAL_FLAG})
		set(WAVM_LINKER_FLAGS ${WAVM_LINKER_FLAGS} PARENT_SCOPE)
	endif()
endfunction()

# By default, CMake uses different optimization settings for Release vs RelWithDebInfo builds.
# For GCC, it uses -O3 in Release and -O2 in RelWithDebInfo.
# For MSVC, it uses /Ob2 in Release and /Ob1 in RelWithDebInfo (amount of inlining).
# In order to reduce problems that only occur in Release builds without debug symbols, override the
# default optimization settings so RelWithDebInfo uses the same optimization settings as Release.
set(CMAKE_C_FLAGS_RELWITHDEBINFO_LOCAL ${CMAKE_C_FLAGS_RELWITHDEBINFO})
set(CMAKE_CXX_FLAGS_RELWITHDEBINFO_LOCAL ${CMAKE_CXX_FLAGS_RELWITHDEBINFO})
string(REPLACE "-O2" "-O3" CMAKE_C_FLAGS_RELWITHDEBINFO_LOCAL ${CMAKE_C_FLAGS_RELWITHDEBINFO_LOCAL})
string(REPLACE "-O2" "-O3" CMAKE_CXX_FLAGS_RELWITHDEBINFO_LOCAL ${CMAKE_CXX_FLAGS_RELWITHDEBINFO_LOCAL})
string(REPLACE "/Ob1" "/Ob2" CMAKE_C_FLAGS_RELWITHDEBINFO_LOCAL ${CMAKE_C_FLAGS_RELWITHDEBINFO_LOCAL})
string(REPLACE "/Ob1" "/Ob2" CMAKE_CXX_FLAGS_RELWITHDEBINFO_LOCAL ${CMAKE_CXX_FLAGS_RELWITHDEBINFO_LOCAL})
set(CMAKE_C_FLAGS_RELWITHDEBINFO ${CMAKE_C_FLAGS_RELWITHDEBINFO_LOCAL} CACHE STRING "" FORCE)
set(CMAKE_CXX_FLAGS_RELWITHDEBINFO ${CMAKE_CXX_FLAGS_RELWITHDEBINFO_LOCAL} CACHE STRING "" FORCE)

if(CMAKE_CXX_COMPILER_ID MATCHES "Clang" OR CMAKE_CXX_COMPILER_ID STREQUAL "GNU")

	# Expose the -fuse-ld=<x> option on GCC/Clang through a WAVM_USE_LINKER config variable.
	set(WAVM_USE_LINKER "" CACHE STRING "If set, overrides the default linker (as the compiler option -fuse-ld=<WAVM_USE_LINKER>)")
	if(WAVM_USE_LINKER)
		list(APPEND WAVM_LINKER_FLAGS "-fuse-ld=${WAVM_USE_LINKER}")
	endif()

	# Warn if a switch doesn't handle an enum case even if it has a default label.
	wavm_add_c_cxx_flag_if_supported("-Wswitch-enum")
	wavm_add_c_cxx_flag_if_supported("-Wswitch-default")

	wavm_add_c_cxx_flag_if_supported("-Wnull-dereference")
	wavm_add_c_cxx_flag_if_supported("-Wduplicated-cond")
	wavm_add_c_cxx_flag_if_supported("-Wduplicated-branches")
	wavm_add_c_cxx_flag_if_supported("-Wlogical-op")
	wavm_add_cxx_flag_if_supported("-Wnon-virtual-dtor")
	wavm_add_c_cxx_flag_if_supported("-Wrestrict")
	wavm_add_c_cxx_flag_if_supported("-Wdouble-promotion")

	# Exclude some warnings included in Wextra that we don't care about.
	wavm_add_c_cxx_flag_if_supported("-Wno-missing-field-initializers")
	wavm_add_c_cxx_flag_if_supported("-Wno-unused-parameter")
endif()

# Use -fvisibility=hidden if available. The *_API definitions are also changed by the value of
# CXX_HAS_FVISIBILITY_HIDDEN.
#wavm_add_c_cxx_flag_if_supported("-fvisibility=hidden")

string(TOUPPER "${WAVM_ENABLE_LTO}" UPPERCASE_WAVM_ENABLE_LTO)
if(UPPERCASE_WAVM_ENABLE_LTO STREQUAL "THIN")
	list(APPEND WAVM_LTO_C_CXX_FLAGS "-flto=thin")
	if(NOT MSVC)
		list(APPEND WAVM_LINKER_FLAGS "-flto=thin")
	endif()

	# Detect which command line options the linker supports to configure the ThinLTO cache.
	wavm_check_linker_flag("-Wl,--thinlto-cache-dir=${CMAKE_BINARY_DIR}/.thinltocache" LINKER_HAS_ELF_LLD_THINLTO_CMD_SYNTAX)
	wavm_check_linker_flag("-Wl,-plugin-opt,cache-dir=${CMAKE_BINARY_DIR}/.thinltocache" LINKER_HAS_GOLD_THINLTO_CMD_SYNTAX)
	wavm_check_linker_flag("-Wl,-cache_path_lto,${CMAKE_BINARY_DIR}/.thinltocache" LINKER_HAS_LD64_THINLTO_CMD_SYNTAX)
	wavm_check_linker_flag("/lldltocache:${CMAKE_BINARY_DIR}/.thinltocache" LINKER_HAS_COFF_LLD_THINLTO_CMD_SYNTAX)

	if(LINKER_HAS_COFF_LLD_THINLTO_CMD_SYNTAX)
		# COFF LLD ThinLTO cache options
		# Favor this path as hack, because lld-link.exe will accept and ignore the other syntaxes
		# with a warning, but LLD, gold, and LD64 will reject this syntax as an error.
		list(APPEND WAVM_LINKER_FLAGS "/lldltocache:${CMAKE_BINARY_DIR}/.thinltocache")
		list(APPEND WAVM_LINKER_FLAGS "/lldltocachepolicy:cache_size_bytes=1g")
	elseif(LINKER_HAS_ELF_LLD_THINLTO_CMD_SYNTAX)
		# LLD ThinLTO cache options
		list(APPEND WAVM_LINKER_FLAGS "-Wl,--thinlto-cache-dir=${CMAKE_BINARY_DIR}/.thinltocache")
		list(APPEND WAVM_LINKER_FLAGS "-Wl,--thinlto-cache-policy,cache_size_bytes=1g")
	elseif(LINKER_HAS_GOLD_THINLTO_CMD_SYNTAX)
		# Gold ThinLTO cache options
		list(APPEND WAVM_LINKER_FLAGS "-Wl,-plugin-opt,cache-dir=${CMAKE_BINARY_DIR}/.thinltocache")
		list(APPEND WAVM_LINKER_FLAGS "-Wl,-plugin-opt,cache-policy=cache_size_bytes=1g")
	elseif(LINKER_HAS_LD64_THINLTO_CMD_SYNTAX)
		# ld64 ThinLTO cache options
		list(APPEND WAVM_LINKER_FLAGS "-Wl,-cache_path_lto,${CMAKE_BINARY_DIR}/.thinltocache")
	endif()
elseif(UPPERCASE_WAVM_ENABLE_LTO STREQUAL "ON")
	if(CMAKE_CXX_COMPILER_ID MATCHES "Clang" OR CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
		list(APPEND WAVM_LTO_C_CXX_FLAGS "-flto")
		if(NOT MSVC)
			list(APPEND WAVM_LINKER_FLAGS "-flto")
		endif()
	elseif(MSVC)
		list(APPEND WAVM_LTO_C_CXX_FLAGS "/GL")
		list(APPEND WAVM_LINKER_FLAGS "/LTCG")
		list(APPEND WAVM_STATIC_LIBRARY_FLAGS "/LTCG")
	else()
		message(FATAL_ERROR "Your compiler does not seem to support LTO via either '-flto' or '/GL'.")
	endif()
elseif(NOT UPPERCASE_WAVM_ENABLE_LTO STREQUAL "OFF")
	message(FATAL_ERROR "WAVM_ENABLE_LTO must be 'OFF', 'ON', or 'THIN'")
endif()

if(CMAKE_CXX_COMPILER_ID MATCHES "Clang" OR CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
	# When -flto and -gsplit-dwarf are passed to GCC, it produces a non-error note:
	# ‘-gsplit-dwarf’ is not supported with LTO, disabling
	# To avoid the nuisance, don't bother with -gsplit-dwarf if any form LTO is enabled on GCC.
	if(NOT CMAKE_CXX_COMPILER_ID STREQUAL "GNU" OR UPPERCASE_WAVM_ENABLE_LTO STREQUAL "OFF")
		# If the compiler supports it, use -gsplit-dwarf for the configurations that build debug info.
		wavm_check_cxx_compiler_flag("-gsplit-dwarf" CXX_HAS_GSPLIT_DWARF)
		if(CXX_HAS_GSPLIT_DWARF)
			set(CMAKE_C_FLAGS_RELWITHDEBINFO   "${CMAKE_C_FLAGS_RELWITHDEBINFO}   -gsplit-dwarf")
			set(CMAKE_CXX_FLAGS_RELWITHDEBINFO "${CMAKE_CXX_FLAGS_RELWITHDEBINFO} -gsplit-dwarf")
			set(CMAKE_C_FLAGS_DEBUG            "${CMAKE_C_FLAGS_DEBUG}            -gsplit-dwarf")
			set(CMAKE_CXX_FLAGS_DEBUG          "${CMAKE_CXX_FLAGS_DEBUG}          -gsplit-dwarf")

			# If the linker supports it, use --gdb-index to add an index of external debug data to the
			# binaries. gold and lld support this, ld does not.
			wavm_add_linker_flag_if_supported("-Wl,--gdb-index")
		endif()
	endif()
endif()

function(WAVM_SET_TARGET_LINKER_OPTIONS TARGET_NAME)
	target_compile_options(${TARGET_NAME} PRIVATE ${WAVM_LTO_C_CXX_FLAGS})
	if(MSVC)
		if(WAVM_LINKER_FLAGS)
			wavm_list_join("${WAVM_LINKER_FLAGS}" " " WAVM_LINKER_FLAGS_SPACE_SEPARATED)
			set_target_properties(${TARGET_NAME} PROPERTIES LINK_FLAGS "${WAVM_LINKER_FLAGS_SPACE_SEPARATED}")
		endif()
		if(WAVM_STATIC_LIBRARY_FLAGS)
			wavm_list_join("${WAVM_STATIC_LIBRARY_FLAGS}" " " WAVM_STATIC_LIBRARY_FLAGS_SPACE_SEPARATED)
			set_target_properties(${TARGET_NAME} PROPERTIES STATIC_LIBRARY_FLAGS "${WAVM_STATIC_LIBRARY_FLAGS_SPACE_SEPARATED}")
		endif()
	else()
		target_link_libraries(${TARGET_NAME} PRIVATE ${WAVM_LINKER_FLAGS})
	endif()

	if(NOT CMAKE_SYSTEM_NAME STREQUAL "Windows")
		get_target_property(TARGET_TYPE ${TARGET_NAME} TYPE)
		if(TARGET_TYPE STREQUAL "STATIC_LIBRARY" AND WAVM_ENABLE_STATIC_LIBRARY_PIC)
			# Ensure that even static libraries are relocatable so they can be linked into a .so
			target_compile_options(${TARGET_NAME} PRIVATE "-fPIC")
		elseif(TARGET_TYPE STREQUAL "EXECUTABLE" AND WAVM_ENABLE_STATIC_LINKING)
			#target_link_libraries(${TARGET_NAME} PRIVATE "-static")
		endif()
	endif()
endfunction()

# A function that sets sanitizer options on a target.
function(WAVM_SET_TARGET_SANITIZER_OPTIONS TARGET_NAME)
	if(CMAKE_CXX_COMPILER_ID MATCHES "Clang" OR CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
		if(WAVM_ENABLE_ASAN)
			target_compile_options(${TARGET_NAME} PRIVATE "-fsanitize=address")
			target_link_libraries(${TARGET_NAME} PUBLIC "-fsanitize=address")
		endif()

		# Optionally enable the undefined-behavior sanitizer.
		if(WAVM_ENABLE_UBSAN)
			target_compile_options(${TARGET_NAME} PRIVATE "-fsanitize=undefined")
			target_link_libraries(${TARGET_NAME} PUBLIC -fsanitize=undefined)
			target_compile_options(${TARGET_NAME} PRIVATE "-fno-sanitize-recover=undefined")
		endif()

		# Optionally enable the thread sanitizer.
		if(WAVM_ENABLE_TSAN)
			target_compile_options(${TARGET_NAME} PRIVATE "-fsanitize=thread")
			target_link_libraries(${TARGET_NAME} PUBLIC -fsanitize=thread)
		endif()

		# Optionally enable Clang's libfuzzer.
		if(CMAKE_CXX_COMPILER_ID MATCHES "Clang" AND WAVM_ENABLE_LIBFUZZER)
			target_compile_options(${TARGET_NAME} PRIVATE "-fsanitize=fuzzer")
			target_compile_options(${TARGET_NAME} PRIVATE "-fsanitize-coverage=trace-cmp,trace-div,trace-gep")
		endif()

		if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
			# Link with the static sanitizer runtimes instead of the shared sanitize runtimes on GCC.
			# This matches the default behavior for Clang.
			if(WAVM_ENABLE_ASAN)
				target_link_libraries(${TARGET_NAME} PUBLIC "-static-libasan")
			endif()
			if(WAVM_ENABLE_UBSAN)
				target_link_libraries(${TARGET_NAME} PUBLIC "-static-libubsan")
			endif()
			if(WAVM_ENABLE_TSAN)
				target_link_libraries(${TARGET_NAME} PUBLIC "-static-libtsan")
			endif()
		endif()
	endif()
endfunction()

# A function that sets compile options that are common to all WAVM targets.
function(WAVM_SET_TARGET_COMPILE_OPTIONS TARGET_NAME)
	# Add the WAVM public include directory.
	target_include_directories(
		${TARGET_NAME}
		PUBLIC $<INSTALL_INTERFACE:include>
			   $<BUILD_INTERFACE:${WAVM_SOURCE_DIR}/Include>
			   $<BUILD_INTERFACE:${PROJECT_BINARY_DIR}/Include>
	)

	# Target C++11.
	target_compile_features(${TARGET_NAME} PUBLIC cxx_std_14)

	# Set sanitizer options.
	WAVM_SET_TARGET_SANITIZER_OPTIONS(${TARGET_NAME})

	if(MSVC)
		# Compile files in parallel.
		target_compile_options(${TARGET_NAME} PRIVATE "/MP")

		# Compile with all warnings, and fatal warnings.
		target_compile_options(${TARGET_NAME} PRIVATE "/W4")
		target_compile_options(${TARGET_NAME} PRIVATE "/WX")

		# Disable warnings
		target_compile_options(${TARGET_NAME} PRIVATE "/wd4127") # conditional expression is constant
		target_compile_options(${TARGET_NAME} PRIVATE "/wd4100") # unreferenced formal parameter
		target_compile_options(${TARGET_NAME} PRIVATE "/wd4512") # assignment operator could not be generated
		target_compile_options(${TARGET_NAME} PRIVATE "/wd4141") # 'inline': used more than once
		target_compile_options(${TARGET_NAME} PRIVATE "/wd4310") # cast truncates constant value
		target_compile_options(${TARGET_NAME} PRIVATE "/wd4324") # structure was padded due to alignment specifier
		target_compile_options(${TARGET_NAME} PRIVATE "/wd4146") # unary minus operator applied to unsigned type, result still unsigned
		target_compile_options(${TARGET_NAME} PRIVATE "/wd4204") # nonstandard extension used : non-constant aggregate initializer
		target_compile_options(${TARGET_NAME} PRIVATE "/wd4251") # struct '' needs to have dll-interface to be used by clients of struct ''

		target_link_libraries(${TARGET_NAME} PRIVATE "-ignore:4199") # /DELAYLOAD:... ignored; no imports found from ...

		if(CMAKE_CXX_COMPILER_ID MATCHES "Clang")
			target_compile_options(${TARGET_NAME} PRIVATE "-Wno-deprecated-declarations")
		endif()

		# error C2338:  You've instantiated std::aligned_storage<Len, Align> with an extended alignment
		# (in other words, Align > alignof(max_align_t)). Before VS 2017 15.8, the member "type" would
		# non-conformingly have an alignment of only alignof(max_align_t). VS 2017 15.8 was fixed to
		# handle this correctly, but the fix inherently changes layout and breaks binary compatibility
		# (*only* for uses of aligned_storage with extended alignments). Please define either
		# (1) _ENABLE_EXTENDED_ALIGNED_STORAGE to acknowledge that you understand this message and
		#     that you actually want a type with an extended alignment, or
		# (2) _DISABLE_EXTENDED_ALIGNED_STORAGE to silence this message and get the old non-conformant
		#     behavior.
		target_compile_definitions(${TARGET_NAME} PRIVATE "_ENABLE_EXTENDED_ALIGNED_STORAGE")
	elseif(CMAKE_CXX_COMPILER_ID MATCHES "Clang" OR CMAKE_CXX_COMPILER_ID STREQUAL "GNU")

		# Compile with all+extra warnings and fatal warnings
		target_compile_options(${TARGET_NAME} PRIVATE "-Wall")
		target_compile_options(${TARGET_NAME} PRIVATE "-Wextra")
		target_compile_options(${TARGET_NAME} PRIVATE "-Werror")

		# Disable RTTI to allow linking against a build of LLVM that was compiled without it.
		target_compile_options(${TARGET_NAME} PRIVATE $<$<COMPILE_LANGUAGE:CXX>:-fno-rtti>)

		# Don't eliminate frame pointers: this makes thread forking work robustly if one of the
		# sanitizers requires a frame pointer, and makes ASAN's stack trace on malloc much better
		# without using the slow libunwind path.
		target_compile_options(${TARGET_NAME} PRIVATE "-fno-omit-frame-pointer")
	endif()

	if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
		# Compile with -pthread on Linux.
		target_compile_options(${TARGET_NAME} PRIVATE "-pthread")
		target_link_libraries(${TARGET_NAME} PRIVATE "-pthread")
	endif()

	# Add the optional flags that the compiler was detected to support.
	# This needs to happen *AFTER* the above -Wall -Wextra to ensure that those flags don't
	# re-enable warnings that are being disabled by these flags.
	target_compile_options(${TARGET_NAME} PRIVATE ${WAVM_C_CXX_FLAGS})
	target_compile_options(${TARGET_NAME} PRIVATE $<$<COMPILE_LANGUAGE:CXX>:${WAVM_CXX_FLAGS}>)

	# Add the linker flags.
	WAVM_SET_TARGET_LINKER_OPTIONS(${TARGET_NAME})
endfunction()

function(WAVM_INSTALL_TARGET TARGET_NAME)
	install(TARGETS ${TARGET_NAME}
			EXPORT WAVMInstallTargets COMPONENT devel
			LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR} COMPONENT devel
			ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR} COMPONENT devel
			RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR} COMPONENT runtime)

	# Install PDB files to the same directory as the binaries.
	# Exclude static libraries as TARGET_PDB_FILE only works for linked targets.
	get_target_property(TARGET_TYPE ${TARGET_NAME} TYPE)
	if(MSVC AND NOT ${TARGET_TYPE} STREQUAL "STATIC_LIBRARY")
		install(FILES $<TARGET_PDB_FILE:${TARGET_NAME}>
				COMPONENT devel
				DESTINATION bin
				OPTIONAL)
	endif()
endfunction()

# Older versions of CMake can't handle target_link_libraries on the monolithic WAVM library
# when invoked from a source directory other than the root directory where the WAVM target is
# defined. To get around this, accumulate the libraries this component needs to link with in an
# internal config variable, which seems to be the closest thing CMake has to a global variable.
# After processing all library components, this root directory CMakeLists.txt invokes
# target_link_libraries with the accumulated libraries.
set(WAVM_MONOLIB_PRIVATE_LIBS "" CACHE INTERNAL "" FORCE)
set(WAVM_MONOLIB_PUBLIC_LIBS "" CACHE INTERNAL "" FORCE)

# CMake also scopes the effect of set_source_files_properties to targets created by the same list
# file, so to set the header-only flag on source files in the WAVM monolib, it's necessary to
# accumulate the set of header-only files in a global variable while processing subdirectory list
# files, and then set the source file properties at the end of this list file.
set(WAVM_MONOLIB_NONCOMPILED_SOURCE_FILES "" CACHE INTERNAL "" FORCE)

function(WAVM_ADD_LIB_COMPONENT COMPONENT_NAME)
	cmake_parse_arguments(COMPONENT
		""
		""
		"SOURCES;NONCOMPILED_SOURCES;PRIVATE_LIBS;PUBLIC_LIBS;PRIVATE_LIB_COMPONENTS;PUBLIC_LIB_COMPONENTS;PRIVATE_INCLUDE_DIRECTORIES;PRIVATE_SYSTEM_INCLUDE_DIRECTORIES;PRIVATE_DEFINITIONS;PUBLIC_DEFINITIONS"
		${ARGN})

	# Translate the relative source and header file paths to absolute paths.
	# Older versions of CMake will use the source directory where a target is defined as the root
	# for relative paths in the target's sources, which breaks when adding the source files to The
	# monolithic WAVM library defined in the root directory.
	foreach(COMPONENT_SOURCE ${COMPONENT_SOURCES})
		get_filename_component(COMPONENT_SOURCE_ABSOLUTE ${COMPONENT_SOURCE} ABSOLUTE)
		list(APPEND COMPONENT_SOURCES_ABSOLUTE ${COMPONENT_SOURCE_ABSOLUTE})
	endforeach()
	foreach(COMPONENT_NONCOMPILED_SOURCE ${COMPONENT_NONCOMPILED_SOURCES})
		get_filename_component(COMPONENT_NONCOMPILED_SOURCE_ABSOLUTE ${COMPONENT_NONCOMPILED_SOURCE} ABSOLUTE)
		list(APPEND COMPONENT_NONCOMPILED_SOURCES_ABSOLUTE ${COMPONENT_NONCOMPILED_SOURCE_ABSOLUTE})
	endforeach()

	# Directly add the component's source files to the monolithic WAVM library.
	target_sources(libWAVM PRIVATE ${COMPONENT_SOURCES_ABSOLUTE} ${CMAKE_CURRENT_LIST_FILE})

	# Add the non-compiled source files to a global list that will be flagged as "header-only".
	list(APPEND WAVM_MONOLIB_NONCOMPILED_SOURCE_FILES ${COMPONENT_NONCOMPILED_SOURCES_ABSOLUTE})
	set(WAVM_MONOLIB_NONCOMPILED_SOURCE_FILES ${WAVM_MONOLIB_NONCOMPILED_SOURCE_FILES} CACHE INTERNAL "" FORCE)

	# Add the libraries this component depends on to the global list of libraries to link the
	# monolithic WAVM library with.
	list(APPEND WAVM_MONOLIB_PRIVATE_LIBS ${COMPONENT_PRIVATE_LIBS})
	list(APPEND WAVM_MONOLIB_PUBLIC_LIBS ${COMPONENT_PUBLIC_LIBS})
	set(WAVM_MONOLIB_PRIVATE_LIBS ${WAVM_MONOLIB_PRIVATE_LIBS} CACHE INTERNAL "" FORCE)
	set(WAVM_MONOLIB_PUBLIC_LIBS ${WAVM_MONOLIB_PUBLIC_LIBS} CACHE INTERNAL "" FORCE)

	# Add the component's include directories and definitions.
	target_include_directories(libWAVM PRIVATE ${COMPONENT_PRIVATE_INCLUDE_DIRECTORIES})
	target_include_directories(libWAVM SYSTEM PRIVATE ${COMPONENT_PRIVATE_SYSTEM_INCLUDE_DIRECTORIES})
	target_compile_definitions(libWAVM PRIVATE ${COMPONENT_PRIVATE_DEFINITIONS})
	target_compile_definitions(libWAVM PUBLIC ${COMPONENT_PUBLIC_DEFINITIONS})
endfunction()

function(WAVM_ADD_EXECUTABLE EXE_NAME)
	cmake_parse_arguments(EXE
		""
		"FOLDER"
		"SOURCES;NONCOMPILED_SOURCES;PRIVATE_LIBS;PUBLIC_LIBS;PRIVATE_LIB_COMPONENTS;PUBLIC_LIB_COMPONENTS"
		${ARGN})

	add_executable(${EXE_NAME} ${EXE_SOURCES} ${EXE_NONCOMPILED_SOURCES})
	WAVM_SET_TARGET_COMPILE_OPTIONS(${EXE_NAME})

	# Mark the non-compiled sources as header-only, which includes the files in IDE projects, but
	# doesn't compile them.
	set_source_files_properties(${EXE_NONCOMPILED_SOURCES} PROPERTIES HEADER_FILE_ONLY TRUE)

	# Add the executable's link libraries.
	target_link_libraries(${EXE_NAME} PRIVATE ${EXE_PRIVATE_LIBS})
	target_link_libraries(${EXE_NAME} PUBLIC ${EXE_PUBLIC_LIBS})

	# Ignore the PRIVATE_LIB_COMPONENTS and PUBLIC_LIB_COMPONENTS, and just link the executable with
	# the monolithic WAVM library.
	target_link_libraries(${EXE_NAME} PRIVATE libWAVM)

	if(EXE_FOLDER)
		set_target_properties(${EXE_NAME} PROPERTIES FOLDER ${EXE_FOLDER})
	endif()
endfunction()

function(WAVM_ADD_THIRD_PARTY_LIBRARY LIB_NAME)
	cmake_parse_arguments(LIB
		""
		""
		"SOURCES;NONCOMPILED_SOURCES;PRIVATE_DEFINITIONS;PUBLIC_DEFINITIONS;PRIVATE_INCLUDE_DIRECTORIES;PUBLIC_INCLUDE_DIRECTORIES;PRIVATE_COMPILE_OPTIONS"
		${ARGN})

	# Create a static library.
	add_library(${LIB_NAME} STATIC ${LIB_SOURCES} ${LIB_NONCOMPILED_SOURCES})

	# Target C++11.
	target_compile_features(${LIB_NAME} PRIVATE cxx_std_11)

	# Set the configured sanitizer and linker options.
	if(NOT ${LIB_NAME} STREQUAL "WAVMUnwind")
		# TODO: figure out why enabling the sanitizers on libunwind causes ASAN failures.
		WAVM_SET_TARGET_SANITIZER_OPTIONS(${LIB_NAME})
	endif()
	WAVM_SET_TARGET_LINKER_OPTIONS(${LIB_NAME})

	# Mark the non-compiled sources as header-only, which includes the files in IDE projects, but
	# doesn't compile them.
	set_source_files_properties(${LIB_NONCOMPILED_SOURCES} PROPERTIES HEADER_FILE_ONLY TRUE)

	# Add the library's definitions, include directories, and compile options.
	target_compile_definitions(${LIB_NAME} PRIVATE ${LIB_PRIVATE_DEFINITIONS})
	target_compile_definitions(${LIB_NAME} PUBLIC ${LIB_PUBLIC_DEFINITIONS})
	target_include_directories(${LIB_NAME} PRIVATE ${LIB_PRIVATE_INCLUDE_DIRECTORIES})
	target_include_directories(${LIB_NAME} PUBLIC ${LIB_PUBLIC_INCLUDE_DIRECTORIES})
	target_compile_options(${LIB_NAME} PRIVATE ${LIB_PRIVATE_COMPILE_OPTIONS})

	if(CMAKE_CXX_COMPILER_ID MATCHES "Clang" OR CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
		if(NOT WAVM_ENABLE_STATIC_LINKING OR WAVM_ENABLE_STATIC_LIBRARY_PIC)
			# Ensure that even static libraries are relocatable so they can be linked into a .so
			target_compile_options(${LIB_NAME} PRIVATE "-fPIC")
		endif()
	endif()

	# When using static linking for the WAVM libraries, the gdtoa library needs to be
	# installed. Otherwise, it will just be linked into the WAVM .so/.dylib files.
	if(WAVM_ENABLE_STATIC_LINKING)
		WAVM_INSTALL_TARGET(${LIB_NAME})
	endif()

	set_target_properties(${LIB_NAME} PROPERTIES FOLDER "Third party")
endfunction()

# Create a WAVM library that will include all WAVM library components.
if(WAVM_ENABLE_STATIC_LINKING)
	add_library(libWAVM STATIC)
else()
	add_library(libWAVM SHARED)
endif()
WAVM_SET_TARGET_COMPILE_OPTIONS(libWAVM)
WAVM_INSTALL_TARGET(libWAVM)
set_target_properties(libWAVM PROPERTIES
	FOLDER Libraries
	INSTALL_RPATH_USE_LINK_PATH TRUE
	SOVERSION ${WAVM_VERSION_MAJOR}
	VERSION ${WAVM_VERSION}
	PREFIX ""
)

# Set up the WAVM_API definitions.
if(NOT WAVM_ENABLE_STATIC_LINKING AND MSVC)
	target_compile_definitions(libWAVM PRIVATE "\"WAVM_API=__declspec(dllexport)\"")
	target_compile_definitions(libWAVM INTERFACE "\"WAVM_API=__declspec(dllimport)\"")
elseif(NOT WAVM_ENABLE_STATIC_LINKING AND CXX_HAS_FVISIBILITY_HIDDEN)
	target_compile_definitions(libWAVM PUBLIC "WAVM_API=__attribute__((visibility(\"default\")))")
else()
	target_compile_definitions(libWAVM PUBLIC "WAVM_API=")
endif()

# Process the CMake scripts in subdirectories.

option(WAVM_BUILD_EXAMPLES "Build examples" OFF)
if (WAVM_BUILD_EXAMPLES)
	# Install the Examples directory to <install root>/examples on Windows, and <install root>/share/wavm/examples on other systems.
	install(DIRECTORY ${WAVM_SOURCE_DIR}/Examples/
			DESTINATION $<IF:$<PLATFORM_ID:Windows>,examples,${CMAKE_INSTALL_DATAROOTDIR}/wavm/examples>
			COMPONENT runtime
			PATTERN *.txt EXCLUDE)

	add_subdirectory(Examples)
endif()

add_subdirectory(Include/WAVM/Inline)
add_subdirectory(Lib/IR)
add_subdirectory(Lib/Logging)
add_subdirectory(Lib/NFA)
add_subdirectory(Lib/Platform)
add_subdirectory(Lib/RegExp)
add_subdirectory(Lib/VFS)
add_subdirectory(Lib/WASM)
add_subdirectory(Lib/WASTParse)
add_subdirectory(Lib/WASTPrint)

option(WAVM_BUILD_PROGRAMS "Build programs" OFF)
if (WAVM_BUILD_PROGRAMS)
  add_subdirectory(Programs/wavm)
endif()

option(WAVM_BUILD_TESTS "Build tests" OFF)
if (WAVM_BUILD_TESTS)
	add_subdirectory(Test)
endif()

add_subdirectory(ThirdParty/BLAKE2)
add_subdirectory(ThirdParty/liblmdb)

if(WAVM_ENABLE_RUNTIME)
	add_subdirectory(Include/WAVM/RuntimeABI)
	add_subdirectory(Lib/Emscripten)
	add_subdirectory(Lib/ObjectCache)
	add_subdirectory(Lib/LLVMJIT)
	add_subdirectory(Lib/Runtime)
	add_subdirectory(Lib/ThreadTest)
	add_subdirectory(Lib/WASI)
	add_subdirectory(Lib/wavm-c)
endif()

if(WAVM_ENABLE_UNWIND)
	add_subdirectory(ThirdParty/libunwind)
endif()

# Add the library dependencies accumulated from the various library components as link dependencies
# of the monolithic WAVM library.
target_link_libraries(libWAVM PRIVATE ${WAVM_MONOLIB_PRIVATE_LIBS})
target_link_libraries(libWAVM PUBLIC ${WAVM_MONOLIB_PUBLIC_LIBS})

# Set the non-compiled source files accumulated from the various library components as header-only,
# which includes the files in IDE projects, but doesn't compile them.
target_sources(libWAVM PRIVATE ${WAVM_MONOLIB_NONCOMPILED_SOURCE_FILES})
set_source_files_properties(${WAVM_MONOLIB_NONCOMPILED_SOURCE_FILES} PROPERTIES HEADER_FILE_ONLY TRUE)

# Create a CMake package in <build>/lib/cmake/WAVM containing the WAVM library targets.
export(
	EXPORT WAVMInstallTargets
	FILE ${CMAKE_CURRENT_BINARY_DIR}/lib/cmake/WAVM/WAVMConfig.cmake
	NAMESPACE WAVM::)

# Create a CMake package in <install root>/lib/cmake/WAVM containing the WAVM library targets.
install(
	EXPORT WAVMInstallTargets
	COMPONENT devel
	FILE WAVMConfig.cmake
	DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/WAVM
	NAMESPACE WAVM::)

# Create a dummy target to hold various files in the project root
add_custom_target(RootFiles SOURCES
	.clang-format
	LICENSE.txt
	README.md
	VERSION
	vs-chromium-project.txt
	WebAssembly.tmLanguage)
