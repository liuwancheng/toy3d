cmake_minimum_required(VERSION 3.19 FATAL_ERROR)

project(toy3d VERSION 1.0.0 LANGUAGES CXX)

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

# Platform specific settings
if(WIN32)
  add_compile_definitions(WITH_WIN64)
  if(MSVC)
    add_compile_options("/MP")
    set_property(DIRECTORY ${CMAKE_SOURCE_DIR} PROPERTY VS_STARTUP_PROJECT Toy3dEditor)
  endif()
elseif(APPLE)
  add_compile_definitions(WITH_MACOS)
elseif(ANDROID)
  add_compile_definitions(WITH_ANDROID)
endif()

# Add subdirectories
add_subdirectory(thirdparty)
add_subdirectory(engine)
add_subdirectory(platform)
add_subdirectory(editor)
add_subdirectory(misc)

# Executable target
add_executable(toy3d main.cpp)
target_link_libraries(toy3d
  PRIVATE
    engine
    platform
    editor
    glfw
    glm_static
    tinyobjloader
    vma
)

# Install targets
install(TARGETS toy3d
  RUNTIME DESTINATION ${CMAKE_INSTALL_PREFIX}
)

# Editor target (if needed)
if(EXISTS "${TOY3D_ROOT_DIR}/editor/editor.cpp")
  add_executable(toy3d_editor
    editor/editor.cpp
    editor/editor.h
  )
  target_link_libraries(toy3d_editor
    PRIVATE
      engine
      platform
      editor
      glfw
      glm_static
      tinyobjloader
      vma
  )
  install(TARGETS toy3d_editor
    RUNTIME DESTINATION ${CMAKE_INSTALL_PREFIX}
  )
endif()