set(TOY3D_SHADER_BINARY_DIR "${CMAKE_BINARY_DIR}/generated/shader")
set(TOY3D_SHADER_SOURCES
    "${CMAKE_CURRENT_SOURCE_DIR}/test_pass.vert"
    "${CMAKE_CURRENT_SOURCE_DIR}/test_pass.frag")

set(TOY3D_SHADER_OUTPUTS)
foreach(TOY3D_SHADER_SOURCE IN LISTS TOY3D_SHADER_SOURCES)
    get_filename_component(TOY3D_SHADER_NAME "${TOY3D_SHADER_SOURCE}" NAME)
    set(TOY3D_SHADER_OUTPUT "${TOY3D_SHADER_BINARY_DIR}/${TOY3D_SHADER_NAME}.spv")
    add_custom_command(
        OUTPUT "${TOY3D_SHADER_OUTPUT}"
        COMMAND ${CMAKE_COMMAND} -E make_directory "${TOY3D_SHADER_BINARY_DIR}"
        COMMAND "${glslangValidator_executable}" -V "${TOY3D_SHADER_SOURCE}" -o "${TOY3D_SHADER_OUTPUT}"
        DEPENDS "${TOY3D_SHADER_SOURCE}"
        VERBATIM)
    list(APPEND TOY3D_SHADER_OUTPUTS "${TOY3D_SHADER_OUTPUT}")
endforeach()

add_custom_target(Toy3dShaders DEPENDS ${TOY3D_SHADER_OUTPUTS})
add_dependencies(Toy3dShaders Toy3dShaderToolchainSync)
set_target_properties(Toy3dShaders PROPERTIES FOLDER "Engine")
set(TOY3D_SHADER_OUTPUTS "${TOY3D_SHADER_OUTPUTS}" PARENT_SCOPE)
