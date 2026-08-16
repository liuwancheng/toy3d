set(TOY3D_SHADER_COMPILER_FRONTEND_SOURCES
    "frontend/diagnostic.h"
    "frontend/shader_ast.h"
    "frontend/shader_parser.cpp"
    "frontend/shader_parser.h"
    "frontend/source_location.h"
    "frontend/token.h"
    "frontend/tokenizer.cpp"
    "frontend/tokenizer.h")

add_library(Toy3dShaderCompilerCore STATIC)
target_sources(Toy3dShaderCompilerCore
    PRIVATE
        ${TOY3D_SHADER_COMPILER_FRONTEND_SOURCES})
target_compile_features(Toy3dShaderCompilerCore PUBLIC cxx_std_17)
set_target_properties(Toy3dShaderCompilerCore PROPERTIES
    CXX_EXTENSIONS OFF
    FOLDER "Engine/Tools")
target_include_directories(Toy3dShaderCompilerCore
    PUBLIC
        "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>")

add_executable(Toy3dShaderCompiler "main.cpp")
target_compile_features(Toy3dShaderCompiler PRIVATE cxx_std_17)
set_target_properties(Toy3dShaderCompiler PROPERTIES
    CXX_EXTENSIONS OFF
    FOLDER "Engine/Tools")
target_link_libraries(Toy3dShaderCompiler PRIVATE Toy3dShaderCompilerCore)

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
endif()
