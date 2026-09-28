add_library(Toy3dText INTERFACE)
target_compile_features(Toy3dText INTERFACE cxx_std_17)
target_include_directories(Toy3dText
    INTERFACE
        "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>")

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
target_link_libraries(Toy3dFileSystem PUBLIC Toy3dText)

add_library(Toy3dReflection STATIC)
target_sources(Toy3dReflection
    PRIVATE
        "reflection/reflection_macros.h"
        "reflection/type_registry.cpp"
        "reflection/type_registry.h")
target_compile_features(Toy3dReflection PUBLIC cxx_std_17)
set_target_properties(Toy3dReflection PROPERTIES
    CXX_EXTENSIONS OFF
    FOLDER "Engine/Core")
target_include_directories(Toy3dReflection
    PUBLIC
        "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>")

add_library(Toy3dSerialization STATIC)
target_sources(Toy3dSerialization
    PRIVATE
        "serialization/math_value_codec.cpp"
        "serialization/math_value_codec.h"
        "serialization/schema_migration.cpp"
        "serialization/schema_migration.h"
        "serialization/value_codec.cpp"
        "serialization/value_codec.h")
target_compile_features(Toy3dSerialization PUBLIC cxx_std_17)
set_target_properties(Toy3dSerialization PROPERTIES
    CXX_EXTENSIONS OFF
    FOLDER "Engine/Core")
target_include_directories(Toy3dSerialization
    PUBLIC
        "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>")
target_link_libraries(Toy3dSerialization PUBLIC Toy3dReflection Toy3dText Toy3dMath)

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

add_library(Toy3dPixelFormat STATIC)
target_sources(Toy3dPixelFormat
    PRIVATE
        "pixel_format/pixel_format.cpp"
        "pixel_format/pixel_format.h")
target_compile_features(Toy3dPixelFormat PUBLIC cxx_std_17)
set_target_properties(Toy3dPixelFormat PROPERTIES
    CXX_EXTENSIONS OFF
    FOLDER "Engine/Core")
target_include_directories(Toy3dPixelFormat
    PUBLIC
        "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>")

add_library(Toy3dMath STATIC)
target_sources(Toy3dMath
    PRIVATE
        "math/angle.h"
        "math/geometry/convex_volume.cpp"
        "math/geometry/convex_volume.h"
        "math/geometry/plane.cpp"
        "math/geometry/plane.h"
        "math/integer_vector.h"
        "math/matrix3.cpp"
        "math/matrix3.h"
        "math/matrix_construction.cpp"
        "math/matrix_construction.h"
        "math/matrix4.cpp"
        "math/matrix4.h"
        "math/math.cpp"
        "math/math.h"
        "math/math_constants.h"
        "math/quaternion.cpp"
        "math/quaternion.h"
        "math/random.h"
        "math/scalar_math.h"
        "math/transform.cpp"
        "math/transform.h"
        "math/vector2.h"
        "math/vector3.h"
        "math/vector4.h"
        "misc/enum_class_utils.h")
target_compile_features(Toy3dMath PUBLIC cxx_std_17)
set_target_properties(Toy3dMath PROPERTIES
    CXX_EXTENSIONS OFF
    FOLDER "Engine/Core")
target_include_directories(Toy3dMath
    PUBLIC
        "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>")
target_link_libraries(Toy3dMath PUBLIC glm::glm)
if(WIN32)
    # Toy3dMath publicly declares min/max functions, so every Windows consumer
    # must suppress the conflicting windows.h function-like macros.
    target_compile_definitions(Toy3dMath PUBLIC NOMINMAX)
endif()

find_package(Threads REQUIRED)

add_library(Toy3dThreading STATIC)
target_sources(Toy3dThreading
    PRIVATE
        "threading/event.cpp"
        "threading/event.h"
        "threading/containers/bounded_mpmc_queue.h"
        "threading/containers/queue.h"
        "threading/runnable.h"
        "threading/runnable_thread.cpp"
        "threading/runnable_thread.h"
        "threading/thread.cpp"
        "threading/thread.h"
        "threading/thread_manager.cpp"
        "threading/thread_manager.h"
        "threading/threading_types.h")
target_compile_features(Toy3dThreading PUBLIC cxx_std_17)
set_target_properties(Toy3dThreading PROPERTIES
    CXX_EXTENSIONS OFF
    FOLDER "Engine/Core")
target_include_directories(Toy3dThreading
    PUBLIC
        "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>")
target_link_libraries(Toy3dThreading PRIVATE Threads::Threads)

add_library(Toy3dTaskGraph STATIC)
target_sources(Toy3dTaskGraph
    PRIVATE
        "task_graph/base_graph_task.cpp"
        "task_graph/base_graph_task.h"
        "task_graph/graph_event.cpp"
        "task_graph/graph_event.h"
        "task_graph/graph_task.cpp"
        "task_graph/graph_task.h"
        "task_graph/stalling_task_queue.cpp"
        "task_graph/stalling_task_queue.h"
        "task_graph/task_graph.cpp"
        "task_graph/task_graph.h"
        "task_graph/task_graph_interface.h"
        "task_graph/task_graph_types.h")
target_compile_features(Toy3dTaskGraph PUBLIC cxx_std_17)
set_target_properties(Toy3dTaskGraph PROPERTIES
    CXX_EXTENSIONS OFF
    FOLDER "Engine/Core")
target_include_directories(Toy3dTaskGraph
    PUBLIC
        "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>")
target_link_libraries(Toy3dTaskGraph PUBLIC Toy3dThreading)

add_subdirectory(asset)
add_subdirectory(mesh_description)
add_subdirectory(static_mesh)

if(BUILD_TESTING)
    add_executable(Toy3dSerializationTests "tests/serialization_tests.cpp")
    target_compile_features(Toy3dSerializationTests PRIVATE cxx_std_17)
    set_target_properties(Toy3dSerializationTests PROPERTIES
        CXX_EXTENSIONS OFF
        FOLDER "Engine/Tests")
    target_link_libraries(Toy3dSerializationTests PRIVATE Toy3dSerialization)
    add_test(NAME Toy3dCore.Serialization COMMAND Toy3dSerializationTests)

    add_executable(Toy3dTextTests "tests/text_tests.cpp")
    target_compile_features(Toy3dTextTests PRIVATE cxx_std_17)
    set_target_properties(Toy3dTextTests PROPERTIES
        CXX_EXTENSIONS OFF
        FOLDER "Engine/Tests")
    target_link_libraries(Toy3dTextTests PRIVATE Toy3dText)
    add_test(NAME Toy3dCore.Text COMMAND Toy3dTextTests)

    add_executable(Toy3dReflectionTests "tests/reflection_tests.cpp")
    target_compile_features(Toy3dReflectionTests PRIVATE cxx_std_17)
    set_target_properties(Toy3dReflectionTests PROPERTIES
        CXX_EXTENSIONS OFF
        FOLDER "Engine/Tests")
    target_link_libraries(Toy3dReflectionTests PRIVATE Toy3dReflection Threads::Threads)
    add_test(NAME Toy3dCore.Reflection COMMAND Toy3dReflectionTests)

    add_executable(Toy3dPixelFormatTests "tests/pixel_format_tests.cpp")
    target_compile_features(Toy3dPixelFormatTests PRIVATE cxx_std_17)
    set_target_properties(Toy3dPixelFormatTests PROPERTIES
        CXX_EXTENSIONS OFF
        FOLDER "Engine/Tests")
    target_link_libraries(Toy3dPixelFormatTests PRIVATE Toy3dPixelFormat)
    add_test(NAME Toy3dCore.PixelFormat COMMAND Toy3dPixelFormatTests)

    add_executable(Toy3dMathTests "tests/math_tests.cpp")
    target_compile_features(Toy3dMathTests PRIVATE cxx_std_17)
    set_target_properties(Toy3dMathTests PROPERTIES
        CXX_EXTENSIONS OFF
        FOLDER "Engine/Tests")
    target_link_libraries(Toy3dMathTests PRIVATE Toy3dMath)
    add_test(NAME Toy3dCore.Math COMMAND Toy3dMathTests)

    add_executable(Toy3dFileSystemTests "tests/file_system_tests.cpp")
    target_compile_features(Toy3dFileSystemTests PRIVATE cxx_std_17)
    set_target_properties(Toy3dFileSystemTests PROPERTIES
        CXX_EXTENSIONS OFF
        FOLDER "Engine/Tests")
    target_link_libraries(Toy3dFileSystemTests PRIVATE Toy3dFileSystem Threads::Threads)
    add_test(NAME Toy3dCore.FileSystem COMMAND Toy3dFileSystemTests)

    add_executable(Toy3dThreadingTests "tests/threading_tests.cpp")
    target_compile_features(Toy3dThreadingTests PRIVATE cxx_std_17)
    set_target_properties(Toy3dThreadingTests PROPERTIES
        CXX_EXTENSIONS OFF
        FOLDER "Engine/Tests")
    target_link_libraries(Toy3dThreadingTests PRIVATE Toy3dThreading Threads::Threads)
    add_test(NAME Toy3dCore.Threading COMMAND Toy3dThreadingTests)

    add_executable(Toy3dQueueTests "tests/queue_tests.cpp")
    target_compile_features(Toy3dQueueTests PRIVATE cxx_std_17)
    set_target_properties(Toy3dQueueTests PROPERTIES
        CXX_EXTENSIONS OFF
        FOLDER "Engine/Tests")
    target_link_libraries(Toy3dQueueTests PRIVATE Toy3dThreading Threads::Threads)
    add_test(NAME Toy3dCore.Queue COMMAND Toy3dQueueTests)

    add_executable(Toy3dTaskGraphTests "tests/task_graph_tests.cpp")
    target_compile_features(Toy3dTaskGraphTests PRIVATE cxx_std_17)
    set_target_properties(Toy3dTaskGraphTests PROPERTIES
        CXX_EXTENSIONS OFF
        FOLDER "Engine/Tests")
    target_link_libraries(Toy3dTaskGraphTests PRIVATE Toy3dTaskGraph Threads::Threads)
    add_test(NAME Toy3dCore.TaskGraph COMMAND Toy3dTaskGraphTests)

    add_executable(Toy3dTaskGraphSchedulerTests "tests/task_graph_scheduler_tests.cpp")
    target_compile_features(Toy3dTaskGraphSchedulerTests PRIVATE cxx_std_17)
    set_target_properties(Toy3dTaskGraphSchedulerTests PROPERTIES
        CXX_EXTENSIONS OFF
        FOLDER "Engine/Tests")
    target_link_libraries(Toy3dTaskGraphSchedulerTests PRIVATE Toy3dTaskGraph Threads::Threads)
    add_test(NAME Toy3dCore.TaskGraphScheduler COMMAND Toy3dTaskGraphSchedulerTests)
endif()
