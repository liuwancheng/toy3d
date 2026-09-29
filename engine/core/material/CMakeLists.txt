set(TOY3D_MATERIAL_GENERATED_DIR "${CMAKE_CURRENT_BINARY_DIR}/generated")
set(TOY3D_MATERIAL_GENERATED_HEADER "${TOY3D_MATERIAL_GENERATED_DIR}/material_asset_reflection.h")
set(TOY3D_MATERIAL_GENERATED_SOURCE "${TOY3D_MATERIAL_GENERATED_DIR}/material_asset_reflection.cpp")
add_custom_command(
    OUTPUT "${TOY3D_MATERIAL_GENERATED_HEADER}" "${TOY3D_MATERIAL_GENERATED_SOURCE}"
    COMMAND "${CMAKE_COMMAND}" -E make_directory "${TOY3D_MATERIAL_GENERATED_DIR}"
    COMMAND $<TARGET_FILE:Toy3dReflectionCodegen>
        --input "${CMAKE_CURRENT_SOURCE_DIR}/material_asset_data.h"
        --header "${TOY3D_MATERIAL_GENERATED_HEADER}"
        --source "${TOY3D_MATERIAL_GENERATED_SOURCE}"
        --function register_material_asset_types
    DEPENDS Toy3dReflectionCodegen "${CMAKE_CURRENT_SOURCE_DIR}/material_asset_data.h"
    VERBATIM)
add_library(Toy3dMaterialAsset STATIC)
target_sources(Toy3dMaterialAsset PRIVATE "material_asset.h" "material_asset.cpp" "material_asset_data.h"
    "${TOY3D_MATERIAL_GENERATED_HEADER}" "${TOY3D_MATERIAL_GENERATED_SOURCE}")
target_compile_features(Toy3dMaterialAsset PUBLIC cxx_std_17)
set_target_properties(Toy3dMaterialAsset PROPERTIES CXX_EXTENSIONS OFF FOLDER "Engine/Core")
target_include_directories(Toy3dMaterialAsset PUBLIC
    "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/..>" "$<BUILD_INTERFACE:${TOY3D_MATERIAL_GENERATED_DIR}>")
target_link_libraries(Toy3dMaterialAsset PUBLIC Toy3dResource Toy3dMath Toy3dShaderFormat)
if(BUILD_TESTING)
    add_executable(Toy3dMaterialAssetTests "tests/material_asset_tests.cpp")
    target_compile_features(Toy3dMaterialAssetTests PRIVATE cxx_std_17)
    set_target_properties(Toy3dMaterialAssetTests PROPERTIES CXX_EXTENSIONS OFF FOLDER "Tests/Core")
    target_link_libraries(Toy3dMaterialAssetTests PRIVATE Toy3dMaterialAsset)
    add_test(NAME Toy3dCore.MaterialAsset COMMAND Toy3dMaterialAssetTests)
endif()
