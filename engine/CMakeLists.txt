set(ENGINE_ROOT_DIR "${TOY3D_ROOT_DIR}/engine")
set(THIRD_PARTY_DIR "${ENGINE_ROOT_DIR}/thirdparty")
set(ENGINE_RUNTIME_DIR "${ENGINE_ROOT_DIR}/runtime")
include("${ENGINE_ROOT_DIR}/build/cmake/deploy_resources.cmake")

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

# 构建子文件夹cmake工程
# Keep spdlog and all consumers on the same native Windows filename ABI.
if(WIN32)
    set(SPDLOG_WCHAR_FILENAMES ON CACHE BOOL "Use native Unicode log paths on Windows" FORCE)
endif()
add_library(imgui SHARED
    "${THIRD_PARTY_DIR}/imgui/imgui.cpp" "${THIRD_PARTY_DIR}/imgui/imgui_draw.cpp"
    "${THIRD_PARTY_DIR}/imgui/imgui_tables.cpp" "${THIRD_PARTY_DIR}/imgui/imgui_widgets.cpp"
    "${THIRD_PARTY_DIR}/ImGuizmo/ImGuizmo.cpp")
target_compile_features(imgui PUBLIC cxx_std_17)
target_include_directories(imgui PUBLIC "${THIRD_PARTY_DIR}/imgui" "${THIRD_PARTY_DIR}/ImGuizmo")
# ImGuizmo shares IMGUI_API and must live in the same DLL as the ImGui context.
set_target_properties(imgui PROPERTIES CXX_EXTENSIONS OFF)
if(WIN32)
    target_compile_definitions(imgui PRIVATE "IMGUI_API=__declspec(dllexport)"
        INTERFACE "IMGUI_API=__declspec(dllimport)")
endif()
toy3d_shared_library(imgui)
add_subdirectory(thirdparty)
add_subdirectory(core)

add_subdirectory(tools)
add_subdirectory(shader)
add_subdirectory(runtime)
if(TOY3D_ENABLE_SHADER_MAP_ENTRY_LOADING)
    add_subdirectory(editor)
else()
    message(STATUS "Toy3dEditor is disabled: development ShaderMapEntry loading is required.")
endif()
