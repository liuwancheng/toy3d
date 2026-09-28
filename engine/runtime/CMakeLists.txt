set(TARGET_NAME Toy3dRuntime)

option(TOY3D_ENABLE_VULKAN_RHI "Build the Vulkan RHI backend" OFF)
option(TOY3D_ENABLE_SHADER_MAP_ENTRY_LOADING
    "Build the development ShaderMapEntry loading path" ON)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
 
file(GLOB_RECURSE ALL_HEADER_FILES CONFIGURE_DEPENDS "*.h")
file(GLOB_RECURSE ALL_SOURCE_FILES CONFIGURE_DEPENDS "*.cpp")

list(FILTER ALL_HEADER_FILES EXCLUDE REGEX ".*/tests/.*\\.h$")
list(FILTER ALL_SOURCE_FILES EXCLUDE REGEX ".*/tests/.*\\.cpp$")

# Vulkan sources are registered explicitly below so backend code is only built
# when TOY3D_ENABLE_VULKAN_RHI is enabled.
list(FILTER ALL_HEADER_FILES EXCLUDE REGEX ".*/drivers/vulkan/.*\\.h$")
list(FILTER ALL_SOURCE_FILES EXCLUDE REGEX ".*/drivers/vulkan/.*\\.cpp$")
list(FILTER ALL_SOURCE_FILES EXCLUDE REGEX ".*/drivers/example/.*\\.cpp$")
list(FILTER ALL_SOURCE_FILES EXCLUDE REGEX ".*/shader/loaders/shader_map_entry_loader\\.cpp$")

# 排除特定平台文件夹
list(FILTER ALL_HEADER_FILES EXCLUDE REGEX ".*/platform/(android|mac|win)/.*\\.h$")
list(FILTER ALL_SOURCE_FILES EXCLUDE REGEX ".*/platform/(android|mac|win)/.*\\.cpp$")


# 添加当前平台特定的源文件
if(APPLE)
    file(GLOB_RECURSE PLATFORM_HEADER_FILES 
         "${CMAKE_CURRENT_SOURCE_DIR}/platform/mac/*.h")
    file(GLOB_RECURSE PLATFORM_SOURCE_FILES 
         "${CMAKE_CURRENT_SOURCE_DIR}/platform/mac/*.cpp"
         "${CMAKE_CURRENT_SOURCE_DIR}/platform/mac/*.mm")
elseif(WIN32)
    file(GLOB_RECURSE PLATFORM_HEADER_FILES 
        "${CMAKE_CURRENT_SOURCE_DIR}/platform/win/*.h")
    file(GLOB_RECURSE PLATFORM_SOURCE_FILES 
        "${CMAKE_CURRENT_SOURCE_DIR}/platform/win/*.cpp")
elseif(ANDROID)
    file(GLOB_RECURSE PLATFORM_HEADER_FILES 
        "${CMAKE_CURRENT_SOURCE_DIR}/platform/android/*.h")
    file(GLOB_RECURSE PLATFORM_SOURCE_FILES 
        "${CMAKE_CURRENT_SOURCE_DIR}/platform/android/*.cpp")
endif()

# 合并源文件
list(REMOVE_ITEM ALL_HEADER_FILES "${CMAKE_CURRENT_SOURCE_DIR}/gamescene/actor/light_actor.h"
    "${CMAKE_CURRENT_SOURCE_DIR}/gamescene/actor/camera_actor.h"
    "${CMAKE_CURRENT_SOURCE_DIR}/rendercore/scene/light_scene_proxy.h")
list(REMOVE_ITEM ALL_SOURCE_FILES "${CMAKE_CURRENT_SOURCE_DIR}/gamescene/actor/light_actor.cpp")
list(REMOVE_ITEM ALL_SOURCE_FILES "${CMAKE_CURRENT_SOURCE_DIR}/gamescene/actor/camera_actor.cpp")
set(HEADER_FILES ${ALL_HEADER_FILES} ${PLATFORM_HEADER_FILES})
set(SOURCE_FILES ${ALL_SOURCE_FILES} ${PLATFORM_SOURCE_FILES})

# message(STATUS \"HEADER_FILES:\")
# foreach(file ${HEADER_FILES})
#     message(STATUS \"  ${file}\")
# endforeach()

source_group(TREE "${CMAKE_CURRENT_SOURCE_DIR}" FILES ${HEADER_FILES} ${SOURCE_FILES})
add_library(${TARGET_NAME} ${HEADER_FILES} ${SOURCE_FILES})
target_link_libraries(${TARGET_NAME} PUBLIC Toy3dStaticMeshAsset)
target_sources(${TARGET_NAME} PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/gamescene/actor/camera_actor.h"
    "${CMAKE_CURRENT_SOURCE_DIR}/gamescene/actor/camera_actor.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/gamescene/actor/light_actor.h"
    "${CMAKE_CURRENT_SOURCE_DIR}/gamescene/actor/light_actor.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/rendercore/scene/light_scene_proxy.h")
add_dependencies(${TARGET_NAME} Toy3dShaders)
add_dependencies(${TARGET_NAME} Toy3dShaderParameters)
set_source_files_properties(${TOY3D_SHADER_PARAMETERS_HEADERS} PROPERTIES GENERATED TRUE)
target_sources(${TARGET_NAME} PRIVATE ${TOY3D_SHADER_PARAMETERS_HEADERS})
source_group("Generated Files\\Shader Parameters" FILES ${TOY3D_SHADER_PARAMETERS_HEADERS})

if(TOY3D_ENABLE_SHADER_MAP_ENTRY_LOADING)
    target_sources(${TARGET_NAME}
        PRIVATE
            "${CMAKE_CURRENT_SOURCE_DIR}/rendercore/shader/loaders/shader_map_entry_loader.cpp")
    target_compile_definitions(${TARGET_NAME}
        PRIVATE TOY3D_ENABLE_SHADER_MAP_ENTRY_LOADING=1)
else()
    target_compile_definitions(${TARGET_NAME}
        PRIVATE TOY3D_ENABLE_SHADER_MAP_ENTRY_LOADING=0)
endif()

if(TOY3D_ENABLE_VULKAN_RHI)
    if(NOT WIN32 AND NOT APPLE)
        message(FATAL_ERROR "The Vulkan RHI backend currently supports Windows and macOS.")
    endif()
    if(NOT EXISTS "${vulkan_include}/vulkan/vulkan.h" OR NOT EXISTS "${vulkan_lib}")
        message(FATAL_ERROR "The Vulkan RHI backend requires the configured Vulkan headers and loader library.")
    endif()
    if(APPLE AND (NOT EXISTS "${TOY3D_VK_ICD_PATH}" OR
                  NOT EXISTS "${TOY3D_VK_LAYER_PATH}/libMoltenVK.dylib"))
        message(FATAL_ERROR "The macOS Vulkan RHI backend requires the bundled MoltenVK ICD and library.")
    endif()

    target_sources(${TARGET_NAME}
        PRIVATE
            "${CMAKE_CURRENT_SOURCE_DIR}/drivers/vulkan/vulkan_device.cpp"
            "${CMAKE_CURRENT_SOURCE_DIR}/drivers/vulkan/vulkan_device.h"
            "${CMAKE_CURRENT_SOURCE_DIR}/drivers/vulkan/vulkan_binding_creation.cpp"
            "${CMAKE_CURRENT_SOURCE_DIR}/drivers/vulkan/vulkan_binding_creation.h"
            "${CMAKE_CURRENT_SOURCE_DIR}/drivers/vulkan/vulkan_command_context.cpp"
            "${CMAKE_CURRENT_SOURCE_DIR}/drivers/vulkan/vulkan_command_context.h"
            "${CMAKE_CURRENT_SOURCE_DIR}/drivers/vulkan/vulkan_descriptor_pool_manager.cpp"
            "${CMAKE_CURRENT_SOURCE_DIR}/drivers/vulkan/vulkan_descriptor_pool_manager.h"
            "${CMAKE_CURRENT_SOURCE_DIR}/drivers/vulkan/vulkan_deferred_deletion.cpp"
            "${CMAKE_CURRENT_SOURCE_DIR}/drivers/vulkan/vulkan_deferred_deletion.h"
            "${CMAKE_CURRENT_SOURCE_DIR}/drivers/vulkan/vulkan_resource.cpp"
            "${CMAKE_CURRENT_SOURCE_DIR}/drivers/vulkan/vulkan_resource.h"
            "${CMAKE_CURRENT_SOURCE_DIR}/drivers/vulkan/vulkan_resource_creation.cpp"
            "${CMAKE_CURRENT_SOURCE_DIR}/drivers/vulkan/vulkan_resource_creation.h"
            "${CMAKE_CURRENT_SOURCE_DIR}/drivers/vulkan/vulkan_graphics_state.cpp"
            "${CMAKE_CURRENT_SOURCE_DIR}/drivers/vulkan/vulkan_graphics_state.h"
            "${CMAKE_CURRENT_SOURCE_DIR}/drivers/vulkan/vulkan_memory_manager.cpp"
            "${CMAKE_CURRENT_SOURCE_DIR}/drivers/vulkan/vulkan_memory_manager.h"
            "${CMAKE_CURRENT_SOURCE_DIR}/drivers/vulkan/vulkan_pipeline_creation.cpp"
            "${CMAKE_CURRENT_SOURCE_DIR}/drivers/vulkan/vulkan_pipeline_creation.h"
            "${CMAKE_CURRENT_SOURCE_DIR}/drivers/vulkan/vulkan_swapchain.cpp"
            "${CMAKE_CURRENT_SOURCE_DIR}/drivers/vulkan/vulkan_swapchain.h"
            "${CMAKE_CURRENT_SOURCE_DIR}/drivers/vulkan/vulkan_type_mapping.cpp"
            "${CMAKE_CURRENT_SOURCE_DIR}/drivers/vulkan/vulkan_type_mapping.h"
            "${CMAKE_CURRENT_SOURCE_DIR}/drivers/vulkan/vulkan_upload_manager.cpp"
            "${CMAKE_CURRENT_SOURCE_DIR}/drivers/vulkan/vulkan_upload_manager.h"
            "${CMAKE_CURRENT_SOURCE_DIR}/drivers/vulkan/vulkan_queue.cpp"
            "${CMAKE_CURRENT_SOURCE_DIR}/drivers/vulkan/vulkan_queue.h"
            "${CMAKE_CURRENT_SOURCE_DIR}/drivers/vulkan/vulkan_viewport_context.cpp"
            "${CMAKE_CURRENT_SOURCE_DIR}/drivers/vulkan/vulkan_viewport_context.h"
            "${CMAKE_CURRENT_SOURCE_DIR}/drivers/vulkan/vma_implementation.cpp")
    target_compile_definitions(${TARGET_NAME}
        PRIVATE
            TOY3D_ENABLE_VULKAN_RHI=1
            "TOY3D_VK_LAYER_PATH=\"${TOY3D_VK_LAYER_PATH}\"")
    if(APPLE)
        target_compile_definitions(${TARGET_NAME}
            PRIVATE
                VK_USE_PLATFORM_METAL_EXT
                "TOY3D_VK_ICD_PATH=\"${TOY3D_VK_ICD_PATH}\"")
    endif()
endif()

set_target_properties(${TARGET_NAME} PROPERTIES
    CXX_STANDARD 17
    CXX_EXTENSIONS OFF)
if(APPLE)
    set_target_properties(${TARGET_NAME} PROPERTIES
        OBJCXX_STANDARD 17
        OBJCXX_STANDARD_REQUIRED ON
        OBJCXX_EXTENSIONS OFF)
endif()
set_target_properties(${TARGET_NAME} PROPERTIES FOLDER "Engine")

# being a cross-platform target, we enforce standards conformance on MSVC
target_compile_options(${TARGET_NAME} PUBLIC "$<$<COMPILE_LANG_AND_ID:CXX,MSVC>:/permissive->")
target_compile_options(${TARGET_NAME} PUBLIC "$<$<COMPILE_LANG_AND_ID:CXX,MSVC>:/WX->")
target_compile_options(${TARGET_NAME} PRIVATE "$<$<COMPILE_LANG_AND_ID:CXX,MSVC>:/utf-8>")

# Link dependencies    
target_link_libraries(${TARGET_NAME} PRIVATE tinyobjloader stb)
target_link_libraries(${TARGET_NAME} PUBLIC glfw)
target_link_libraries(${TARGET_NAME} PRIVATE imgui)
target_link_libraries(${TARGET_NAME} PUBLIC ${vulkan_lib})
target_link_libraries(${TARGET_NAME} PUBLIC vma)
target_link_libraries(${TARGET_NAME} PUBLIC glm::glm)
target_link_libraries(${TARGET_NAME} PUBLIC fmt)
target_link_libraries(${TARGET_NAME} PUBLIC Toy3dLogging)
target_link_libraries(${TARGET_NAME} PUBLIC Toy3dMath)
target_link_libraries(${TARGET_NAME} PUBLIC Toy3dFileSystem)
target_link_libraries(${TARGET_NAME} PUBLIC Toy3dPixelFormat)
target_link_libraries(${TARGET_NAME} PUBLIC Toy3dShaderFormat)
target_link_libraries(${TARGET_NAME} PUBLIC Toy3dTaskGraph)
if(APPLE)
    find_library(TOY3D_COCOA_FRAMEWORK Cocoa REQUIRED)
    find_library(TOY3D_QUARTZ_CORE_FRAMEWORK QuartzCore REQUIRED)
    target_link_libraries(${TARGET_NAME}
        PRIVATE
            "${TOY3D_COCOA_FRAMEWORK}"
            "${TOY3D_QUARTZ_CORE_FRAMEWORK}")
endif()


target_include_directories(
  ${TARGET_NAME}
  PUBLIC $<BUILD_INTERFACE:${vulkan_include}>)

# 添加项目根目录为包含目录
# target_include_directories(
#     ${TARGET_NAME}
#     PUBLIC $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>)

target_include_directories(
    ${TARGET_NAME} 
    PUBLIC $<BUILD_INTERFACE:${ENGINE_ROOT_DIR}/runtime>)

target_include_directories(
    ${TARGET_NAME} 
    PUBLIC $<BUILD_INTERFACE:${ENGINE_ROOT_DIR}/runtime/drivers>)

# add generated files
target_include_directories(
    ${TARGET_NAME} 
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/generated>
        $<BUILD_INTERFACE:${CMAKE_BINARY_DIR}/generated>)
  
# win32 resouce.h
if(WIN32)
    target_include_directories(${TARGET_NAME} PRIVATE "${ENGINE_ROOT_DIR}/build/windows/resources")
endif()

if(BUILD_TESTING)
    add_executable(Toy3dShaderParametersTests
        "tests/shader_parameters_tests.cpp")
    target_compile_features(Toy3dShaderParametersTests PRIVATE cxx_std_17)
    set_target_properties(Toy3dShaderParametersTests PROPERTIES
        CXX_EXTENSIONS OFF
        FOLDER "Engine/Tests")
    target_link_libraries(Toy3dShaderParametersTests PRIVATE Toy3dRuntime)
    add_test(NAME Toy3dRuntime.ShaderParameters COMMAND Toy3dShaderParametersTests)

    add_executable(Toy3dGeneratedShaderParametersCompileTests
        "tests/generated_shader_parameters_compile_tests.cpp")
    target_compile_features(Toy3dGeneratedShaderParametersCompileTests PRIVATE cxx_std_17)
    set_target_properties(Toy3dGeneratedShaderParametersCompileTests PROPERTIES
        CXX_EXTENSIONS OFF
        FOLDER "Engine/Tests")
    target_link_libraries(Toy3dGeneratedShaderParametersCompileTests PRIVATE Toy3dRuntime)
    add_test(NAME Toy3dRuntime.GeneratedShaderParametersCompile
        COMMAND Toy3dGeneratedShaderParametersCompileTests)

    find_package(Threads REQUIRED)
    add_executable(Toy3dConsoleManagerTests
        "tests/console_manager_tests.cpp"
        "config/console_manager.cpp"
        "config/console_manager.h"
        "config/command_line_parser.cpp"
        "config/command_line_parser.h")
    target_compile_features(Toy3dConsoleManagerTests PRIVATE cxx_std_17)
    set_target_properties(Toy3dConsoleManagerTests PROPERTIES
        CXX_EXTENSIONS OFF
        FOLDER "Engine/Tests")
    target_include_directories(Toy3dConsoleManagerTests
        PRIVATE
            "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>")
    target_compile_options(Toy3dConsoleManagerTests
        PRIVATE
            "$<$<COMPILE_LANG_AND_ID:CXX,MSVC>:/utf-8>")
    target_link_libraries(Toy3dConsoleManagerTests PRIVATE Toy3dFileSystem Threads::Threads)
    add_test(NAME Toy3dRuntime.ConsoleManager COMMAND Toy3dConsoleManagerTests)

    add_executable(Toy3dImGuiSystemTests
        "tests/imgui_system_tests.cpp")
    target_compile_features(Toy3dImGuiSystemTests PRIVATE cxx_std_17)
    set_target_properties(Toy3dImGuiSystemTests PROPERTIES
        CXX_EXTENSIONS OFF
        FOLDER "Engine/Tests")
    target_compile_options(Toy3dImGuiSystemTests
        PRIVATE
            "$<$<COMPILE_LANG_AND_ID:CXX,MSVC>:/utf-8>")
    target_link_libraries(Toy3dImGuiSystemTests PRIVATE Toy3dRuntime imgui)
    add_test(NAME Toy3dRuntime.ImGuiSystem COMMAND Toy3dImGuiSystemTests)

    add_executable(Toy3dTonemapPassTests
        "tests/tonemap_pass_tests.cpp")
    target_compile_features(Toy3dTonemapPassTests PRIVATE cxx_std_17)
    set_target_properties(Toy3dTonemapPassTests PROPERTIES
        CXX_EXTENSIONS OFF
        FOLDER "Engine/Tests")
    target_compile_options(Toy3dTonemapPassTests
        PRIVATE
            "$<$<COMPILE_LANG_AND_ID:CXX,MSVC>:/utf-8>")
    target_link_libraries(Toy3dTonemapPassTests PRIVATE Toy3dRuntime)
    add_test(NAME Toy3dRuntime.TonemapPass COMMAND Toy3dTonemapPassTests)

    add_executable(Toy3dShaderMapTests
        "tests/shader_map_tests.cpp")
    target_compile_features(Toy3dShaderMapTests PRIVATE cxx_std_17)
    set_target_properties(Toy3dShaderMapTests PROPERTIES
        CXX_EXTENSIONS OFF
        FOLDER "Engine/Tests")
    target_compile_options(Toy3dShaderMapTests
        PRIVATE
            "$<$<COMPILE_LANG_AND_ID:CXX,MSVC>:/utf-8>")
    target_link_libraries(Toy3dShaderMapTests PRIVATE Toy3dRuntime Threads::Threads)
    add_test(NAME Toy3dRuntime.ShaderMap COMMAND Toy3dShaderMapTests)

    add_executable(Toy3dShaderGraphicsStateTests
        "tests/shader_graphics_state_tests.cpp")
    target_compile_features(Toy3dShaderGraphicsStateTests PRIVATE cxx_std_17)
    set_target_properties(Toy3dShaderGraphicsStateTests PROPERTIES
        CXX_EXTENSIONS OFF
        FOLDER "Engine/Tests")
    target_compile_options(Toy3dShaderGraphicsStateTests
        PRIVATE
            "$<$<COMPILE_LANG_AND_ID:CXX,MSVC>:/utf-8>")
    target_link_libraries(Toy3dShaderGraphicsStateTests PRIVATE Toy3dRuntime)
    add_test(NAME Toy3dRuntime.ShaderGraphicsState COMMAND Toy3dShaderGraphicsStateTests)

    add_executable(Toy3dRHIShaderProgramCacheTests
        "tests/rhi_shader_program_cache_tests.cpp")
    target_compile_features(Toy3dRHIShaderProgramCacheTests PRIVATE cxx_std_17)
    set_target_properties(Toy3dRHIShaderProgramCacheTests PROPERTIES
        CXX_EXTENSIONS OFF
        FOLDER "Engine/Tests")
    target_compile_options(Toy3dRHIShaderProgramCacheTests
        PRIVATE
            "$<$<COMPILE_LANG_AND_ID:CXX,MSVC>:/utf-8>")
    target_link_libraries(Toy3dRHIShaderProgramCacheTests PRIVATE Toy3dRuntime)
    add_test(NAME Toy3dRuntime.RHIShaderProgramCache COMMAND Toy3dRHIShaderProgramCacheTests)

    add_executable(Toy3dGlobalShaderMapTests
        "tests/global_shader_map_tests.cpp")
    target_compile_features(Toy3dGlobalShaderMapTests PRIVATE cxx_std_17)
    set_target_properties(Toy3dGlobalShaderMapTests PROPERTIES
        CXX_EXTENSIONS OFF
        FOLDER "Engine/Tests")
    target_compile_options(Toy3dGlobalShaderMapTests
        PRIVATE
            "$<$<COMPILE_LANG_AND_ID:CXX,MSVC>:/utf-8>")
    target_link_libraries(Toy3dGlobalShaderMapTests PRIVATE Toy3dRuntime)
    add_test(NAME Toy3dRuntime.GlobalShaderMap COMMAND Toy3dGlobalShaderMapTests)

    add_executable(Toy3dRHIBindingTests
        "tests/rhi_binding_tests.cpp")
    target_compile_features(Toy3dRHIBindingTests PRIVATE cxx_std_17)
    set_target_properties(Toy3dRHIBindingTests PROPERTIES
        CXX_EXTENSIONS OFF
        FOLDER "Engine/Tests")
    target_compile_options(Toy3dRHIBindingTests
        PRIVATE
            "$<$<COMPILE_LANG_AND_ID:CXX,MSVC>:/utf-8>")
    target_link_libraries(Toy3dRHIBindingTests PRIVATE Toy3dRuntime)
    add_test(NAME Toy3dRuntime.RHIBinding COMMAND Toy3dRHIBindingTests)

    add_executable(Toy3dRHIDeviceFrontendTests
        "tests/rhi_device_frontend_tests.cpp")
    target_compile_features(Toy3dRHIDeviceFrontendTests PRIVATE cxx_std_17)
    set_target_properties(Toy3dRHIDeviceFrontendTests PROPERTIES
        CXX_EXTENSIONS OFF
        FOLDER "Engine/Tests")
    target_compile_options(Toy3dRHIDeviceFrontendTests
        PRIVATE
            "$<$<COMPILE_LANG_AND_ID:CXX,MSVC>:/utf-8>")
    target_link_libraries(Toy3dRHIDeviceFrontendTests PRIVATE Toy3dRuntime Threads::Threads)
    add_test(NAME Toy3dRuntime.RHIDeviceFrontend COMMAND Toy3dRHIDeviceFrontendTests)

    add_executable(Toy3dRHIPixelFormatTests
        "tests/rhi_format_tests.cpp")
    target_compile_features(Toy3dRHIPixelFormatTests PRIVATE cxx_std_17)
    set_target_properties(Toy3dRHIPixelFormatTests PROPERTIES
        CXX_EXTENSIONS OFF
        FOLDER "Engine/Tests")
    target_compile_options(Toy3dRHIPixelFormatTests
        PRIVATE
            "$<$<COMPILE_LANG_AND_ID:CXX,MSVC>:/utf-8>")
    target_link_libraries(Toy3dRHIPixelFormatTests PRIVATE Toy3dRuntime)
    add_test(NAME Toy3dRuntime.PixelFormat COMMAND Toy3dRHIPixelFormatTests)

    add_executable(Toy3dRHIViewportStatusTests
        "tests/rhi_viewport_status_tests.cpp")
    target_compile_features(Toy3dRHIViewportStatusTests PRIVATE cxx_std_17)
    set_target_properties(Toy3dRHIViewportStatusTests PROPERTIES
        CXX_EXTENSIONS OFF
        FOLDER "Engine/Tests")
    target_compile_options(Toy3dRHIViewportStatusTests
        PRIVATE
            "$<$<COMPILE_LANG_AND_ID:CXX,MSVC>:/utf-8>")
    target_link_libraries(Toy3dRHIViewportStatusTests PRIVATE Toy3dRuntime)
    add_test(NAME Toy3dRuntime.RHIViewportStatus COMMAND Toy3dRHIViewportStatusTests)

    if(TOY3D_ENABLE_VULKAN_RHI)
        add_executable(Toy3dVulkanTypeMappingTests
            "tests/vulkan_type_mapping_tests.cpp")
        target_compile_features(Toy3dVulkanTypeMappingTests PRIVATE cxx_std_17)
        set_target_properties(Toy3dVulkanTypeMappingTests PROPERTIES
            CXX_EXTENSIONS OFF
            FOLDER "Engine/Tests")
        target_compile_options(Toy3dVulkanTypeMappingTests
            PRIVATE
                "$<$<COMPILE_LANG_AND_ID:CXX,MSVC>:/utf-8>")
        target_link_libraries(Toy3dVulkanTypeMappingTests PRIVATE Toy3dRuntime)
        add_test(NAME Toy3dRuntime.VulkanTypeMapping
            COMMAND Toy3dVulkanTypeMappingTests)

        add_executable(Toy3dVulkanDeviceLifecycleTests
            "tests/vulkan_device_lifecycle_tests.cpp")
        target_compile_features(Toy3dVulkanDeviceLifecycleTests PRIVATE cxx_std_17)
        set_target_properties(Toy3dVulkanDeviceLifecycleTests PROPERTIES
            CXX_EXTENSIONS OFF
            FOLDER "Engine/Tests")
        target_compile_options(Toy3dVulkanDeviceLifecycleTests
            PRIVATE
                "$<$<COMPILE_LANG_AND_ID:CXX,MSVC>:/utf-8>")
        target_link_libraries(Toy3dVulkanDeviceLifecycleTests PRIVATE Toy3dRuntime)
        add_test(NAME Toy3dRuntime.VulkanDeviceLifecycle
            COMMAND Toy3dVulkanDeviceLifecycleTests)

        add_executable(Toy3dVulkanSwapchainTests
            "tests/vulkan_swapchain_tests.cpp")
        target_compile_features(Toy3dVulkanSwapchainTests PRIVATE cxx_std_17)
        set_target_properties(Toy3dVulkanSwapchainTests PROPERTIES
            CXX_EXTENSIONS OFF
            FOLDER "Engine/Tests")
        target_compile_options(Toy3dVulkanSwapchainTests
            PRIVATE
                "$<$<COMPILE_LANG_AND_ID:CXX,MSVC>:/utf-8>")
        target_link_libraries(Toy3dVulkanSwapchainTests PRIVATE Toy3dRuntime)
        add_test(NAME Toy3dRuntime.VulkanSwapchain
            COMMAND Toy3dVulkanSwapchainTests)
    endif()

    add_executable(Toy3dRHIResourceStateTests
        "tests/rhi_resource_state_tests.cpp")
    target_compile_features(Toy3dRHIResourceStateTests PRIVATE cxx_std_17)
    set_target_properties(Toy3dRHIResourceStateTests PROPERTIES
        CXX_EXTENSIONS OFF
        FOLDER "Engine/Tests")
    target_compile_options(Toy3dRHIResourceStateTests
        PRIVATE
            "$<$<COMPILE_LANG_AND_ID:CXX,MSVC>:/utf-8>")
    target_link_libraries(Toy3dRHIResourceStateTests PRIVATE Toy3dRuntime Threads::Threads)
    add_test(NAME Toy3dRuntime.RHIResourceState COMMAND Toy3dRHIResourceStateTests)

    add_executable(Toy3dRenderResourceManagerTests
        "tests/render_resource_manager_tests.cpp")
    target_compile_features(Toy3dRenderResourceManagerTests PRIVATE cxx_std_17)
    set_target_properties(Toy3dRenderResourceManagerTests PROPERTIES
        CXX_EXTENSIONS OFF
        FOLDER "Engine/Tests")
    target_compile_options(Toy3dRenderResourceManagerTests
        PRIVATE
            "$<$<COMPILE_LANG_AND_ID:CXX,MSVC>:/utf-8>")
    target_link_libraries(Toy3dRenderResourceManagerTests PRIVATE Toy3dRuntime)
    add_test(NAME Toy3dRuntime.RenderResourceManager
        COMMAND Toy3dRenderResourceManagerTests)

    add_executable(Toy3dGameSceneTests
        "tests/gamescene_tests.cpp")
    target_compile_features(Toy3dGameSceneTests PRIVATE cxx_std_17)
    set_target_properties(Toy3dGameSceneTests PROPERTIES
        CXX_EXTENSIONS OFF
        FOLDER "Engine/Tests")
    target_compile_options(Toy3dGameSceneTests
        PRIVATE
            "$<$<COMPILE_LANG_AND_ID:CXX,MSVC>:/utf-8>")
    target_link_libraries(Toy3dGameSceneTests PRIVATE Toy3dRuntime)
    add_test(NAME Toy3dRuntime.GameScene COMMAND Toy3dGameSceneTests)

    add_executable(Toy3dRenderingThreadTests
        "tests/rendering_thread_tests.cpp"
        "rendercore/render_command.cpp"
        "rendercore/render_command.h"
        "rendercore/render_command_internal.h"
        "rendercore/rendering_thread.cpp"
        "rendercore/rendering_thread.h")
    target_compile_features(Toy3dRenderingThreadTests PRIVATE cxx_std_17)
    set_target_properties(Toy3dRenderingThreadTests PROPERTIES
        CXX_EXTENSIONS OFF
        FOLDER "Engine/Tests")
    target_include_directories(Toy3dRenderingThreadTests
        PRIVATE
            "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>")
    target_compile_options(Toy3dRenderingThreadTests
        PRIVATE
            "$<$<COMPILE_LANG_AND_ID:CXX,MSVC>:/utf-8>")
    target_link_libraries(Toy3dRenderingThreadTests PRIVATE Toy3dTaskGraph Threads::Threads)
    add_test(NAME Toy3dRuntime.RenderingThread COMMAND Toy3dRenderingThreadTests)

    add_executable(Toy3dFrameSynchronizationTests
        "tests/frame_synchronization_tests.cpp"
        "rendercore/frame_synchronization.cpp"
        "rendercore/frame_synchronization.h"
        "rendercore/render_command.cpp"
        "rendercore/render_command.h"
        "rendercore/render_command_internal.h"
        "rendercore/rendering_thread.cpp"
        "rendercore/rendering_thread.h")
    target_compile_features(Toy3dFrameSynchronizationTests PRIVATE cxx_std_17)
    set_target_properties(Toy3dFrameSynchronizationTests PROPERTIES
        CXX_EXTENSIONS OFF
        FOLDER "Engine/Tests")
    target_include_directories(Toy3dFrameSynchronizationTests
        PRIVATE
            "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>")
    target_compile_options(Toy3dFrameSynchronizationTests
        PRIVATE
            "$<$<COMPILE_LANG_AND_ID:CXX,MSVC>:/utf-8>")
    target_link_libraries(Toy3dFrameSynchronizationTests
        PRIVATE Toy3dTaskGraph Threads::Threads)
    add_test(NAME Toy3dRuntime.FrameSynchronization
        COMMAND Toy3dFrameSynchronizationTests)

    add_executable(Toy3dRendererSceneOwnershipTests
        "tests/renderer_scene_ownership_tests.cpp")
    target_compile_features(Toy3dRendererSceneOwnershipTests PRIVATE cxx_std_17)
    set_target_properties(Toy3dRendererSceneOwnershipTests PROPERTIES
        CXX_EXTENSIONS OFF
        FOLDER "Engine/Tests")
    target_compile_options(Toy3dRendererSceneOwnershipTests
        PRIVATE
            "$<$<COMPILE_LANG_AND_ID:CXX,MSVC>:/utf-8>")
    target_link_libraries(Toy3dRendererSceneOwnershipTests PRIVATE Toy3dRuntime)
    add_test(NAME Toy3dRuntime.RendererSceneOwnership
        COMMAND Toy3dRendererSceneOwnershipTests)

    if(TOY3D_ENABLE_SHADER_MAP_ENTRY_LOADING)
        set(TOY3D_SHADER_MAP_ENTRY_LOADER_FIXTURE_SOURCE
            "${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/shaders/test_pass.shader")
        set(TOY3D_SHADER_MAP_ENTRY_LOADER_FIXTURE_ROOT
            "${CMAKE_BINARY_DIR}/generated/test_fixtures/shader_map_entry_loader/test_pass")
        set(TOY3D_SHADER_MAP_ENTRY_LOADER_FIXTURE_WORKING_DIR
            "${CMAKE_BINARY_DIR}/generated/test_fixtures/shader_work/shader_map_entry_loader")
        set(TOY3D_SHADER_MAP_ENTRY_LOADER_FIXTURE_STAMP
            "${CMAKE_BINARY_DIR}/generated/test_fixtures/shader_map_entry_loader/test_pass.stamp")
        add_custom_command(
            OUTPUT "${TOY3D_SHADER_MAP_ENTRY_LOADER_FIXTURE_STAMP}"
            COMMAND ${CMAKE_COMMAND} -E remove_directory
                    "${TOY3D_SHADER_MAP_ENTRY_LOADER_FIXTURE_ROOT}"
            COMMAND ${CMAKE_COMMAND} -E make_directory
                    "${TOY3D_SHADER_MAP_ENTRY_LOADER_FIXTURE_ROOT}"
            COMMAND ${CMAKE_COMMAND} -E make_directory
                    "${TOY3D_SHADER_MAP_ENTRY_LOADER_FIXTURE_WORKING_DIR}"
            COMMAND $<TARGET_FILE:Toy3dShaderCompiler>
                    --toolchain-root "${TOY3D_SHADER_TOOLCHAIN_ROOT}"
                    compile-vulkan
                    "${TOY3D_SHADER_MAP_ENTRY_LOADER_FIXTURE_SOURCE}"
                    "/Engine/Tests/Shaders/test_pass.shader"
                    "TestPass"
                    "${TOY3D_SHADER_MAP_ENTRY_LOADER_FIXTURE_ROOT}"
                    "${TOY3D_SHADER_MAP_ENTRY_LOADER_FIXTURE_WORKING_DIR}"
            COMMAND ${CMAKE_COMMAND} -E touch
                    "${TOY3D_SHADER_MAP_ENTRY_LOADER_FIXTURE_STAMP}"
            DEPENDS
                Toy3dShaderCompiler
                "${TOY3D_SHADER_MAP_ENTRY_LOADER_FIXTURE_SOURCE}"
            VERBATIM)
        add_custom_target(Toy3dShaderMapEntryLoaderFixture
            DEPENDS "${TOY3D_SHADER_MAP_ENTRY_LOADER_FIXTURE_STAMP}")
        set_target_properties(Toy3dShaderMapEntryLoaderFixture PROPERTIES
            FOLDER "Engine/Tests")

        add_executable(Toy3dShaderMapEntryLoaderTests
            "tests/shader_map_entry_loader_tests.cpp")
        target_compile_features(Toy3dShaderMapEntryLoaderTests PRIVATE cxx_std_17)
        set_target_properties(Toy3dShaderMapEntryLoaderTests PROPERTIES
            CXX_EXTENSIONS OFF
            FOLDER "Engine/Tests")
        target_compile_definitions(Toy3dShaderMapEntryLoaderTests
            PRIVATE
                "TOY3D_SHADER_MAP_ENTRY_TEST_ROOT=\"${TOY3D_SHADER_MAP_ENTRY_LOADER_FIXTURE_ROOT}\"")
        target_compile_options(Toy3dShaderMapEntryLoaderTests
            PRIVATE
                "$<$<COMPILE_LANG_AND_ID:CXX,MSVC>:/utf-8>")
        target_link_libraries(Toy3dShaderMapEntryLoaderTests PRIVATE Toy3dRuntime)
        add_dependencies(Toy3dShaderMapEntryLoaderTests
            Toy3dShaderMapEntryLoaderFixture)
        add_test(NAME Toy3dRuntime.ShaderMapEntryLoader COMMAND Toy3dShaderMapEntryLoaderTests)
    endif()
endif()
