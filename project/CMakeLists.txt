set(TOY3D_CUBE_TEST_SOURCES
    "cube_actor.cpp"
    "cube_actor.h"
    "cube_application.cpp"
    "cube_application.h"
    "cube_test.cpp")

if(APPLE)
    set(TOY3D_CUBE_TEST_ICON "${TOY3D_ROOT_DIR}/engine/asset/mac/Toy3d.icns")
    add_executable(Toy3dCubeTest MACOSX_BUNDLE
        ${TOY3D_CUBE_TEST_SOURCES}
        "${TOY3D_CUBE_TEST_ICON}")
    set_source_files_properties("${TOY3D_CUBE_TEST_ICON}" PROPERTIES
        MACOSX_PACKAGE_LOCATION "Resources")
else()
    add_executable(Toy3dCubeTest ${TOY3D_CUBE_TEST_SOURCES})
endif()

add_dependencies(Toy3dCubeTest Toy3dShaders)
target_compile_features(Toy3dCubeTest PRIVATE cxx_std_17)
set_target_properties(Toy3dCubeTest PROPERTIES
    CXX_EXTENSIONS OFF
    FOLDER "Project")
if(APPLE)
    set_target_properties(Toy3dCubeTest PROPERTIES
        MACOSX_BUNDLE_BUNDLE_NAME "Toy3dCubeTest"
        MACOSX_BUNDLE_GUI_IDENTIFIER "com.toy3d.CubeTest"
        MACOSX_BUNDLE_ICON_FILE "Toy3d.icns"
        XCODE_ATTRIBUTE_CODE_SIGNING_ALLOWED "NO"
        XCODE_ATTRIBUTE_CODE_SIGNING_REQUIRED "NO")
endif()
target_compile_options(Toy3dCubeTest
    PRIVATE
        "$<$<COMPILE_LANG_AND_ID:CXX,MSVC>:/utf-8>")
target_compile_definitions(Toy3dCubeTest
    PRIVATE
        "TOY3D_SHADER_MAP_ENTRY_ROOT=\"${BINARY_ROOT_DIR}shader/phong\"")
target_link_libraries(Toy3dCubeTest PRIVATE Toy3dRuntime imgui)

set(TOY3D_CUBE_TEST_POST_BUILD_COMMANDS
    COMMAND ${CMAKE_COMMAND} -E make_directory "${BINARY_ROOT_DIR}"
    COMMAND ${CMAKE_COMMAND} -E make_directory "${BINARY_ROOT_DIR}saved")
if(APPLE)
    list(APPEND TOY3D_CUBE_TEST_POST_BUILD_COMMANDS
        COMMAND ${CMAKE_COMMAND} -E copy_directory
                "$<TARGET_BUNDLE_DIR:Toy3dCubeTest>"
                "${BINARY_ROOT_DIR}Toy3dCubeTest.app"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "$<TARGET_FILE:Toy3dCubeTest>"
                "${BINARY_ROOT_DIR}Toy3dCubeTest")
else()
    list(APPEND TOY3D_CUBE_TEST_POST_BUILD_COMMANDS
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "$<TARGET_FILE:Toy3dCubeTest>"
                "${BINARY_ROOT_DIR}$<TARGET_FILE_NAME:Toy3dCubeTest>")
endif()
list(APPEND TOY3D_CUBE_TEST_POST_BUILD_COMMANDS
    COMMAND ${CMAKE_COMMAND} -E remove_directory "${BINARY_ROOT_DIR}/asset"
    COMMAND ${CMAKE_COMMAND} -E copy_directory
            "${TOY3D_ROOT_DIR}/engine/asset"
            "${BINARY_ROOT_DIR}/asset")
add_custom_command(TARGET Toy3dCubeTest POST_BUILD
    ${TOY3D_CUBE_TEST_POST_BUILD_COMMANDS})
