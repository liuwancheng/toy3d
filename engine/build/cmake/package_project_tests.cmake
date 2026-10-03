cmake_minimum_required(VERSION 3.21)

string(RANDOM LENGTH 16 ALPHABET 0123456789abcdef TOY3D_TEST_ID)
set(TOY3D_FIXTURE "${TOY3D_BUILD}/package-test-output/${TOY3D_TEST_ID}")
set(TOY3D_ORIGINAL "${TOY3D_FIXTURE}/original")
set(TOY3D_RELOCATED "${TOY3D_FIXTURE}/移动 游戏")
execute_process(COMMAND "${CMAKE_COMMAND}" "-DTOY3D_PROJECT=${TOY3D_PROJECT}"
    "-DTOY3D_BUILD=${TOY3D_BUILD}" "-DTOY3D_OUTPUT=${TOY3D_ORIGINAL}"
    "-DTOY3D_CONFIGURATION=${TOY3D_CONFIGURATION}" -DTOY3D_BUILD_EDITOR=OFF
    -P "${CMAKE_CURRENT_LIST_DIR}/package_project.cmake" RESULT_VARIABLE TOY3D_RESULT)
if(NOT TOY3D_RESULT EQUAL 0)
    message(FATAL_ERROR "Package integration build/cook/stage failed.")
endif()
# Both literal descendants belong to this random fixture; no source directory is moved.
get_filename_component(TOY3D_ORIGINAL_PARENT "${TOY3D_ORIGINAL}" DIRECTORY)
get_filename_component(TOY3D_RELOCATED_PARENT "${TOY3D_RELOCATED}" DIRECTORY)
if(NOT TOY3D_ORIGINAL_PARENT STREQUAL TOY3D_FIXTURE OR NOT TOY3D_RELOCATED_PARENT STREQUAL TOY3D_FIXTURE)
    message(FATAL_ERROR "Invalid package relocation fixture ownership.")
endif()
file(RENAME "${TOY3D_ORIGINAL}" "${TOY3D_RELOCATED}" NO_REPLACE)
file(GLOB TOY3D_GAME "${TOY3D_RELOCATED}/*Game.exe")
list(LENGTH TOY3D_GAME TOY3D_GAME_COUNT)
if(NOT TOY3D_GAME_COUNT EQUAL 1)
    message(FATAL_ERROR "Package must contain one Game executable.")
endif()
if(EXISTS "${TOY3D_RELOCATED}/Toy3dEditor.exe" OR EXISTS "${TOY3D_RELOCATED}/ShadowDemo.dll" OR
    EXISTS "${TOY3D_RELOCATED}/project/src" OR EXISTS "${TOY3D_RELOCATED}/project/shader" OR
    EXISTS "${TOY3D_RELOCATED}/project/saved")
    message(FATAL_ERROR "Package includes Editor or source/cache inputs.")
endif()
execute_process(COMMAND ${TOY3D_GAME} --Game.FrameLimit=8
    WORKING_DIRECTORY "${CMAKE_CURRENT_LIST_DIR}" RESULT_VARIABLE TOY3D_RESULT
    OUTPUT_VARIABLE TOY3D_OUTPUT ERROR_VARIABLE TOY3D_ERROR TIMEOUT 60)
if(NOT TOY3D_RESULT EQUAL 0 OR NOT TOY3D_OUTPUT MATCHES "Game completed 8 ticks")
    message(FATAL_ERROR "Relocated Game failed: ${TOY3D_RESULT}\n${TOY3D_OUTPUT}\n${TOY3D_ERROR}")
endif()
message(STATUS "Relocated Game completed 8 ticks from unrelated cwd: ${TOY3D_RELOCATED}")
file(READ "${TOY3D_RELOCATED}/game_package.ini" TOY3D_PACKAGE_BOOT)
file(WRITE "${TOY3D_RELOCATED}/game_package.ini" "[Package]\nFormatVersion=1\nDescriptor=../escape.toy\n")
execute_process(COMMAND ${TOY3D_GAME} --Game.FrameLimit=8 RESULT_VARIABLE TOY3D_RESULT
    OUTPUT_VARIABLE TOY3D_OUTPUT ERROR_VARIABLE TOY3D_ERROR TIMEOUT 20)
if(TOY3D_RESULT EQUAL 0 OR NOT TOY3D_ERROR MATCHES "dot segments")
    message(FATAL_ERROR "Package accepted descriptor escape: ${TOY3D_OUTPUT}\n${TOY3D_ERROR}")
endif()
file(WRITE "${TOY3D_RELOCATED}/game_package.ini" "${TOY3D_PACKAGE_BOOT}")
file(RENAME "${TOY3D_RELOCATED}/shader/player" "${TOY3D_RELOCATED}/shader/missing-player" NO_REPLACE)
execute_process(COMMAND ${TOY3D_GAME} --Game.FrameLimit=8 RESULT_VARIABLE TOY3D_RESULT
    OUTPUT_VARIABLE TOY3D_OUTPUT ERROR_VARIABLE TOY3D_ERROR TIMEOUT 30)
if(TOY3D_RESULT EQUAL 0)
    message(FATAL_ERROR "Package reused source/Saved Shader when Player deployment was missing.")
endif()
file(RENAME "${TOY3D_RELOCATED}/shader/missing-player" "${TOY3D_RELOCATED}/shader/player" NO_REPLACE)
file(WRITE "${TOY3D_RELOCATED}/keep.txt" "owned-marker")
execute_process(COMMAND "${CMAKE_COMMAND}" "-DTOY3D_PROJECT=${TOY3D_PROJECT}"
    "-DTOY3D_BUILD=${TOY3D_BUILD}" "-DTOY3D_OUTPUT=${TOY3D_RELOCATED}"
    -P "${CMAKE_CURRENT_LIST_DIR}/package_project.cmake" RESULT_VARIABLE TOY3D_RESULT
    OUTPUT_QUIET ERROR_QUIET)
file(READ "${TOY3D_RELOCATED}/keep.txt" TOY3D_MARKER)
if(TOY3D_RESULT EQUAL 0 OR NOT TOY3D_MARKER STREQUAL "owned-marker")
    message(FATAL_ERROR "Package overwrote an existing destination.")
endif()
message(STATUS "Package relocation and failure paths passed.")
