# cod4_mp - ReXGlue Recompiled Project (Call of Duty 4 / IW3 MULTIPLAYER, iw3mp)
#
# Sibling of mw-recomp (iw3sp). Separate project dir so the codegen-emitted
# generated/rexglue.cmake (hardcoded to generated/) does not collide with SP.
# Reuses the PREBUILT installed rexglue-sdk via CMAKE_PREFIX_PATH.
#
# This file is yours to edit. 'rexglue migrate' will NOT overwrite it.

cmake_minimum_required(VERSION 3.25)
project(cod4_mp LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include(generated/rexglue.cmake)

# Sources
set(COD4MP_SOURCES
    src/main.cpp
    src/cod4mp_patches.cpp
    src/cod4mp_user_settings.cpp
)

if(WIN32)
    add_executable(cod4_mp WIN32 ${COD4MP_SOURCES})
else()
    add_executable(cod4_mp ${COD4MP_SOURCES})
endif()

rexglue_setup_target(cod4_mp)

# SDK ships rexruntime as a SHARED lib; RUNPATH is $ORIGIN, so copy librexruntime.so
# next to the binary — lets `./cod4_mp` run without LD_LIBRARY_PATH.
add_custom_command(TARGET cod4_mp POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            $<TARGET_FILE:rex::runtime> $<TARGET_FILE_DIR:cod4_mp>
    VERBATIM)

# Provide the default runtime config on a fresh build dir (does NOT overwrite an
# existing cod4_mp.toml). The app auto-loads <exe_dir>/cod4_mp.toml at startup.
if(NOT EXISTS "${CMAKE_BINARY_DIR}/cod4_mp.toml")
    configure_file("${CMAKE_CURRENT_SOURCE_DIR}/config/cod4_mp.default.toml"
                   "${CMAKE_BINARY_DIR}/cod4_mp.toml" COPYONLY)
endif()
