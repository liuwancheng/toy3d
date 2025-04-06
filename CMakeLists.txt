cmake_minimum_required(VERSION 3.19 FATAL_ERROR)

project(toy3d VERSION 1.0.0)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(BUILD_SHARED_LIBS OFF)

include(CMakeDependentOption)

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


if(WIN32)
    add_compile_definitions(WITH_WIN64=1)
elseif(UNIX)
    if(APPLE)
        add_compile_definitions(WITH_MAC=1)
    else()
        add_compile_definitions(WITH_LINUX=1)
    endif()
elseif(ANDROID)
    add_compile_definitions(WITH_ANDROID=1)
elseif(IOS)
    add_compile_definitions(WITH_IOS=1)
else()
    message(FATAL_ERROR "unknown platform")
endif()

add_subdirectory(engine)