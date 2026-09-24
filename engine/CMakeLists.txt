
cmake_minimum_required(VERSION 3.19)
project(Toy3dEngine)

set(ENGINE_ROOT_DIR "${TOY3D_ROOT_DIR}/engine")
set(THIRD_PARTY_DIR "${ENGINE_ROOT_DIR}/thirdparty")
set(ENGINE_RUNTIME_DIR "${ENGINE_ROOT_DIR}/runtime")
set(ASSET_NAME "asset")

# 设置C++标准
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# vulkan sdk
set(vulkan_include ${THIRD_PARTY_DIR}/VulkanSDK/include)

if(WIN32)
    set(vulkan_lib ${THIRD_PARTY_DIR}/VulkanSDK/lib/Win32/vulkan-1.lib)
    set(TOY3D_VK_LAYER_PATH "${THIRD_PARTY_DIR}/VulkanSDK/bin/Win32")
elseif(UNIX)
    if(APPLE)
      set(vulkan_lib ${THIRD_PARTY_DIR}/VulkanSDK/lib/MacOS/libvulkan.1.dylib)
      set(TOY3D_VK_LAYER_PATH "${THIRD_PARTY_DIR}/VulkanSDK/bin/MacOS")
      set(TOY3D_VK_ICD_PATH "${THIRD_PARTY_DIR}/VulkanSDK/bin/MacOS/MoltenVK_icd.json")
      message(STATUS "apple platform")
    else()
      set(vulkan_lib ${THIRD_PARTY_DIR}/VulkanSDK/lib/Linux/libvulkan.so.1)
      set(TOY3D_VK_LAYER_PATH "${THIRD_PARTY_DIR}/VulkanSDK/bin/Linux")
      message(STATUS "linux platform")
    endif()
else()
  message(FATAL_ERROR "unknown platform")
endif()

# 生成配置头文件
set(ENGINE_ASSET_ROOT "${BINARY_ROOT_DIR}") 
set(ENGINE_SAVED_ROOT "${BINARY_ROOT_DIR}saved") 

configure_file(
  ${ENGINE_ROOT_DIR}/template/defines.h.in
  ${ENGINE_RUNTIME_DIR}/generated/defines.h
)

# 构建子文件夹cmake工程
add_subdirectory(thirdparty)
add_subdirectory(core)
add_subdirectory(resource)

add_subdirectory(tools)
add_subdirectory(shader)
add_subdirectory(runtime)
add_subdirectory(editor)
