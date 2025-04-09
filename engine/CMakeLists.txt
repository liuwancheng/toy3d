
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
    set(glslangValidator_executable ${THIRD_PARTY_DIR}/VulkanSDK/bin/Win32/glslangValidator.exe)
    add_compile_definitions("TOY3D_VK_LAYER_PATH=${THIRD_PARTY_DIR}/VulkanSDK/bin/Win32")
elseif(UNIX)
    if(APPLE)
      set(vulkan_lib ${THIRD_PARTY_DIR}/VulkanSDK/lib/MacOS/libvulkan.1.dylib)
      set(glslangValidator_executable ${THIRD_PARTY_DIR}/VulkanSDK/bin/MacOS/glslangValidator)
      add_compile_definitions("TOY3D_VK_LAYER_PATH=${THIRD_PARTY_DIR}/VulkanSDK/bin/MacOS")
      add_compile_definitions("TOY3D_VK_ICD_FILENAMES=${THIRD_PARTY_DIR}/VulkanSDK/bin/MacOS/MoltenVK_icd.json")
      message(STATUS "apple platform")
    else()
      set(vulkan_lib ${THIRD_PARTY_DIR}/VulkanSDK/lib/Linux/libvulkan.so.1)
      set(glslangValidator_executable ${THIRD_PARTY_DIR}/VulkanSDK/bin/Linux/glslangValidator)
      add_compile_definitions("TOY3D_VK_LAYER_PATH=${THIRD_PARTY_DIR}/VulkanSDK/bin/Linux")
      message(STATUS "linux platform")
    endif()
else()
  message(FATAL_ERROR "unknown platform")
endif()

#set(SHADER_COMPILE_TARGET Toy3dShaderCompile)
#add_subdirectory(shader)

add_subdirectory(thirdparty)
add_subdirectory(runtime)
add_subdirectory(editor)

# 生成配置头文件
set(ENGINE_ASSET_ROOT "${BINARY_ROOT_DIR}asset/") 

configure_file(
  ${ENGINE_ROOT_DIR}/template/defines.h.in
  ${ENGINE_ROOT_DIR}/generated/defines.h
)