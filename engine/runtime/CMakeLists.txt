set(TARGET_NAME Toy3dRuntime)

option(TOY3D_ENABLE_VULKAN_RHI "Build the canonical Vulkan RHI backend" OFF)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
 
file(GLOB_RECURSE ALL_HEADER_FILES CONFIGURE_DEPENDS "*.h")
file(GLOB_RECURSE ALL_SOURCE_FILES CONFIGURE_DEPENDS "*.cpp")

# The previous IDynamicRHI Vulkan prototype is intentionally kept out of the
# target while the backend is rebuilt against the canonical RHIDevice API.
list(FILTER ALL_HEADER_FILES EXCLUDE REGEX ".*/drivers/vulkan/.*\\.h$")
list(FILTER ALL_SOURCE_FILES EXCLUDE REGEX ".*/drivers/vulkan/.*\\.cpp$")
list(FILTER ALL_SOURCE_FILES EXCLUDE REGEX ".*/drivers/example/.*\\.cpp$")

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

add_custom_command(
    TARGET ${TARGET_NAME}
    POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory "${BINARY_ROOT_DIR}/shader"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different ${TOY3D_SHADER_OUTPUTS} "${BINARY_ROOT_DIR}/shader"
    VERBATIM)

if(TOY3D_ENABLE_VULKAN_RHI)
    if(NOT WIN32)
        message(FATAL_ERROR "The canonical Vulkan RHI backend currently requires WIN32.")
    endif()
    if(NOT EXISTS "${vulkan_include}/vulkan/vulkan.h" OR NOT EXISTS "${vulkan_lib}")
        message(FATAL_ERROR "The canonical Vulkan RHI backend requires the configured Vulkan headers and loader library.")
    endif()

    target_sources(${TARGET_NAME}
        PRIVATE
            "${CMAKE_CURRENT_SOURCE_DIR}/drivers/vulkan/canonical/vulkan_device.cpp"
            "${CMAKE_CURRENT_SOURCE_DIR}/drivers/vulkan/canonical/vulkan_device.h"
            "${CMAKE_CURRENT_SOURCE_DIR}/drivers/vulkan/canonical/vulkan_command_context.cpp"
            "${CMAKE_CURRENT_SOURCE_DIR}/drivers/vulkan/canonical/vulkan_command_context.h"
            "${CMAKE_CURRENT_SOURCE_DIR}/drivers/vulkan/canonical/vulkan_deferred_deletion.cpp"
            "${CMAKE_CURRENT_SOURCE_DIR}/drivers/vulkan/canonical/vulkan_deferred_deletion.h"
            "${CMAKE_CURRENT_SOURCE_DIR}/drivers/vulkan/canonical/vulkan_resource.cpp"
            "${CMAKE_CURRENT_SOURCE_DIR}/drivers/vulkan/canonical/vulkan_resource.h"
            "${CMAKE_CURRENT_SOURCE_DIR}/drivers/vulkan/canonical/vulkan_graphics_state.cpp"
            "${CMAKE_CURRENT_SOURCE_DIR}/drivers/vulkan/canonical/vulkan_graphics_state.h"
            "${CMAKE_CURRENT_SOURCE_DIR}/drivers/vulkan/canonical/vulkan_memory_allocator.cpp"
            "${CMAKE_CURRENT_SOURCE_DIR}/drivers/vulkan/canonical/vulkan_memory_allocator.h"
            "${CMAKE_CURRENT_SOURCE_DIR}/drivers/vulkan/canonical/vulkan_queue.cpp"
            "${CMAKE_CURRENT_SOURCE_DIR}/drivers/vulkan/canonical/vulkan_queue.h"
            "${CMAKE_CURRENT_SOURCE_DIR}/drivers/vulkan/canonical/vulkan_swapchain.cpp"
            "${CMAKE_CURRENT_SOURCE_DIR}/drivers/vulkan/canonical/vulkan_swapchain.h"
            "${CMAKE_CURRENT_SOURCE_DIR}/drivers/vulkan/canonical/vulkan_viewport_context.cpp"
            "${CMAKE_CURRENT_SOURCE_DIR}/drivers/vulkan/canonical/vulkan_viewport_context.h")
    target_compile_definitions(${TARGET_NAME}
        PRIVATE
            TOY3D_ENABLE_VULKAN_RHI=1
            "TOY3D_VK_LAYER_PATH=\"${TOY3D_VK_LAYER_PATH}\"")
endif()

set_target_properties(${TARGET_NAME} PROPERTIES CXX_STANDARD 17)
set_target_properties(${TARGET_NAME} PROPERTIES FOLDER "Engine")

# being a cross-platform target, we enforce standards conformance on MSVC
target_compile_options(${TARGET_NAME} PUBLIC "$<$<COMPILE_LANG_AND_ID:CXX,MSVC>:/permissive->")
target_compile_options(${TARGET_NAME} PUBLIC "$<$<COMPILE_LANG_AND_ID:CXX,MSVC>:/WX->")

# Link dependencies    
target_link_libraries(${TARGET_NAME} PRIVATE tinyobjloader stb)
target_link_libraries(${TARGET_NAME} PUBLIC glfw)
target_link_libraries(${TARGET_NAME} PUBLIC imgui)
target_link_libraries(${TARGET_NAME} PUBLIC ${vulkan_lib})
target_link_libraries(${TARGET_NAME} PUBLIC vma)
target_link_libraries(${TARGET_NAME} PUBLIC glm::glm)
target_link_libraries(${TARGET_NAME} PUBLIC fmt)
target_link_libraries(${TARGET_NAME} PUBLIC spdlog)


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
