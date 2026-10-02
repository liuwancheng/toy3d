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


# Platform selectors are provided by core/platform/platform_defines.h rather
# than directory-wide compiler definitions (including third-party targets).
if(NOT (WIN32 OR ANDROID OR CMAKE_SYSTEM_NAME STREQUAL "Darwin"
        OR CMAKE_SYSTEM_NAME STREQUAL "iOS" OR CMAKE_SYSTEM_NAME STREQUAL "Linux"))
    message(FATAL_ERROR "Unsupported Toy3d target OS: ${CMAKE_SYSTEM_NAME}")
endif()

add_subdirectory(engine)
