set(TOY3D_SHADER_COMPILER_CORE_SOURCES
    "shader_map/shader_code_entry.cpp"
    "shader_map/shader_code_entry.h"
    "shader_map/shader_map_entry.cpp"
    "shader_map/shader_map_entry.h"
    "shader_map/shader_map_storage.cpp"
    "shader_map/shader_map_storage.h"
    "codegen/binding_codegen.cpp"
    "codegen/binding_codegen.h"
    "codegen/cpp_identifier.cpp"
    "codegen/cpp_identifier.h"
    "codegen/shader_parameters_codegen.cpp"
    "codegen/shader_parameters_codegen.h"
    "codegen/shader_parameters_writer.cpp"
    "codegen/shader_parameters_writer.h"
    "compiler/compile_request.cpp"
    "compiler/compile_request.h"
    "compiler/dxc_adapter.cpp"
    "compiler/dxc_adapter.h"
    "compiler/include_resolver.cpp"
    "compiler/include_resolver.h"
    "compiler/program_compiler.cpp"
    "compiler/program_compiler.h"
    "compiler/shader_compiler.cpp"
    "compiler/shader_compiler.h"
    "compiler/shader_source_provider.cpp"
    "compiler/shader_source_provider.h"
    "compiler/toolchain_manifest.cpp"
    "compiler/toolchain_manifest.h"
    "compiler/variant_permutation.cpp"
    "compiler/variant_permutation.h"
    "frontend/diagnostic.cpp"
    "frontend/diagnostic.h"
    "frontend/shader_ast.h"
    "frontend/shader_parser.cpp"
    "frontend/shader_parser.h"
    "frontend/source_location.h"
    "frontend/token.h"
    "frontend/tokenizer.cpp"
    "frontend/tokenizer.h"
    "layout/binding_allocator.cpp"
    "layout/binding_allocator.h"
    "layout/builtin_shader_parameter_inputs.cpp"
    "layout/shader_layout.cpp"
    "layout/shader_layout.h"
    "reflection/spirv_reflection.cpp"
    "reflection/spirv_reflection.h")

add_library(Toy3dShaderCompilerCore STATIC)
target_compile_definitions(Toy3dShaderCompilerCore PRIVATE WITH_EDITORONLY_DATA=$<BOOL:${TOY3D_SHADER_EDITORONLY_DATA}>)
target_sources(Toy3dShaderCompilerCore
    PRIVATE
        ${TOY3D_SHADER_COMPILER_CORE_SOURCES})
target_compile_features(Toy3dShaderCompilerCore PUBLIC cxx_std_17)
set_target_properties(Toy3dShaderCompilerCore PROPERTIES
    CXX_EXTENSIONS OFF
    FOLDER "Engine/Tools")
target_include_directories(Toy3dShaderCompilerCore
    PUBLIC
        "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>")
target_link_libraries(Toy3dShaderCompilerCore
    PRIVATE
 Toy3dCore
    PUBLIC

 Toy3dShaderFormat
 Toy3d::SpirvReflect)

add_executable(Toy3dShaderCompiler "main.cpp")
target_compile_features(Toy3dShaderCompiler PRIVATE cxx_std_17)
set_target_properties(Toy3dShaderCompiler PROPERTIES
    CXX_EXTENSIONS OFF
    FOLDER "Engine/Tools")
target_link_libraries(Toy3dShaderCompiler
    PRIVATE
 Toy3dCore

 Toy3dShaderCompilerCore)
target_compile_definitions(Toy3dShaderCompiler
    PRIVATE
        "TOY3D_SHADER_COMPILER_LOG_DIR=\"${BINARY_ROOT_DIR}saved/logs\"")
add_dependencies(Toy3dShaderCompiler Toy3dShaderToolchainSync)
add_custom_command(
    TARGET Toy3dShaderCompiler
    POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory "${BINARY_ROOT_DIR}"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "$<TARGET_FILE:Toy3dShaderCompiler>"
            "${BINARY_ROOT_DIR}$<TARGET_FILE_NAME:Toy3dShaderCompiler>"
    VERBATIM)

if(BUILD_TESTING)
    add_executable(Toy3dShaderCompilerFrontendTests "tests/frontend_tests.cpp")
    target_compile_features(Toy3dShaderCompilerFrontendTests PRIVATE cxx_std_17)
    set_target_properties(Toy3dShaderCompilerFrontendTests PROPERTIES
        CXX_EXTENSIONS OFF
        FOLDER "Engine/Tests")
    target_link_libraries(Toy3dShaderCompilerFrontendTests
        PRIVATE
 Toy3dShaderCompilerCore)
    target_compile_definitions(Toy3dShaderCompilerFrontendTests
        PRIVATE
            "TOY3D_SHADER_TEST_DATA_DIR=\"${CMAKE_CURRENT_SOURCE_DIR}/tests/data\"")
    add_test(NAME Toy3dShaderCompiler.Frontend
        COMMAND Toy3dShaderCompilerFrontendTests)

    add_executable(Toy3dShaderCompilerLayoutTests "tests/layout_tests.cpp")
    target_compile_features(Toy3dShaderCompilerLayoutTests PRIVATE cxx_std_17)
    set_target_properties(Toy3dShaderCompilerLayoutTests PROPERTIES
        CXX_EXTENSIONS OFF
        FOLDER "Engine/Tests")
    target_link_libraries(Toy3dShaderCompilerLayoutTests
        PRIVATE
 Toy3dShaderCompilerCore)
    target_compile_definitions(Toy3dShaderCompilerLayoutTests
        PRIVATE
            WITH_EDITORONLY_DATA=$<BOOL:${TOY3D_SHADER_EDITORONLY_DATA}>
            "TOY3D_SHADER_PARAMETERS_WRITER_TEST_DIR=\"${CMAKE_CURRENT_BINARY_DIR}/tests/generated/shader_parameters\"")
    add_test(NAME Toy3dShaderCompiler.Layout
        COMMAND Toy3dShaderCompilerLayoutTests)

    add_executable(Toy3dShaderCompilerCompileTests "tests/compile_tests.cpp")
    target_compile_features(Toy3dShaderCompilerCompileTests PRIVATE cxx_std_17)
    set_target_properties(Toy3dShaderCompilerCompileTests PROPERTIES
        CXX_EXTENSIONS OFF
        FOLDER "Engine/Tests")
    target_link_libraries(Toy3dShaderCompilerCompileTests
        PRIVATE
 Toy3dCore
 Toy3dShaderCompilerCore)
    set(TOY3D_SHADER_TEST_DXC "" CACHE FILEPATH "Explicit DXC executable used only by Shader compiler integration tests")
    set(TOY3D_SHADER_TEST_SPIRV_VAL "" CACHE FILEPATH "Explicit spirv-val executable used only by Shader compiler integration tests")
    set(TOY3D_SHADER_TEST_TOOLCHAIN_ROOT "${TOY3D_SHADER_TOOLCHAIN_ROOT}" CACHE PATH
        "Explicit locked Toy3dShaderToolchain bundle used by integration tests")
    if(TOY3D_SHADER_TEST_TOOLCHAIN_ROOT)
        if(TOY3D_SHADER_TEST_DXC OR TOY3D_SHADER_TEST_SPIRV_VAL)
            message(FATAL_ERROR "TOY3D_SHADER_TEST_TOOLCHAIN_ROOT cannot be combined with individual tool paths")
        endif()
        if(NOT EXISTS "${TOY3D_SHADER_TEST_TOOLCHAIN_ROOT}/Toy3dShaderToolchain.manifest")
            message(FATAL_ERROR "TOY3D_SHADER_TEST_TOOLCHAIN_ROOT has no manifest: ${TOY3D_SHADER_TEST_TOOLCHAIN_ROOT}")
        endif()
        target_compile_definitions(Toy3dShaderCompilerCompileTests
            PRIVATE
                "TOY3D_SHADER_TEST_TOOLCHAIN_ROOT=\"${TOY3D_SHADER_TEST_TOOLCHAIN_ROOT}\"")
    elseif(TOY3D_SHADER_TEST_DXC AND TOY3D_SHADER_TEST_SPIRV_VAL)
        if(NOT EXISTS "${TOY3D_SHADER_TEST_DXC}")
            message(FATAL_ERROR "TOY3D_SHADER_TEST_DXC does not exist: ${TOY3D_SHADER_TEST_DXC}")
        endif()
        if(NOT EXISTS "${TOY3D_SHADER_TEST_SPIRV_VAL}")
            message(FATAL_ERROR "TOY3D_SHADER_TEST_SPIRV_VAL does not exist: ${TOY3D_SHADER_TEST_SPIRV_VAL}")
        endif()
        target_compile_definitions(Toy3dShaderCompilerCompileTests
            PRIVATE
                "TOY3D_SHADER_TEST_DXC=\"${TOY3D_SHADER_TEST_DXC}\""
                "TOY3D_SHADER_TEST_SPIRV_VAL=\"${TOY3D_SHADER_TEST_SPIRV_VAL}\"")
    elseif(TOY3D_SHADER_TEST_DXC OR TOY3D_SHADER_TEST_SPIRV_VAL)
        message(FATAL_ERROR "DXC integration tests require both TOY3D_SHADER_TEST_DXC and TOY3D_SHADER_TEST_SPIRV_VAL")
    endif()
    add_test(NAME Toy3dShaderCompiler.Compile
        COMMAND Toy3dShaderCompilerCompileTests)
endif()
