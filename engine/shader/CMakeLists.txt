add_library(Toy3dShaderFormat STATIC)
target_sources(Toy3dShaderFormat
    PRIVATE
        "builtin_shader_parameters.h"
        "format/shader_format_hash.cpp"
        "format/shader_format_types.h"
        "format/shader_map_entry.h"
        "format/shader_parameter_schema.cpp"
        "format/shader_map_entry_reader.cpp")
target_compile_features(Toy3dShaderFormat PUBLIC cxx_std_17)
set_target_properties(Toy3dShaderFormat PROPERTIES
    CXX_EXTENSIONS OFF
    FOLDER "Engine/Shader")
target_include_directories(Toy3dShaderFormat
    PUBLIC
        "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>")
target_link_libraries(Toy3dShaderFormat PUBLIC Toy3dFileSystem Toy3dHash)

set(TOY3D_SHADER_MAP_OUTPUT_DIR "${CMAKE_BINARY_DIR}/generated/shader_map")
set(TOY3D_SHADER_PARAMETERS_OUTPUT_DIR "${CMAKE_BINARY_DIR}/generated/shader_parameters")
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
set(TOY3D_HIT_PROXY_SHADER_SOURCE
    "${CMAKE_CURRENT_SOURCE_DIR}/builtin/editor/hit_proxy.shader")
set(TOY3D_HIT_PROXY_SHADER_MAP_ROOT "${TOY3D_SHADER_MAP_OUTPUT_DIR}/hit_proxy")
set(TOY3D_HIT_PROXY_SHADER_WORKING_DIR "${CMAKE_BINARY_DIR}/generated/shader_work/hit_proxy")
set(TOY3D_HIT_PROXY_SHADER_STAMP "${TOY3D_SHADER_MAP_OUTPUT_DIR}/hit_proxy.stamp")
set(TOY3D_OUTPUT_SHADER_DEPLOY_ROOT "${BINARY_ROOT_DIR}/shader/output")
set(TOY3D_OUTPUT_SHADER_DEPLOY_STAMP "${TOY3D_SHADER_MAP_OUTPUT_DIR}/output_deploy.stamp")
set(TOY3D_BUILTIN_SHADER_PARAMETERS_HEADER
    "${TOY3D_SHADER_PARAMETERS_OUTPUT_DIR}/builtin_shader_parameters.generated.h")
set(TOY3D_UNLIT_SHADER_PARAMETERS_HEADER
    "${TOY3D_SHADER_PARAMETERS_OUTPUT_DIR}/toy3d_surface_unlit.generated.h")
set(TOY3D_PHONG_SHADER_PARAMETERS_HEADER
    "${TOY3D_SHADER_PARAMETERS_OUTPUT_DIR}/toy3d_surface_phong.generated.h")
set(TOY3D_TONEMAP_SHADER_PARAMETERS_HEADER
    "${TOY3D_SHADER_PARAMETERS_OUTPUT_DIR}/toy3d_postprocess_tonemap.generated.h")
set(TOY3D_IMGUI_SHADER_PARAMETERS_HEADER
    "${TOY3D_SHADER_PARAMETERS_OUTPUT_DIR}/toy3d_ui_imgui.generated.h")
set(TOY3D_HIT_PROXY_SHADER_PARAMETERS_HEADER
    "${TOY3D_SHADER_PARAMETERS_OUTPUT_DIR}/toy3d_editor_hitproxy.generated.h")
set(TOY3D_SHADER_PARAMETERS_HEADERS
    "${TOY3D_BUILTIN_SHADER_PARAMETERS_HEADER}"
    "${TOY3D_UNLIT_SHADER_PARAMETERS_HEADER}"
    "${TOY3D_PHONG_SHADER_PARAMETERS_HEADER}"
    "${TOY3D_TONEMAP_SHADER_PARAMETERS_HEADER}"
    "${TOY3D_IMGUI_SHADER_PARAMETERS_HEADER}"
    "${TOY3D_HIT_PROXY_SHADER_PARAMETERS_HEADER}")
set(TOY3D_SHADER_PARAMETERS_STAMP
    "${TOY3D_SHADER_PARAMETERS_OUTPUT_DIR}/generation.stamp")
set(TOY3D_SHADER_PARAMETERS_MANIFEST
    "${TOY3D_SHADER_PARAMETERS_OUTPUT_DIR}/shader_parameters.outputs")
set(TOY3D_SHADER_PARAMETERS_DEPENDENCIES
    "${TOY3D_SHADER_PARAMETERS_OUTPUT_DIR}/shader_parameters.dependencies")

add_custom_command(
    OUTPUT "${TOY3D_SHADER_PARAMETERS_STAMP}"
    BYPRODUCTS
        ${TOY3D_SHADER_PARAMETERS_HEADERS}
        "${TOY3D_SHADER_PARAMETERS_MANIFEST}"
        "${TOY3D_SHADER_PARAMETERS_DEPENDENCIES}"
    COMMAND ${CMAKE_COMMAND} -E make_directory "${TOY3D_SHADER_PARAMETERS_OUTPUT_DIR}"
    COMMAND $<TARGET_FILE:Toy3dShaderCompiler>
            generate-parameters
            "${TOY3D_SHADER_PARAMETERS_OUTPUT_DIR}"
            "${TOY3D_UNLIT_SHADER_SOURCE}"
            "${TOY3D_PHONG_SHADER_SOURCE}"
            "${TOY3D_TONEMAP_SHADER_SOURCE}"
            "${TOY3D_IMGUI_SHADER_SOURCE}"
            "${TOY3D_HIT_PROXY_SHADER_SOURCE}"
    COMMAND ${CMAKE_COMMAND} -E touch "${TOY3D_SHADER_PARAMETERS_STAMP}"
    DEPENDS
        Toy3dShaderCompiler
        "${TOY3D_UNLIT_SHADER_SOURCE}"
        "${TOY3D_PHONG_SHADER_SOURCE}"
        "${TOY3D_TONEMAP_SHADER_SOURCE}"
        "${TOY3D_IMGUI_SHADER_SOURCE}"
        "${TOY3D_HIT_PROXY_SHADER_SOURCE}"
    VERBATIM)
set_source_files_properties(${TOY3D_SHADER_PARAMETERS_HEADERS} PROPERTIES GENERATED TRUE)
add_custom_target(Toy3dShaderParameters DEPENDS "${TOY3D_SHADER_PARAMETERS_STAMP}")
set_target_properties(Toy3dShaderParameters PROPERTIES FOLDER "Engine/Shader")

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
    OUTPUT "${TOY3D_HIT_PROXY_SHADER_STAMP}"
    COMMAND ${CMAKE_COMMAND} -E remove_directory "${TOY3D_HIT_PROXY_SHADER_MAP_ROOT}"
    COMMAND ${CMAKE_COMMAND} -E make_directory "${TOY3D_HIT_PROXY_SHADER_MAP_ROOT}"
    COMMAND ${CMAKE_COMMAND} -E make_directory "${TOY3D_HIT_PROXY_SHADER_WORKING_DIR}"
    COMMAND $<TARGET_FILE:Toy3dShaderCompiler>
            --toolchain-root "${TOY3D_SHADER_TOOLCHAIN_ROOT}"
            compile-vulkan
            "${TOY3D_HIT_PROXY_SHADER_SOURCE}"
            "/Engine/Shaders/Editor/hit_proxy.shader"
            "HitProxy"
            "${TOY3D_HIT_PROXY_SHADER_MAP_ROOT}"
            "${TOY3D_HIT_PROXY_SHADER_WORKING_DIR}"
    COMMAND ${CMAKE_COMMAND} -E touch "${TOY3D_HIT_PROXY_SHADER_STAMP}"
    DEPENDS
        Toy3dShaderCompiler
        "${TOY3D_HIT_PROXY_SHADER_SOURCE}"
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
    COMMAND ${CMAKE_COMMAND} -E copy_directory
            "${TOY3D_HIT_PROXY_SHADER_MAP_ROOT}"
            "${TOY3D_OUTPUT_SHADER_DEPLOY_ROOT}"
    COMMAND ${CMAKE_COMMAND} -E touch "${TOY3D_OUTPUT_SHADER_DEPLOY_STAMP}"
    DEPENDS
        "${TOY3D_TONEMAP_SHADER_STAMP}"
        "${TOY3D_IMGUI_SHADER_STAMP}"
        "${TOY3D_HIT_PROXY_SHADER_STAMP}"
    VERBATIM)

add_custom_target(Toy3dShaders
    DEPENDS
        "${TOY3D_UNLIT_SHADER_STAMP}"
        "${TOY3D_PHONG_SHADER_STAMP}"
        "${TOY3D_TONEMAP_SHADER_STAMP}"
        "${TOY3D_IMGUI_SHADER_STAMP}"
        "${TOY3D_HIT_PROXY_SHADER_STAMP}"
        "${TOY3D_OUTPUT_SHADER_DEPLOY_STAMP}")
set_target_properties(Toy3dShaders PROPERTIES FOLDER "Engine")
set(TOY3D_SHADER_MAP_OUTPUT_DIR "${TOY3D_SHADER_MAP_OUTPUT_DIR}" PARENT_SCOPE)
set(TOY3D_SHADER_PARAMETERS_OUTPUT_DIR "${TOY3D_SHADER_PARAMETERS_OUTPUT_DIR}" PARENT_SCOPE)
set(TOY3D_SHADER_PARAMETERS_HEADERS "${TOY3D_SHADER_PARAMETERS_HEADERS}" PARENT_SCOPE)
