add_library(Toy3dShaderFormat STATIC)
target_sources(Toy3dShaderFormat
    PRIVATE
        "format/shader_format_hash.cpp"
        "format/sha256.cpp"
        "format/sha256.h"
        "format/shader_format_types.h"
        "format/shader_map_entry.h"
        "format/shader_map_entry_reader.cpp")
target_compile_features(Toy3dShaderFormat PUBLIC cxx_std_17)
set_target_properties(Toy3dShaderFormat PROPERTIES
    CXX_EXTENSIONS OFF
    FOLDER "Engine/Shader")
target_include_directories(Toy3dShaderFormat
    PUBLIC
        "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>")
target_link_libraries(Toy3dShaderFormat PUBLIC Toy3dFileSystem)

set(TOY3D_SHADER_MAP_OUTPUT_DIR "${CMAKE_BINARY_DIR}/generated/shader_map")
set(TOY3D_UNLIT_SHADER_SOURCE
    "${CMAKE_CURRENT_SOURCE_DIR}/builtin/surface/unlit.shader")
set(TOY3D_UNLIT_SHADER_MAP_ROOT "${TOY3D_SHADER_MAP_OUTPUT_DIR}/unlit")
set(TOY3D_UNLIT_SHADER_WORKING_DIR "${CMAKE_BINARY_DIR}/generated/shader_work/unlit")
set(TOY3D_UNLIT_SHADER_STAMP "${TOY3D_SHADER_MAP_OUTPUT_DIR}/unlit.stamp")
set(TOY3D_PHONG_SHADER_SOURCE
    "${CMAKE_CURRENT_SOURCE_DIR}/builtin/surface/phong.shader")
set(TOY3D_PHONG_SHADER_MAP_ROOT "${TOY3D_SHADER_MAP_OUTPUT_DIR}/phong")
set(TOY3D_PHONG_SHADER_WORKING_DIR "${CMAKE_BINARY_DIR}/generated/shader_work/phong")
set(TOY3D_PHONG_SHADER_STAMP "${TOY3D_SHADER_MAP_OUTPUT_DIR}/phong.stamp")
set(TOY3D_TONEMAP_SHADER_SOURCE
    "${CMAKE_CURRENT_SOURCE_DIR}/builtin/postprocess/tonemap.shader")
set(TOY3D_TONEMAP_SHADER_MAP_ROOT "${TOY3D_SHADER_MAP_OUTPUT_DIR}/tonemap")
set(TOY3D_TONEMAP_SHADER_WORKING_DIR "${CMAKE_BINARY_DIR}/generated/shader_work/tonemap")
set(TOY3D_TONEMAP_SHADER_STAMP "${TOY3D_SHADER_MAP_OUTPUT_DIR}/tonemap.stamp")
set(TOY3D_IMGUI_SHADER_SOURCE
    "${CMAKE_CURRENT_SOURCE_DIR}/builtin/ui/imgui.shader")
set(TOY3D_IMGUI_SHADER_MAP_ROOT "${TOY3D_SHADER_MAP_OUTPUT_DIR}/imgui")
set(TOY3D_IMGUI_SHADER_WORKING_DIR "${CMAKE_BINARY_DIR}/generated/shader_work/imgui")
set(TOY3D_IMGUI_SHADER_STAMP "${TOY3D_SHADER_MAP_OUTPUT_DIR}/imgui.stamp")
set(TOY3D_OUTPUT_SHADER_DEPLOY_ROOT "${BINARY_ROOT_DIR}/shader/output")
set(TOY3D_OUTPUT_SHADER_DEPLOY_STAMP "${TOY3D_SHADER_MAP_OUTPUT_DIR}/output_deploy.stamp")

add_custom_command(
    OUTPUT "${TOY3D_UNLIT_SHADER_STAMP}"
    COMMAND ${CMAKE_COMMAND} -E remove_directory "${TOY3D_UNLIT_SHADER_MAP_ROOT}"
    COMMAND ${CMAKE_COMMAND} -E make_directory "${TOY3D_UNLIT_SHADER_MAP_ROOT}"
    COMMAND ${CMAKE_COMMAND} -E make_directory "${TOY3D_UNLIT_SHADER_WORKING_DIR}"
    COMMAND $<TARGET_FILE:Toy3dShaderCompiler>
            --toolchain-root "${TOY3D_SHADER_TOOLCHAIN_ROOT}"
            compile-vulkan
            "${TOY3D_UNLIT_SHADER_SOURCE}"
            "/Engine/Shaders/Surface/unlit.shader"
            "Forward"
            "${TOY3D_UNLIT_SHADER_MAP_ROOT}"
            "${TOY3D_UNLIT_SHADER_WORKING_DIR}"
    COMMAND ${CMAKE_COMMAND} -E remove_directory "${BINARY_ROOT_DIR}/shader/unlit"
    COMMAND ${CMAKE_COMMAND} -E make_directory "${BINARY_ROOT_DIR}/shader"
    COMMAND ${CMAKE_COMMAND} -E copy_directory
            "${TOY3D_UNLIT_SHADER_MAP_ROOT}"
            "${BINARY_ROOT_DIR}/shader/unlit"
    COMMAND ${CMAKE_COMMAND} -E touch "${TOY3D_UNLIT_SHADER_STAMP}"
    DEPENDS
        Toy3dShaderCompiler
        "${TOY3D_UNLIT_SHADER_SOURCE}"
    VERBATIM)

add_custom_command(
    OUTPUT "${TOY3D_PHONG_SHADER_STAMP}"
    COMMAND ${CMAKE_COMMAND} -E remove_directory "${TOY3D_PHONG_SHADER_MAP_ROOT}"
    COMMAND ${CMAKE_COMMAND} -E make_directory "${TOY3D_PHONG_SHADER_MAP_ROOT}"
    COMMAND ${CMAKE_COMMAND} -E make_directory "${TOY3D_PHONG_SHADER_WORKING_DIR}"
    COMMAND $<TARGET_FILE:Toy3dShaderCompiler>
            --toolchain-root "${TOY3D_SHADER_TOOLCHAIN_ROOT}"
            compile-vulkan
            "${TOY3D_PHONG_SHADER_SOURCE}"
            "/Engine/Shaders/Surface/phong.shader"
            "Forward"
            "${TOY3D_PHONG_SHADER_MAP_ROOT}"
            "${TOY3D_PHONG_SHADER_WORKING_DIR}"
    COMMAND ${CMAKE_COMMAND} -E remove_directory "${BINARY_ROOT_DIR}/shader/phong"
    COMMAND ${CMAKE_COMMAND} -E make_directory "${BINARY_ROOT_DIR}/shader"
    COMMAND ${CMAKE_COMMAND} -E copy_directory
            "${TOY3D_PHONG_SHADER_MAP_ROOT}"
            "${BINARY_ROOT_DIR}/shader/phong"
    COMMAND ${CMAKE_COMMAND} -E touch "${TOY3D_PHONG_SHADER_STAMP}"
    DEPENDS
        Toy3dShaderCompiler
        "${TOY3D_PHONG_SHADER_SOURCE}"
    VERBATIM)

add_custom_command(
    OUTPUT "${TOY3D_TONEMAP_SHADER_STAMP}"
    COMMAND ${CMAKE_COMMAND} -E remove_directory "${TOY3D_TONEMAP_SHADER_MAP_ROOT}"
    COMMAND ${CMAKE_COMMAND} -E make_directory "${TOY3D_TONEMAP_SHADER_MAP_ROOT}"
    COMMAND ${CMAKE_COMMAND} -E make_directory "${TOY3D_TONEMAP_SHADER_WORKING_DIR}"
    COMMAND $<TARGET_FILE:Toy3dShaderCompiler>
            --toolchain-root "${TOY3D_SHADER_TOOLCHAIN_ROOT}"
            compile-vulkan
            "${TOY3D_TONEMAP_SHADER_SOURCE}"
            "/Engine/Shaders/PostProcess/tonemap.shader"
            "Tonemap"
            "${TOY3D_TONEMAP_SHADER_MAP_ROOT}"
            "${TOY3D_TONEMAP_SHADER_WORKING_DIR}"
    COMMAND ${CMAKE_COMMAND} -E remove_directory "${BINARY_ROOT_DIR}/shader/tonemap"
    COMMAND ${CMAKE_COMMAND} -E make_directory "${BINARY_ROOT_DIR}/shader"
    COMMAND ${CMAKE_COMMAND} -E copy_directory
            "${TOY3D_TONEMAP_SHADER_MAP_ROOT}"
            "${BINARY_ROOT_DIR}/shader/tonemap"
    COMMAND ${CMAKE_COMMAND} -E touch "${TOY3D_TONEMAP_SHADER_STAMP}"
    DEPENDS
        Toy3dShaderCompiler
        "${TOY3D_TONEMAP_SHADER_SOURCE}"
    VERBATIM)

add_custom_command(
    OUTPUT "${TOY3D_IMGUI_SHADER_STAMP}"
    COMMAND ${CMAKE_COMMAND} -E remove_directory "${TOY3D_IMGUI_SHADER_MAP_ROOT}"
    COMMAND ${CMAKE_COMMAND} -E make_directory "${TOY3D_IMGUI_SHADER_MAP_ROOT}"
    COMMAND ${CMAKE_COMMAND} -E make_directory "${TOY3D_IMGUI_SHADER_WORKING_DIR}"
    COMMAND $<TARGET_FILE:Toy3dShaderCompiler>
            --toolchain-root "${TOY3D_SHADER_TOOLCHAIN_ROOT}"
            compile-vulkan
            "${TOY3D_IMGUI_SHADER_SOURCE}"
            "/Engine/Shaders/UI/imgui.shader"
            "ImGui"
            "${TOY3D_IMGUI_SHADER_MAP_ROOT}"
            "${TOY3D_IMGUI_SHADER_WORKING_DIR}"
    COMMAND ${CMAKE_COMMAND} -E remove_directory "${BINARY_ROOT_DIR}/shader/imgui"
    COMMAND ${CMAKE_COMMAND} -E make_directory "${BINARY_ROOT_DIR}/shader"
    COMMAND ${CMAKE_COMMAND} -E copy_directory
            "${TOY3D_IMGUI_SHADER_MAP_ROOT}"
            "${BINARY_ROOT_DIR}/shader/imgui"
    COMMAND ${CMAKE_COMMAND} -E touch "${TOY3D_IMGUI_SHADER_STAMP}"
    DEPENDS
        Toy3dShaderCompiler
        "${TOY3D_IMGUI_SHADER_SOURCE}"
    VERBATIM)

add_custom_command(
    OUTPUT "${TOY3D_OUTPUT_SHADER_DEPLOY_STAMP}"
    COMMAND ${CMAKE_COMMAND} -E remove_directory
            "${TOY3D_OUTPUT_SHADER_DEPLOY_ROOT}"
    COMMAND ${CMAKE_COMMAND} -E make_directory
            "${TOY3D_OUTPUT_SHADER_DEPLOY_ROOT}"
    COMMAND ${CMAKE_COMMAND} -E copy_directory
            "${TOY3D_TONEMAP_SHADER_MAP_ROOT}"
            "${TOY3D_OUTPUT_SHADER_DEPLOY_ROOT}"
    COMMAND ${CMAKE_COMMAND} -E copy_directory
            "${TOY3D_IMGUI_SHADER_MAP_ROOT}"
            "${TOY3D_OUTPUT_SHADER_DEPLOY_ROOT}"
    COMMAND ${CMAKE_COMMAND} -E touch "${TOY3D_OUTPUT_SHADER_DEPLOY_STAMP}"
    DEPENDS
        "${TOY3D_TONEMAP_SHADER_STAMP}"
        "${TOY3D_IMGUI_SHADER_STAMP}"
    VERBATIM)

add_custom_target(Toy3dShaders
    DEPENDS
        "${TOY3D_UNLIT_SHADER_STAMP}"
        "${TOY3D_PHONG_SHADER_STAMP}"
        "${TOY3D_TONEMAP_SHADER_STAMP}"
        "${TOY3D_IMGUI_SHADER_STAMP}"
        "${TOY3D_OUTPUT_SHADER_DEPLOY_STAMP}")
set_target_properties(Toy3dShaders PROPERTIES FOLDER "Engine")
set(TOY3D_SHADER_MAP_OUTPUT_DIR "${TOY3D_SHADER_MAP_OUTPUT_DIR}" PARENT_SCOPE)
