add_executable(Toy3dCubeTest "cube_test.cpp")
add_dependencies(Toy3dCubeTest Toy3dShaders)
target_compile_features(Toy3dCubeTest PRIVATE cxx_std_17)
set_target_properties(Toy3dCubeTest PROPERTIES
    CXX_EXTENSIONS OFF
    FOLDER "Project")
target_compile_options(Toy3dCubeTest
    PRIVATE
        "$<$<COMPILE_LANG_AND_ID:CXX,MSVC>:/utf-8>")
target_compile_definitions(Toy3dCubeTest
    PRIVATE
        "TOY3D_SHADER_MAP_ENTRY_ROOT=\"${BINARY_ROOT_DIR}shader/phong\"")
target_link_libraries(Toy3dCubeTest PRIVATE Toy3dRuntime)

set(TOY3D_CUBE_TEST_POST_BUILD_COMMANDS
    COMMAND ${CMAKE_COMMAND} -E make_directory "${BINARY_ROOT_DIR}"
    COMMAND ${CMAKE_COMMAND} -E make_directory "${BINARY_ROOT_DIR}saved"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "$<TARGET_FILE:Toy3dCubeTest>"
            "${BINARY_ROOT_DIR}$<TARGET_FILE_NAME:Toy3dCubeTest>"
    COMMAND ${CMAKE_COMMAND} -E remove_directory "${BINARY_ROOT_DIR}/asset"
    COMMAND ${CMAKE_COMMAND} -E copy_directory
            "${TOY3D_ROOT_DIR}/engine/asset"
            "${BINARY_ROOT_DIR}/asset")
add_custom_command(TARGET Toy3dCubeTest POST_BUILD
    ${TOY3D_CUBE_TEST_POST_BUILD_COMMANDS})
