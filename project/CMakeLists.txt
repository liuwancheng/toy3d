add_library(ShadowDemo STATIC "src/rotating_actor.cpp" "src/shadow_demo_module.cpp")
target_compile_features(ShadowDemo PUBLIC cxx_std_17)
set_target_properties(ShadowDemo PROPERTIES CXX_EXTENSIONS OFF FOLDER "Game/ShadowDemo")
target_link_libraries(ShadowDemo PUBLIC Toy3dRuntime)
target_include_directories(ShadowDemo PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/src" "${CMAKE_CURRENT_BINARY_DIR}/generated")
target_compile_options(ShadowDemo PRIVATE "$<$<COMPILE_LANG_AND_ID:CXX,MSVC>:/utf-8>")
set(TOY3D_ROTATION_HEADER "${CMAKE_CURRENT_BINARY_DIR}/generated/rotation_reflection.h")
set(TOY3D_ROTATION_SOURCE "${CMAKE_CURRENT_BINARY_DIR}/generated/rotation_reflection.cpp")
add_custom_command(OUTPUT "${TOY3D_ROTATION_HEADER}" "${TOY3D_ROTATION_SOURCE}"
    COMMAND "${CMAKE_COMMAND}" -E make_directory "${CMAKE_CURRENT_BINARY_DIR}/generated"
    COMMAND $<TARGET_FILE:Toy3dReflectionCodegen> --input "${CMAKE_CURRENT_SOURCE_DIR}/src/rotating_actor.h"
        --header "${TOY3D_ROTATION_HEADER}" --source "${TOY3D_ROTATION_SOURCE}" --function register_rotation_types
    DEPENDS Toy3dReflectionCodegen "${CMAKE_CURRENT_SOURCE_DIR}/src/rotating_actor.h" VERBATIM)
target_sources(ShadowDemo PRIVATE "${TOY3D_ROTATION_HEADER}" "${TOY3D_ROTATION_SOURCE}")
toy3d_add_game_hosts(ShadowDemo "${CMAKE_CURRENT_SOURCE_DIR}/ShadowDemo.toy")

if(BUILD_TESTING)
    add_library(ShadowDemoIncompatibleModule MODULE "tests/incompatible_module.cpp")
    target_link_libraries(ShadowDemoIncompatibleModule PRIVATE Toy3dRuntime)
    target_compile_features(ShadowDemoIncompatibleModule PRIVATE cxx_std_17)
    set_target_properties(ShadowDemoIncompatibleModule PROPERTIES CXX_EXTENSIONS OFF FOLDER "Game/ShadowDemo/Tests")
    add_executable(ShadowDemoModuleTests "tests/module_host_tests.cpp")
    target_link_libraries(ShadowDemoModuleTests PRIVATE Toy3dEditorCore)
    target_compile_features(ShadowDemoModuleTests PRIVATE cxx_std_17)
    set_target_properties(ShadowDemoModuleTests PROPERTIES CXX_EXTENSIONS OFF FOLDER "Game/ShadowDemo/Tests")
    target_compile_definitions(ShadowDemoModuleTests PRIVATE
        "TOY3D_TEST_MODULE_PATH=\"$<TARGET_FILE:ShadowDemoModule>\""
        "TOY3D_TEST_BAD_MODULE_PATH=\"$<TARGET_FILE:ShadowDemoIncompatibleModule>\""
        "TOY3D_TEST_DEPLOYMENT=\"${BINARY_ROOT_DIR}\""
        "TOY3D_TEST_ENGINE_ASSETS=\"${TOY3D_ROOT_DIR}/engine/asset\""
        "TOY3D_TEST_EDITOR_RESOURCES=\"${TOY3D_ROOT_DIR}/engine/editor/resources\"")
    add_dependencies(ShadowDemoModuleTests ShadowDemoModule ShadowDemoIncompatibleModule)
    add_test(NAME ShadowDemo.SharedHost COMMAND ShadowDemoModuleTests)
    set_tests_properties(ShadowDemo.SharedHost PROPERTIES TIMEOUT 30)
    if(WIN32)
        add_test(NAME ShadowDemo.Package COMMAND "${CMAKE_COMMAND}"
            "-DTOY3D_PROJECT=${CMAKE_CURRENT_SOURCE_DIR}/ShadowDemo.toy"
            "-DTOY3D_BUILD=${CMAKE_BINARY_DIR}" "-DTOY3D_CONFIGURATION=$<CONFIG>"
            -P "${TOY3D_ROOT_DIR}/engine/build/cmake/package_project_tests.cmake")
        set_tests_properties(ShadowDemo.Package PROPERTIES TIMEOUT 600 RUN_SERIAL TRUE)
    endif()
    add_executable(ShadowDemoTests "tests/rotating_actor_tests.cpp")
    if(TOY3D_ENABLE_SHADER_MAP_ENTRY_LOADING)
        target_sources(ShadowDemoTests PRIVATE "tests/editor_play_tests.cpp")
        target_compile_definitions(ShadowDemoTests PRIVATE "TOY3D_TEST_SHADER_ROOT=\"${BINARY_ROOT_DIR}shader/phong\"")
    endif()
    target_link_libraries(ShadowDemoTests PRIVATE ShadowDemo Toy3dEditorCore)
    target_compile_features(ShadowDemoTests PRIVATE cxx_std_17)
    set_target_properties(ShadowDemoTests PROPERTIES CXX_EXTENSIONS OFF FOLDER "Game/ShadowDemo/Tests")
    target_compile_options(ShadowDemoTests PRIVATE "$<$<COMPILE_LANG_AND_ID:CXX,MSVC>:/utf-8>")
    target_compile_definitions(ShadowDemoTests PRIVATE "TOY3D_TEST_ENGINE_ASSETS=\"${TOY3D_ROOT_DIR}/engine/asset\"")
    target_compile_definitions(ShadowDemoTests PRIVATE "TOY3D_TEST_EDITOR_RESOURCES=\"${TOY3D_ROOT_DIR}/engine/editor/resources\"")
    add_test(NAME ShadowDemo.RotatingActor COMMAND ShadowDemoTests)
    set_tests_properties(ShadowDemo.RotatingActor PROPERTIES TIMEOUT 30)
endif()
