find_package(Threads REQUIRED)

add_library(Toy3dCore STATIC)
target_sources(Toy3dCore PRIVATE
    "file_system/directory_file_store.cpp"
    "file_system/directory_file_store.h"
    "file_system/file_error.cpp"
    "file_system/file_error.h"
    "file_system/file_handle.h"
    "file_system/file_store.h"
    "file_system/file_system.cpp"
    "file_system/file_system.h"
    "file_system/native_platform_file.cpp"
    "file_system/native_platform_file.h"
    "file_system/physical_path.cpp"
    "file_system/physical_path.h"
    "file_system/platform_file.h"
    "file_system/store_path.cpp"
    "file_system/store_path.h"
    "file_system/virtual_path.cpp"
    "file_system/virtual_path.h"
    "image/pixel_format.cpp"
    "image/pixel_format.h"
    "image/png_codec.cpp"
    "image/png_codec.h"
    "logging/logger.cpp"
    "logging/logger.h"
    "logging/log_buffer.cpp"
    "logging/log_buffer.h"
    "math/angle.h"
    "math/geometry/convex_volume.cpp"
    "math/geometry/convex_volume.h"
    "math/geometry/plane.cpp"
    "math/geometry/plane.h"
    "math/integer_vector.h"
    "math/length_units.h"
    "math/math.cpp"
    "math/math.h"
    "math/math_constants.h"
    "math/matrix3.cpp"
    "math/matrix3.h"
    "math/matrix4.cpp"
    "math/matrix4.h"
    "math/matrix_construction.cpp"
    "math/matrix_construction.h"
    "math/quaternion.cpp"
    "math/quaternion.h"
    "math/random.h"
    "math/scalar_math.h"
    "math/transform.cpp"
    "math/transform.h"
    "math/vector2.h"
    "math/vector3.h"
    "math/vector4.h"
    "misc/enum_class_utils.h"
    "misc/sha256.cpp"
    "misc/sha256.h"
    "misc/utf8.h"
    "platform/platform_defines.h"
    "platform/platform_services.cpp"
    "platform/platform_services.h"
    "reflection/reflection_macros.h"
    "reflection/type_registry.cpp"
    "reflection/type_registry.h"
    "serialization/math_value_codec.cpp"
    "serialization/math_value_codec.h"
    "serialization/schema_migration.cpp"
    "serialization/schema_migration.h"
    "serialization/value_codec.cpp"
    "serialization/value_codec.h"
    "threading/containers/bounded_mpmc_queue.h"
    "threading/containers/queue.h"
    "threading/event.cpp"
    "threading/event.h"
    "threading/runnable.h"
    "threading/runnable_thread.cpp"
    "threading/runnable_thread.h"
    "threading/task_graph/base_graph_task.cpp"
    "threading/task_graph/base_graph_task.h"
    "threading/task_graph/graph_event.cpp"
    "threading/task_graph/graph_event.h"
    "threading/task_graph/graph_task.cpp"
    "threading/task_graph/graph_task.h"
    "threading/task_graph/stalling_task_queue.cpp"
    "threading/task_graph/stalling_task_queue.h"
    "threading/task_graph/task_graph.cpp"
    "threading/task_graph/task_graph.h"
    "threading/task_graph/task_graph_interface.h"
    "threading/task_graph/task_graph_types.h"
    "threading/thread.cpp"
    "threading/thread.h"
    "threading/thread_manager.cpp"
    "threading/thread_manager.h"
    "threading/threading_types.h"
)
target_compile_features(Toy3dCore PUBLIC cxx_std_17)
set_target_properties(Toy3dCore PROPERTIES CXX_EXTENSIONS OFF FOLDER "Engine/Core")
target_include_directories(Toy3dCore PUBLIC "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>"
    PRIVATE "${THIRD_PARTY_DIR}/stb")
target_link_libraries(Toy3dCore PUBLIC glm::glm spdlog PRIVATE Threads::Threads)

if(BUILD_TESTING)
    add_executable(Toy3dLoggingTests "tests/logging_tests.cpp")
    target_compile_features(Toy3dLoggingTests PRIVATE cxx_std_17)
    set_target_properties(Toy3dLoggingTests PROPERTIES CXX_EXTENSIONS OFF FOLDER "Tests/Core")
    target_link_libraries(Toy3dLoggingTests PRIVATE Toy3dCore Threads::Threads)
    target_compile_definitions(Toy3dLoggingTests PRIVATE
        "TOY3D_LOGGING_TEST_ROOT=\"${CMAKE_CURRENT_BINARY_DIR}/logging-test-output\"")
    target_compile_options(Toy3dLoggingTests PRIVATE "$<$<COMPILE_LANG_AND_ID:CXX,MSVC>:/utf-8>")
    add_test(NAME Toy3dCore.Logging COMMAND Toy3dLoggingTests)
    set_tests_properties(Toy3dCore.Logging PROPERTIES TIMEOUT 20)
endif()
target_compile_options(Toy3dCore PRIVATE "$<$<COMPILE_LANG_AND_ID:CXX,MSVC>:/utf-8>")
if(WIN32)
    target_compile_definitions(Toy3dCore PUBLIC NOMINMAX)
    target_link_libraries(Toy3dCore PRIVATE shell32 ole32)
endif()

add_library(Toy3dShaderFormat STATIC)
target_sources(Toy3dShaderFormat PRIVATE
    "shader/builtin_shader_parameters.h"
    "shader/shader_binding_identity.h"
    "shader/shader_editor_properties.cpp"
    "shader/shader_editor_properties.h"
    "shader/shader_format_hash.cpp"
    "shader/shader_format_types.h"
    "shader/shader_map_entry.h"
    "shader/shader_map_entry_reader.cpp"
    "shader/shader_parameter_schema.cpp"
)
target_compile_features(Toy3dShaderFormat PUBLIC cxx_std_17)
set_target_properties(Toy3dShaderFormat PROPERTIES CXX_EXTENSIONS OFF FOLDER "Engine/Core")
target_link_libraries(Toy3dShaderFormat PUBLIC Toy3dCore)
target_compile_definitions(Toy3dShaderFormat PRIVATE WITH_EDITORONLY_DATA=$<BOOL:${TOY3D_SHADER_EDITORONLY_DATA}>)

add_library(Toy3dAssets STATIC)
target_sources(Toy3dAssets PRIVATE
    "asset/asset_catalog.cpp"
    "asset/asset_catalog.h"
    "asset/asset_descriptor_path.cpp"
    "asset/asset_descriptor_path.h"
    "asset/asset_file.cpp"
    "asset/asset_file.h"
    "asset/asset_format_migration.cpp"
    "asset/asset_identity.cpp"
    "asset/asset_identity.h"
    "asset/asset_index.cpp"
    "asset/asset_index.h"
    "asset/asset_meta.cpp"
    "asset/asset_meta.h"
    "asset/asset_pair.cpp"
    "asset/asset_pair.h"
    "asset/asset_pair_store.cpp"
    "asset/asset_pair_store.h"
    "asset/asset_value_codec.cpp"
    "asset/game_project.cpp"
    "asset/game_project.h"
    "asset/asset_yaml.cpp"
    "asset/asset_yaml.h"
    "asset/edit_session.h"
    "asset/material/material_asset.cpp"
    "asset/material/material_asset.h"
    "asset/material/material_asset_data.h"
    "asset/mesh/mesh_description.cpp"
    "asset/mesh/mesh_description.h"
    "asset/mesh/static_mesh_asset.cpp"
    "asset/mesh/static_mesh_asset.h"
    "asset/mesh/static_mesh_asset_data.h"
    "asset/property_path.cpp"
    "asset/property_path.h"
    "asset/scene/component_settings.cpp"
    "asset/scene/component_settings.h"
    "asset/scene/scene_asset.cpp"
    "asset/scene/scene_asset.h"
    "asset/scene/scene_asset_data.h"
    "asset/texture/texture_asset.cpp"
    "asset/texture/texture_asset.h"
    "asset/texture/texture_asset_data.h"
    "asset/thumbnail/asset_thumbnail.cpp"
    "asset/thumbnail/asset_thumbnail.h"
)
target_compile_features(Toy3dAssets PUBLIC cxx_std_17)
set_target_properties(Toy3dAssets PROPERTIES CXX_EXTENSIONS OFF FOLDER "Engine/Core")
target_link_libraries(Toy3dAssets PUBLIC Toy3dCore Toy3dShaderFormat PRIVATE yaml-cpp Threads::Threads)
set(TOY3D_ASSET_GENERATED_DIR "${CMAKE_CURRENT_BINARY_DIR}/generated")
target_include_directories(Toy3dAssets PUBLIC "$<BUILD_INTERFACE:${TOY3D_ASSET_GENERATED_DIR}>")

# Reflection registration is generated by the standalone scanner, never by Runtime.
set(TOY3D_GENERATED_HEADER "${TOY3D_ASSET_GENERATED_DIR}/material_asset_reflection.h")
set(TOY3D_GENERATED_SOURCE "${TOY3D_ASSET_GENERATED_DIR}/material_asset_reflection.cpp")
add_custom_command(
    OUTPUT "${TOY3D_GENERATED_HEADER}" "${TOY3D_GENERATED_SOURCE}"
    COMMAND "${CMAKE_COMMAND}" -E make_directory "${TOY3D_ASSET_GENERATED_DIR}"
    COMMAND $<TARGET_FILE:Toy3dReflectionCodegen>
        --input "${CMAKE_CURRENT_SOURCE_DIR}/asset/material/material_asset_data.h"
        --header "${TOY3D_GENERATED_HEADER}" --source "${TOY3D_GENERATED_SOURCE}"
        --function register_material_asset_types
    DEPENDS Toy3dReflectionCodegen
        "${CMAKE_CURRENT_SOURCE_DIR}/asset/material/material_asset_data.h"
    VERBATIM)
target_sources(Toy3dAssets PRIVATE "${TOY3D_GENERATED_HEADER}" "${TOY3D_GENERATED_SOURCE}")

set(TOY3D_GENERATED_HEADER "${TOY3D_ASSET_GENERATED_DIR}/static_mesh_reflection.h")
set(TOY3D_GENERATED_SOURCE "${TOY3D_ASSET_GENERATED_DIR}/static_mesh_reflection.cpp")
add_custom_command(
    OUTPUT "${TOY3D_GENERATED_HEADER}" "${TOY3D_GENERATED_SOURCE}"
    COMMAND "${CMAKE_COMMAND}" -E make_directory "${TOY3D_ASSET_GENERATED_DIR}"
    COMMAND $<TARGET_FILE:Toy3dReflectionCodegen>
        --input "${CMAKE_CURRENT_SOURCE_DIR}/asset/mesh/static_mesh_asset_data.h"
        --header "${TOY3D_GENERATED_HEADER}" --source "${TOY3D_GENERATED_SOURCE}"
        --function register_static_mesh_asset_types
    DEPENDS Toy3dReflectionCodegen
        "${CMAKE_CURRENT_SOURCE_DIR}/asset/mesh/static_mesh_asset_data.h"
    VERBATIM)
target_sources(Toy3dAssets PRIVATE "${TOY3D_GENERATED_HEADER}" "${TOY3D_GENERATED_SOURCE}")

set(TOY3D_GENERATED_HEADER "${TOY3D_ASSET_GENERATED_DIR}/texture_asset_reflection.h")
set(TOY3D_GENERATED_SOURCE "${TOY3D_ASSET_GENERATED_DIR}/texture_asset_reflection.cpp")
add_custom_command(
    OUTPUT "${TOY3D_GENERATED_HEADER}" "${TOY3D_GENERATED_SOURCE}"
    COMMAND "${CMAKE_COMMAND}" -E make_directory "${TOY3D_ASSET_GENERATED_DIR}"
    COMMAND $<TARGET_FILE:Toy3dReflectionCodegen>
        --input "${CMAKE_CURRENT_SOURCE_DIR}/asset/texture/texture_asset_data.h"
        --header "${TOY3D_GENERATED_HEADER}" --source "${TOY3D_GENERATED_SOURCE}"
        --function register_texture_asset_types
    DEPENDS Toy3dReflectionCodegen
        "${CMAKE_CURRENT_SOURCE_DIR}/asset/texture/texture_asset_data.h"
    VERBATIM)
target_sources(Toy3dAssets PRIVATE "${TOY3D_GENERATED_HEADER}" "${TOY3D_GENERATED_SOURCE}")

set(TOY3D_GENERATED_HEADER "${TOY3D_ASSET_GENERATED_DIR}/scene_asset_reflection.h")
set(TOY3D_GENERATED_SOURCE "${TOY3D_ASSET_GENERATED_DIR}/scene_asset_reflection.cpp")
add_custom_command(
    OUTPUT "${TOY3D_GENERATED_HEADER}" "${TOY3D_GENERATED_SOURCE}"
    COMMAND "${CMAKE_COMMAND}" -E make_directory "${TOY3D_ASSET_GENERATED_DIR}"
    COMMAND $<TARGET_FILE:Toy3dReflectionCodegen>
        --input "${CMAKE_CURRENT_SOURCE_DIR}/asset/scene/component_settings.h"
        --input "${CMAKE_CURRENT_SOURCE_DIR}/asset/scene/scene_asset_data.h"
        --header "${TOY3D_GENERATED_HEADER}" --source "${TOY3D_GENERATED_SOURCE}"
        --function register_scene_asset_types
    DEPENDS Toy3dReflectionCodegen
        "${CMAKE_CURRENT_SOURCE_DIR}/asset/scene/component_settings.h"
        "${CMAKE_CURRENT_SOURCE_DIR}/asset/scene/scene_asset_data.h"
    VERBATIM)
target_sources(Toy3dAssets PRIVATE "${TOY3D_GENERATED_HEADER}" "${TOY3D_GENERATED_SOURCE}")

if(BUILD_TESTING)
    add_executable(Toy3dSerializationTests "tests/serialization_tests.cpp")
    target_compile_features(Toy3dSerializationTests PRIVATE cxx_std_17)
    set_target_properties(Toy3dSerializationTests PROPERTIES
        CXX_EXTENSIONS OFF
        FOLDER "Engine/Tests")
    target_link_libraries(Toy3dSerializationTests PRIVATE Toy3dCore)
    add_test(NAME Toy3dCore.Serialization COMMAND Toy3dSerializationTests)

    add_executable(Toy3dTextTests "tests/text_tests.cpp")
    target_compile_features(Toy3dTextTests PRIVATE cxx_std_17)
    set_target_properties(Toy3dTextTests PROPERTIES
        CXX_EXTENSIONS OFF
        FOLDER "Engine/Tests")
    target_link_libraries(Toy3dTextTests PRIVATE Toy3dCore)
    add_test(NAME Toy3dCore.Text COMMAND Toy3dTextTests)

    add_executable(Toy3dReflectionTests "tests/reflection_tests.cpp")
    target_compile_features(Toy3dReflectionTests PRIVATE cxx_std_17)
    set_target_properties(Toy3dReflectionTests PROPERTIES
        CXX_EXTENSIONS OFF
        FOLDER "Engine/Tests")
    target_link_libraries(Toy3dReflectionTests PRIVATE Toy3dCore Threads::Threads)
    add_test(NAME Toy3dCore.Reflection COMMAND Toy3dReflectionTests)

    add_executable(Toy3dPixelFormatTests "tests/pixel_format_tests.cpp")
    target_compile_features(Toy3dPixelFormatTests PRIVATE cxx_std_17)
    set_target_properties(Toy3dPixelFormatTests PROPERTIES
        CXX_EXTENSIONS OFF
        FOLDER "Engine/Tests")
    target_link_libraries(Toy3dPixelFormatTests PRIVATE Toy3dCore)
    add_test(NAME Toy3dCore.PixelFormat COMMAND Toy3dPixelFormatTests)

    add_executable(Toy3dMathTests "tests/math_tests.cpp")
    target_compile_features(Toy3dMathTests PRIVATE cxx_std_17)
    set_target_properties(Toy3dMathTests PROPERTIES
        CXX_EXTENSIONS OFF
        FOLDER "Engine/Tests")
    target_link_libraries(Toy3dMathTests PRIVATE Toy3dCore)
    add_test(NAME Toy3dCore.Math COMMAND Toy3dMathTests)

    add_executable(Toy3dFileSystemTests "tests/file_system_tests.cpp")
    target_compile_features(Toy3dFileSystemTests PRIVATE cxx_std_17)
    set_target_properties(Toy3dFileSystemTests PROPERTIES
        CXX_EXTENSIONS OFF
        FOLDER "Engine/Tests")
    target_link_libraries(Toy3dFileSystemTests PRIVATE Toy3dCore Threads::Threads)
    add_test(NAME Toy3dCore.FileSystem COMMAND Toy3dFileSystemTests)

    add_executable(Toy3dThreadingTests "tests/threading_tests.cpp")
    target_compile_features(Toy3dThreadingTests PRIVATE cxx_std_17)
    set_target_properties(Toy3dThreadingTests PROPERTIES
        CXX_EXTENSIONS OFF
        FOLDER "Engine/Tests")
    target_link_libraries(Toy3dThreadingTests PRIVATE Toy3dCore Threads::Threads)
    add_test(NAME Toy3dCore.Threading COMMAND Toy3dThreadingTests)

    add_executable(Toy3dQueueTests "tests/queue_tests.cpp")
    target_compile_features(Toy3dQueueTests PRIVATE cxx_std_17)
    set_target_properties(Toy3dQueueTests PROPERTIES
        CXX_EXTENSIONS OFF
        FOLDER "Engine/Tests")
    target_link_libraries(Toy3dQueueTests PRIVATE Toy3dCore Threads::Threads)
    add_test(NAME Toy3dCore.Queue COMMAND Toy3dQueueTests)

    add_executable(Toy3dTaskGraphTests "tests/task_graph_tests.cpp")
    target_compile_features(Toy3dTaskGraphTests PRIVATE cxx_std_17)
    set_target_properties(Toy3dTaskGraphTests PROPERTIES
        CXX_EXTENSIONS OFF
        FOLDER "Engine/Tests")
    target_link_libraries(Toy3dTaskGraphTests PRIVATE Toy3dCore Threads::Threads)
    add_test(NAME Toy3dCore.TaskGraph COMMAND Toy3dTaskGraphTests)

    add_executable(Toy3dTaskGraphSchedulerTests "tests/task_graph_scheduler_tests.cpp")
    target_compile_features(Toy3dTaskGraphSchedulerTests PRIVATE cxx_std_17)
    set_target_properties(Toy3dTaskGraphSchedulerTests PROPERTIES
        CXX_EXTENSIONS OFF
        FOLDER "Engine/Tests")
    target_link_libraries(Toy3dTaskGraphSchedulerTests PRIVATE Toy3dCore Threads::Threads)
    add_test(NAME Toy3dCore.TaskGraphScheduler COMMAND Toy3dTaskGraphSchedulerTests)
endif()
if(BUILD_TESTING)
    add_executable(Toy3dAssetFileTests "tests/asset_file_tests.cpp")
    target_compile_features(Toy3dAssetFileTests PRIVATE cxx_std_17)
    set_target_properties(Toy3dAssetFileTests PROPERTIES CXX_EXTENSIONS OFF FOLDER "Engine/Tests")
    target_link_libraries(Toy3dAssetFileTests PRIVATE Toy3dAssets)
    add_test(NAME Toy3dAssets.AssetFile COMMAND Toy3dAssetFileTests)
endif()

if(BUILD_TESTING)
    add_executable(Toy3dAssetThumbnailTests "tests/asset_thumbnail_tests.cpp")
    target_compile_features(Toy3dAssetThumbnailTests PRIVATE cxx_std_17)
    set_target_properties(Toy3dAssetThumbnailTests PROPERTIES CXX_EXTENSIONS OFF FOLDER "Engine/Tests")
    target_link_libraries(Toy3dAssetThumbnailTests PRIVATE Toy3dAssets Toy3dCore)
    add_test(NAME Toy3dAsset.Thumbnail COMMAND Toy3dAssetThumbnailTests)
endif()

if(BUILD_TESTING)
    add_executable(Toy3dMaterialAssetTests "tests/material_asset_tests.cpp")
    target_compile_features(Toy3dMaterialAssetTests PRIVATE cxx_std_17)
    set_target_properties(Toy3dMaterialAssetTests PROPERTIES CXX_EXTENSIONS OFF FOLDER "Tests/Core")
    target_link_libraries(Toy3dMaterialAssetTests PRIVATE Toy3dAssets)
    add_test(NAME Toy3dCore.MaterialAsset COMMAND Toy3dMaterialAssetTests)
endif()

if(BUILD_TESTING)
    add_executable(Toy3dProcessHelper "tests/process_helper.cpp")
    add_executable(Toy3dProcessTests "tests/process_tests.cpp")
    set(TOY3D_PROCESS_TEST_TARGETS Toy3dProcessHelper Toy3dProcessTests)
    if(WIN32)
        add_executable(Toy3dWindowsProcessCleanupTests "tests/windows_cleanup_tests.cpp")
        target_link_libraries(Toy3dWindowsProcessCleanupTests PRIVATE Toy3dCore)
        list(APPEND TOY3D_PROCESS_TEST_TARGETS Toy3dWindowsProcessCleanupTests)
        add_test(NAME Toy3dCore.WindowsProcessCleanup
            COMMAND Toy3dWindowsProcessCleanupTests "$<TARGET_FILE:Toy3dProcessHelper>")
        set_tests_properties(Toy3dCore.WindowsProcessCleanup PROPERTIES TIMEOUT 15)
    endif()
    foreach(TOY3D_PROCESS_TEST_TARGET IN LISTS TOY3D_PROCESS_TEST_TARGETS)
        target_link_libraries(${TOY3D_PROCESS_TEST_TARGET} PRIVATE Toy3dCore)
        target_compile_features(${TOY3D_PROCESS_TEST_TARGET} PRIVATE cxx_std_17)
        set_target_properties(${TOY3D_PROCESS_TEST_TARGET} PROPERTIES CXX_EXTENSIONS OFF FOLDER "Engine/Tests")
        if(MSVC)
            target_compile_options(${TOY3D_PROCESS_TEST_TARGET} PRIVATE /utf-8)
        endif()
    endforeach()
    target_link_libraries(Toy3dProcessTests PRIVATE Toy3dCore)
    add_dependencies(Toy3dProcessTests Toy3dProcessHelper)
    if(WIN32)
        add_dependencies(Toy3dWindowsProcessCleanupTests Toy3dProcessHelper)
    endif()
    add_test(NAME Toy3dCore.Process COMMAND Toy3dProcessTests "$<TARGET_FILE:Toy3dProcessHelper>")
    set_tests_properties(Toy3dCore.Process PROPERTIES TIMEOUT 20)
endif()
