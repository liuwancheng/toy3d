add_executable(Toy3dReflectionCodegen "main.cpp")
target_compile_features(Toy3dReflectionCodegen PRIVATE cxx_std_17)
set_target_properties(Toy3dReflectionCodegen PROPERTIES
    CXX_EXTENSIONS OFF
    FOLDER "Engine/Tools")

if(BUILD_TESTING)
    set(TOY3D_REFLECTION_GENERATED_DIR "${CMAKE_CURRENT_BINARY_DIR}/generated")
    file(MAKE_DIRECTORY "${TOY3D_REFLECTION_GENERATED_DIR}")
    set(TOY3D_REFLECTION_FIXTURE_HEADER "${TOY3D_REFLECTION_GENERATED_DIR}/fixture_reflection.h")
    set(TOY3D_REFLECTION_FIXTURE_SOURCE "${TOY3D_REFLECTION_GENERATED_DIR}/fixture_reflection.cpp")
    add_custom_command(
        OUTPUT "${TOY3D_REFLECTION_FIXTURE_HEADER}" "${TOY3D_REFLECTION_FIXTURE_SOURCE}"
        COMMAND $<TARGET_FILE:Toy3dReflectionCodegen>
            --input "${CMAKE_CURRENT_SOURCE_DIR}/tests/simple_asset.h"
            --input "${CMAKE_CURRENT_SOURCE_DIR}/tests/nested_asset.h"
            --input "${CMAKE_CURRENT_SOURCE_DIR}/tests/current_box.h"
            --input "${CMAKE_CURRENT_SOURCE_DIR}/tests/editor_asset.h"
            --input "${CMAKE_CURRENT_SOURCE_DIR}/tests/camera_asset.h"
            --input "${CMAKE_CURRENT_SOURCE_DIR}/tests/ref_asset.h"
            --input "${CMAKE_CURRENT_SOURCE_DIR}/tests/resource_kinds.h"
            --header "${TOY3D_REFLECTION_FIXTURE_HEADER}"
            --source "${TOY3D_REFLECTION_FIXTURE_SOURCE}"
            --function register_generated_fixture_types
        DEPENDS Toy3dReflectionCodegen
            "${CMAKE_CURRENT_SOURCE_DIR}/tests/simple_asset.h"
            "${CMAKE_CURRENT_SOURCE_DIR}/tests/nested_asset.h"
            "${CMAKE_CURRENT_SOURCE_DIR}/tests/current_box.h"
            "${CMAKE_CURRENT_SOURCE_DIR}/tests/editor_asset.h"
            "${CMAKE_CURRENT_SOURCE_DIR}/tests/camera_asset.h"
            "${CMAKE_CURRENT_SOURCE_DIR}/tests/ref_asset.h"
            "${CMAKE_CURRENT_SOURCE_DIR}/tests/resource_kinds.h"
        VERBATIM)

    add_executable(Toy3dReflectionCodegenTests
        "tests/codegen_tests.cpp"
        "${TOY3D_REFLECTION_FIXTURE_SOURCE}"
        "${TOY3D_REFLECTION_FIXTURE_HEADER}")
    target_compile_features(Toy3dReflectionCodegenTests PRIVATE cxx_std_17)
    set_target_properties(Toy3dReflectionCodegenTests PROPERTIES
        CXX_EXTENSIONS OFF
        FOLDER "Engine/Tests")
    target_include_directories(Toy3dReflectionCodegenTests PRIVATE "${CMAKE_CURRENT_BINARY_DIR}")
    target_link_libraries(Toy3dReflectionCodegenTests PRIVATE Toy3dReflection Toy3dSerialization Toy3dResource)
    add_test(NAME Toy3dCore.ReflectionCodegen COMMAND Toy3dReflectionCodegenTests)

    add_executable(Toy3dResourceKindTests
        "tests/resource_kind_tests.cpp"
        "${TOY3D_REFLECTION_FIXTURE_SOURCE}"
        "${TOY3D_REFLECTION_FIXTURE_HEADER}")
    target_compile_features(Toy3dResourceKindTests PRIVATE cxx_std_17)
    set_target_properties(Toy3dResourceKindTests PROPERTIES CXX_EXTENSIONS OFF FOLDER "Engine/Tests")
    target_include_directories(Toy3dResourceKindTests PRIVATE "${CMAKE_CURRENT_BINARY_DIR}")
    target_link_libraries(Toy3dResourceKindTests PRIVATE Toy3dResource Toy3dShaderCompilerCore)
    target_compile_definitions(Toy3dResourceKindTests PRIVATE
        "TOY3D_RESOURCE_SHADER_FIXTURE=\"${CMAKE_CURRENT_SOURCE_DIR}/../shader_compiler/tests/data/frontend_valid.shader\"")
    add_test(NAME Toy3dResource.Kinds COMMAND Toy3dResourceKindTests)
    add_test(NAME Toy3dCore.ReflectionCodegenInvalidUsage
        COMMAND "${CMAKE_COMMAND}"
            "-DGENERATOR=$<TARGET_FILE:Toy3dReflectionCodegen>"
            "-DINPUT=${CMAKE_CURRENT_SOURCE_DIR}/tests/invalid_usage.h"
            "-DOUTPUT_HEADER=${TOY3D_REFLECTION_GENERATED_DIR}/invalid_usage.h"
            "-DOUTPUT_SOURCE=${TOY3D_REFLECTION_GENERATED_DIR}/invalid_usage.cpp"
            "-DEXPECTED_LOCATION=InvalidUsage.value"
            -P "${CMAKE_CURRENT_SOURCE_DIR}/tests/invalid_usage_test.cmake")
    add_test(NAME Toy3dCore.ReflectionCodegenInvalidVisibleUsage
        COMMAND "${CMAKE_COMMAND}"
            "-DGENERATOR=$<TARGET_FILE:Toy3dReflectionCodegen>"
            "-DINPUT=${CMAKE_CURRENT_SOURCE_DIR}/tests/invalid_visible_usage.h"
            "-DOUTPUT_HEADER=${TOY3D_REFLECTION_GENERATED_DIR}/invalid_visible_usage.h"
            "-DOUTPUT_SOURCE=${TOY3D_REFLECTION_GENERATED_DIR}/invalid_visible_usage.cpp"
            "-DEXPECTED_LOCATION=InvalidVisibleUsage.value"
            -P "${CMAKE_CURRENT_SOURCE_DIR}/tests/invalid_usage_test.cmake")

    function(toy3d_add_invalid_reflection_test test_name fixture expected)
        add_test(NAME "Toy3dCore.${test_name}"
            COMMAND "${CMAKE_COMMAND}"
                "-DGENERATOR=$<TARGET_FILE:Toy3dReflectionCodegen>"
                "-DINPUT=${CMAKE_CURRENT_SOURCE_DIR}/tests/${fixture}"
                "-DOUTPUT_HEADER=${TOY3D_REFLECTION_GENERATED_DIR}/${test_name}.h"
                "-DOUTPUT_SOURCE=${TOY3D_REFLECTION_GENERATED_DIR}/${test_name}.cpp"
                "-DEXPECTED=${expected}"
                -P "${CMAKE_CURRENT_SOURCE_DIR}/tests/invalid_type_test.cmake")
    endfunction()
    toy3d_add_invalid_reflection_test(ReflectionInvalidPointer invalid_pointer.h
        "InvalidPointer.raw")
    toy3d_add_invalid_reflection_test(ReflectionInvalidElement invalid_element.h
        "InvalidElement.items")
    toy3d_add_invalid_reflection_test(ReflectionInvalidPrivate invalid_private.h
        "InvalidPrivate.secret")
endif()
