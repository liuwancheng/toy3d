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
         "${CMAKE_CURRENT_SOURCE_DIR}/platform/mac/*.cpp")
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
set(HEADER_FILES ${ALL_HEADER_FILES} ${PLATFORM_HEADER_FILES})
set(SOURCE_FILES ${ALL_SOURCE_FILES} ${PLATFORM_SOURCE_FILES})

# message(STATUS \"HEADER_FILES:\")
# foreach(file ${HEADER_FILES})
#     message(STATUS \"  ${file}\")
# endforeach()

source_group(TREE "${CMAKE_CURRENT_SOURCE_DIR}" FILES ${HEADER_FILES} ${SOURCE_FILES})
add_library(${TARGET_NAME} ${HEADER_FILES} ${SOURCE_FILES})
add_dependencies(${TARGET_NAME} Toy3dShaders)

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
            "${CMAKE_CURRENT_SOURCE_DIR}/drivers/vulkan/vulkan_command_context.cpp"
            "${CMAKE_CURRENT_SOURCE_DIR}/drivers/vulkan/vulkan_command_context.h"
            "${CMAKE_CURRENT_SOURCE_DIR}/drivers/vulkan/vulkan_deferred_deletion.cpp"
            "${CMAKE_CURRENT_SOURCE_DIR}/drivers/vulkan/vulkan_deferred_deletion.h"
            "${CMAKE_CURRENT_SOURCE_DIR}/drivers/vulkan/vulkan_resource.cpp"
            "${CMAKE_CURRENT_SOURCE_DIR}/drivers/vulkan/vulkan_resource.h"
            "${CMAKE_CURRENT_SOURCE_DIR}/drivers/vulkan/vulkan_graphics_state.cpp"
            "${CMAKE_CURRENT_SOURCE_DIR}/drivers/vulkan/vulkan_graphics_state.h"
            "${CMAKE_CURRENT_SOURCE_DIR}/drivers/vulkan/vulkan_memory_manager.cpp"
            "${CMAKE_CURRENT_SOURCE_DIR}/drivers/vulkan/vulkan_memory_manager.h"
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

set_target_properties(${TARGET_NAME} PROPERTIES CXX_STANDARD 17)
set_target_properties(${TARGET_NAME} PROPERTIES FOLDER "Engine")

# being a cross-platform target, we enforce standards conformance on MSVC
target_compile_options(${TARGET_NAME} PUBLIC "$<$<COMPILE_LANG_AND_ID:CXX,MSVC>:/permissive->")
target_compile_options(${TARGET_NAME} PUBLIC "$<$<COMPILE_LANG_AND_ID:CXX,MSVC>:/WX->")
target_compile_options(${TARGET_NAME} PRIVATE "$<$<COMPILE_LANG_AND_ID:CXX,MSVC>:/utf-8>")

# Link dependencies    
target_link_libraries(${TARGET_NAME} PRIVATE tinyobjloader stb)
target_link_libraries(${TARGET_NAME} PUBLIC glfw)
target_link_libraries(${TARGET_NAME} PUBLIC imgui)
target_link_libraries(${TARGET_NAME} PUBLIC ${vulkan_lib})
target_link_libraries(${TARGET_NAME} PUBLIC vma)
target_link_libraries(${TARGET_NAME} PUBLIC glm::glm)
target_link_libraries(${TARGET_NAME} PUBLIC fmt)
target_link_libraries(${TARGET_NAME} PUBLIC Toy3dLogging)
target_link_libraries(${TARGET_NAME} PUBLIC Toy3dMath)
target_link_libraries(${TARGET_NAME} PUBLIC Toy3dFileSystem)
target_link_libraries(${TARGET_NAME} PUBLIC Toy3dShaderFormat)


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
    PUBLIC $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/generated>)
  
# win32 resouce.h
if(WIN32)
    target_include_directories(${TARGET_NAME} PRIVATE ${ENGINE_ROOT_DIR}/${ASSET_NAME}/win32)
endif()

if(BUILD_TESTING)
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

    add_executable(Toy3dRHIFormatTests
        "tests/rhi_format_tests.cpp")
    target_compile_features(Toy3dRHIFormatTests PRIVATE cxx_std_17)
    set_target_properties(Toy3dRHIFormatTests PROPERTIES
        CXX_EXTENSIONS OFF
        FOLDER "Engine/Tests")
    target_compile_options(Toy3dRHIFormatTests
        PRIVATE
            "$<$<COMPILE_LANG_AND_ID:CXX,MSVC>:/utf-8>")
    target_link_libraries(Toy3dRHIFormatTests PRIVATE Toy3dRuntime)
    add_test(NAME Toy3dRuntime.RHIFormat COMMAND Toy3dRHIFormatTests)

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

    add_executable(Toy3dRenderSceneTests
        "tests/render_scene_tests.cpp")
    target_compile_features(Toy3dRenderSceneTests PRIVATE cxx_std_17)
    set_target_properties(Toy3dRenderSceneTests PROPERTIES
        CXX_EXTENSIONS OFF
        FOLDER "Engine/Tests")
    target_compile_options(Toy3dRenderSceneTests
        PRIVATE
            "$<$<COMPILE_LANG_AND_ID:CXX,MSVC>:/utf-8>")
    target_link_libraries(Toy3dRenderSceneTests PRIVATE Toy3dRuntime)
    add_test(NAME Toy3dRuntime.RenderScene COMMAND Toy3dRenderSceneTests)

    add_executable(Toy3dRenderResourceCacheTests
        "tests/render_resource_cache_tests.cpp")
    target_compile_features(Toy3dRenderResourceCacheTests PRIVATE cxx_std_17)
    set_target_properties(Toy3dRenderResourceCacheTests PROPERTIES
        CXX_EXTENSIONS OFF
        FOLDER "Engine/Tests")
    target_compile_options(Toy3dRenderResourceCacheTests
        PRIVATE
            "$<$<COMPILE_LANG_AND_ID:CXX,MSVC>:/utf-8>")
    target_link_libraries(Toy3dRenderResourceCacheTests PRIVATE Toy3dRuntime)
    add_test(NAME Toy3dRuntime.RenderResourceCache
        COMMAND Toy3dRenderResourceCacheTests)

    add_executable(Toy3dRenderResourceUploadTests
        "tests/render_resource_upload_tests.cpp")
    target_compile_features(Toy3dRenderResourceUploadTests PRIVATE cxx_std_17)
    set_target_properties(Toy3dRenderResourceUploadTests PROPERTIES
        CXX_EXTENSIONS OFF
        FOLDER "Engine/Tests")
    target_compile_options(Toy3dRenderResourceUploadTests
        PRIVATE
            "$<$<COMPILE_LANG_AND_ID:CXX,MSVC>:/utf-8>")
    target_link_libraries(Toy3dRenderResourceUploadTests PRIVATE Toy3dRuntime)
    add_test(NAME Toy3dRuntime.RenderResourceUpload
        COMMAND Toy3dRenderResourceUploadTests)

    add_executable(Toy3dRenderFoundationPipelineTests
        "tests/render_foundation_pipeline_tests.cpp")
    target_compile_features(Toy3dRenderFoundationPipelineTests PRIVATE cxx_std_17)
    set_target_properties(Toy3dRenderFoundationPipelineTests PROPERTIES
        CXX_EXTENSIONS OFF
        FOLDER "Engine/Tests")
    target_compile_options(Toy3dRenderFoundationPipelineTests
        PRIVATE
            "$<$<COMPILE_LANG_AND_ID:CXX,MSVC>:/utf-8>")
    target_link_libraries(Toy3dRenderFoundationPipelineTests PRIVATE Toy3dRuntime)
    add_test(NAME Toy3dRuntime.RenderFoundationPipeline
        COMMAND Toy3dRenderFoundationPipelineTests)

    add_executable(Toy3dRenderFrameTransportTests
        "tests/render_frame_transport_tests.cpp")
    target_compile_features(Toy3dRenderFrameTransportTests PRIVATE cxx_std_17)
    set_target_properties(Toy3dRenderFrameTransportTests PROPERTIES
        CXX_EXTENSIONS OFF
        FOLDER "Engine/Tests")
    target_compile_options(Toy3dRenderFrameTransportTests
        PRIVATE
            "$<$<COMPILE_LANG_AND_ID:CXX,MSVC>:/utf-8>")
    target_link_libraries(Toy3dRenderFrameTransportTests
        PRIVATE Toy3dRuntime Threads::Threads)
    add_test(NAME Toy3dRuntime.RenderFrameTransport
        COMMAND Toy3dRenderFrameTransportTests)

    if(TOY3D_ENABLE_SHADER_MAP_ENTRY_LOADING)
        add_executable(Toy3dShaderMapEntryLoaderTests
            "tests/shader_map_entry_loader_tests.cpp")
        target_compile_features(Toy3dShaderMapEntryLoaderTests PRIVATE cxx_std_17)
        set_target_properties(Toy3dShaderMapEntryLoaderTests PROPERTIES
            CXX_EXTENSIONS OFF
            FOLDER "Engine/Tests")
        target_compile_definitions(Toy3dShaderMapEntryLoaderTests
            PRIVATE
                "TOY3D_SHADER_MAP_ENTRY_TEST_ROOT=\"${TOY3D_SHADER_MAP_OUTPUT_DIR}/test_pass\"")
        target_compile_options(Toy3dShaderMapEntryLoaderTests
            PRIVATE
                "$<$<COMPILE_LANG_AND_ID:CXX,MSVC>:/utf-8>")
        target_link_libraries(Toy3dShaderMapEntryLoaderTests PRIVATE Toy3dRuntime)
        add_dependencies(Toy3dShaderMapEntryLoaderTests Toy3dShaders)
        add_test(NAME Toy3dRuntime.ShaderMapEntryLoader COMMAND Toy3dShaderMapEntryLoaderTests)
    endif()
endif()
