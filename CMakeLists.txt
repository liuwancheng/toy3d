cmake_minimum_required(VERSION 3.19 FATAL_ERROR)

project(toy3d VERSION 1.0.0)

if(APPLE)
  enable_language(OBJC OBJCXX)
endif()


set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(BUILD_SHARED_LIBS OFF)

include(CMakeDependentOption)
include(CTest)
option(TOY3D_SHADER_EDITORONLY_DATA "Generate and load optional Shader Editor property descriptions" ON)

# ---- Include guards ----
if(PROJECT_SOURCE_DIR STREQUAL PROJECT_BINARY_DIR)
  message(
    FATAL_ERROR
      "In-source builds not allowed. Please make a new directory (called a build directory) and run CMake from there."
  )
endif()

set(TOY3D_ROOT_DIR "${CMAKE_CURRENT_SOURCE_DIR}")
set(CMAKE_INSTALL_PREFIX "${TOY3D_ROOT_DIR}/bin")
set(BINARY_ROOT_DIR "${CMAKE_INSTALL_PREFIX}/")
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/host")
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/host")

# One process must own one Core/Assets/Runtime state across project DLLs.
function(toy3d_shared_library target)
    set_target_properties(${target} PROPERTIES WINDOWS_EXPORT_ALL_SYMBOLS ON POSITION_INDEPENDENT_CODE ON)
    add_custom_command(TARGET ${target} POST_BUILD
        COMMAND "${CMAKE_COMMAND}" -E make_directory "${BINARY_ROOT_DIR}"
        COMMAND "${CMAKE_COMMAND}" -E copy_if_different "$<TARGET_FILE:${target}>" "${BINARY_ROOT_DIR}"
        VERBATIM)
endfunction()


# Platform selectors are provided by core/platform/platform_defines.h rather
# than directory-wide compiler definitions (including third-party targets).
if(NOT (WIN32 OR ANDROID OR CMAKE_SYSTEM_NAME STREQUAL "Darwin"
        OR CMAKE_SYSTEM_NAME STREQUAL "iOS" OR CMAKE_SYSTEM_NAME STREQUAL "Linux"))
    message(FATAL_ERROR "Unsupported Toy3d target OS: ${CMAKE_SYSTEM_NAME}")
endif()

add_subdirectory(engine)

set(TOY3D_GAME_PROJECT "${TOY3D_ROOT_DIR}/project" CACHE PATH "Explicit C++ game project directory; empty builds the engine only")
if(TOY3D_GAME_PROJECT AND TARGET Toy3dEditorCore)
    if(NOT EXISTS "${TOY3D_GAME_PROJECT}/CMakeLists.txt")
        message(FATAL_ERROR "The selected game project has no CMakeLists.txt")
    endif()
    include("${TOY3D_ROOT_DIR}/engine/build/cmake/game_project.cmake")
    add_subdirectory("${TOY3D_GAME_PROJECT}" "${CMAKE_BINARY_DIR}/game_project")
endif()

if(TARGET Toy3dEditor)
    add_executable(Toy3dGame "engine/runtime/application/game_main.cpp"
        "engine/runtime/application/generic_game_main.cpp" "engine/runtime/application/host_entry.cpp")
    target_link_libraries(Toy3dGame PRIVATE Toy3dRuntime)
    target_compile_features(Toy3dGame PRIVATE cxx_std_17)
    target_compile_options(Toy3dGame PRIVATE "$<$<COMPILE_LANG_AND_ID:CXX,MSVC>:/utf-8>")
    set_target_properties(Toy3dGame PROPERTIES CXX_EXTENSIONS OFF)
    if(WIN32)
        set_target_properties(Toy3dGame PROPERTIES WIN32_EXECUTABLE ON)
        target_link_libraries(Toy3dGame PRIVATE shell32)
    endif()
    target_compile_definitions(Toy3dGame PRIVATE
        "TOY3D_PROJECT_DESCRIPTOR=\"\"" "TOY3D_EDITOR_DEPLOY_ROOT=\"${BINARY_ROOT_DIR}\""
        "TOY3D_EDITOR_ENGINE_ASSET_ROOT=\"${TOY3D_ROOT_DIR}/engine/asset\""
        "TOY3D_EDITOR_ENGINE_CONFIG_ROOT=\"${TOY3D_ROOT_DIR}/engine/config\""
        "TOY3D_GAME_SHADER_ROOT=\"${BINARY_ROOT_DIR}shader/player\"")
    add_custom_command(TARGET Toy3dGame POST_BUILD COMMAND "${CMAKE_COMMAND}" -E copy_if_different
        "$<TARGET_FILE:Toy3dGame>" "${BINARY_ROOT_DIR}" VERBATIM)
    add_dependencies(Toy3dEditor Toy3dGame)
    get_property(TOY3D_PACKAGE_GAME GLOBAL PROPERTY TOY3D_GAME_HOST_TARGET)
    if(NOT TOY3D_PACKAGE_GAME)
        set(TOY3D_PACKAGE_GAME Toy3dGame)
    endif()
    get_property(TOY3D_PACKAGE_MODULE GLOBAL PROPERTY TOY3D_GAME_MODULE_TARGET)
    file(GENERATE OUTPUT "${CMAKE_BINARY_DIR}/project-package/$<CONFIG>.json" CONTENT
        "{\"editor\":\"$<TARGET_FILE:Toy3dEditor>\",\"game\":\"$<TARGET_FILE:${TOY3D_PACKAGE_GAME}>\",\"game_target\":\"${TOY3D_PACKAGE_GAME}\",\"module_target\":\"${TOY3D_PACKAGE_MODULE}\",\"compiler\":\"$<TARGET_FILE:Toy3dShaderCompiler>\",\"toolchain\":\"${TOY3D_SHADER_TOOLCHAIN_ROOT}\",\"linker\":\"${CMAKE_LINKER}\",\"configuration\":\"$<CONFIG>\"}\n")
endif()
