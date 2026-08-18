add_library(Toy3dFileSystem STATIC)
target_sources(Toy3dFileSystem
    PRIVATE
        "file_system/file_error.cpp"
        "file_system/file_error.h"
        "file_system/file_handle.h"
        "file_system/file_store.h"
        "file_system/file_system.cpp"
        "file_system/file_system.h"
        "file_system/directory_file_store.cpp"
        "file_system/directory_file_store.h"
        "file_system/native_platform_file.cpp"
        "file_system/native_platform_file.h"
        "file_system/physical_path.cpp"
        "file_system/physical_path.h"
        "file_system/platform_file.h"
        "file_system/store_path.cpp"
        "file_system/store_path.h"
        "file_system/virtual_path.cpp"
        "file_system/virtual_path.h")
target_compile_features(Toy3dFileSystem PUBLIC cxx_std_17)
set_target_properties(Toy3dFileSystem PROPERTIES
    CXX_EXTENSIONS OFF
    FOLDER "Engine/Core")
target_include_directories(Toy3dFileSystem
    PUBLIC
        "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>")

add_library(Toy3dLogging STATIC)
target_sources(Toy3dLogging
    PRIVATE
        "logging/logger.cpp"
        "logging/logger.h")
target_compile_features(Toy3dLogging PUBLIC cxx_std_17)
set_target_properties(Toy3dLogging PROPERTIES
    CXX_EXTENSIONS OFF
    FOLDER "Engine/Core")
target_include_directories(Toy3dLogging
    PUBLIC
        "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>")
target_link_libraries(Toy3dLogging PUBLIC spdlog)

add_library(Toy3dMath STATIC)
target_sources(Toy3dMath
    PRIVATE
        "math/math.cpp"
        "math/math.h"
        "math/random.h"
        "misc/enum_class_utils.h")
target_compile_features(Toy3dMath PUBLIC cxx_std_17)
set_target_properties(Toy3dMath PROPERTIES
    CXX_EXTENSIONS OFF
    FOLDER "Engine/Core")
target_include_directories(Toy3dMath
    PUBLIC
        "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>")
target_link_libraries(Toy3dMath PUBLIC glm::glm)

if(BUILD_TESTING)
    find_package(Threads REQUIRED)
    add_executable(Toy3dFileSystemTests "tests/file_system_tests.cpp")
    target_compile_features(Toy3dFileSystemTests PRIVATE cxx_std_17)
    set_target_properties(Toy3dFileSystemTests PROPERTIES
        CXX_EXTENSIONS OFF
        FOLDER "Engine/Tests")
    target_link_libraries(Toy3dFileSystemTests PRIVATE Toy3dFileSystem Threads::Threads)
    add_test(NAME Toy3dCore.FileSystem COMMAND Toy3dFileSystemTests)
endif()
