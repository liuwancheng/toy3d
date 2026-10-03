# Unmodified upstream MikkTSpace revision 3e895b49d05ea07e4c2133156cfa94369e19e409.
# Keep this Tools-private dependency in source so clean builds work without downloading it.
set(TOY3D_MIKKTSPACE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/thirdparty/mikktspace")
set(TOY3D_MIKKTSPACE_FILES "mikktspace.h" "mikktspace.c")
set(TOY3D_MIKKTSPACE_HASHES
    "17fc433894f24c73753d548086cc4d8c5c0379f4a6edfb98b5da243e4f0bc3d0"
    "de87e74107df766ce68108801262bd8d53899414236b59810509a8fc2a51e288")
foreach(TOY3D_MIKKTSPACE_INDEX RANGE 0 1)
    list(GET TOY3D_MIKKTSPACE_FILES ${TOY3D_MIKKTSPACE_INDEX} TOY3D_MIKKTSPACE_FILE)
    list(GET TOY3D_MIKKTSPACE_HASHES ${TOY3D_MIKKTSPACE_INDEX} TOY3D_MIKKTSPACE_HASH)
    set(TOY3D_MIKKTSPACE_PATH "${TOY3D_MIKKTSPACE_DIR}/${TOY3D_MIKKTSPACE_FILE}")
    if(NOT EXISTS "${TOY3D_MIKKTSPACE_PATH}")
        message(FATAL_ERROR "Bundled MikkTSpace source is missing: ${TOY3D_MIKKTSPACE_PATH}")
    endif()
    file(SHA256 "${TOY3D_MIKKTSPACE_PATH}" TOY3D_MIKKTSPACE_ACTUAL_HASH)
    if(NOT TOY3D_MIKKTSPACE_ACTUAL_HASH STREQUAL TOY3D_MIKKTSPACE_HASH)
        message(FATAL_ERROR "MikkTSpace source differs from pinned revision: ${TOY3D_MIKKTSPACE_PATH}")
    endif()
endforeach()

add_library(Toy3dAssetPipeline STATIC)
target_sources(Toy3dAssetPipeline PRIVATE
    "asset_cook.h" "asset_cook.cpp"
    "skeletal_mesh_builder.h" "skeletal_mesh_builder.cpp"
    "static_mesh_builder.h" "static_mesh_builder.cpp"
    "texture_import.h" "texture_import.cpp"
    "environment_import.h" "environment_import.cpp"
    "mesh_tangents.h" "mesh_tangents.cpp"
    "${TOY3D_MIKKTSPACE_DIR}/mikktspace.c")
target_include_directories(Toy3dAssetPipeline PRIVATE "${TOY3D_MIKKTSPACE_DIR}")
target_compile_features(Toy3dAssetPipeline PUBLIC cxx_std_17)
set_target_properties(Toy3dAssetPipeline PROPERTIES CXX_EXTENSIONS OFF FOLDER "Engine/Tools")
target_include_directories(Toy3dAssetPipeline PUBLIC "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/..>")
target_link_libraries(Toy3dAssetPipeline PUBLIC Toy3dAssets)
if(TOY3D_ENABLE_ASSIMP_MODEL_IMPORT)
    target_sources(Toy3dAssetPipeline PRIVATE "static_mesh_import.h" "static_mesh_import.cpp"
        "assimp_import_support.h" "skeletal_mesh_import.h" "skeletal_mesh_import.cpp")
    target_link_libraries(Toy3dAssetPipeline PRIVATE assimp::assimp)
    target_compile_definitions(Toy3dAssetPipeline PUBLIC WITH_EDITORONLY_DATA=1)
endif()

add_executable(Toy3dDefaultAssets "default_assets_main.cpp")
target_compile_features(Toy3dDefaultAssets PRIVATE cxx_std_17)
set_target_properties(Toy3dDefaultAssets PROPERTIES CXX_EXTENSIONS OFF FOLDER "Engine/Tools")
target_link_libraries(Toy3dDefaultAssets PRIVATE Toy3dAssetPipeline Toy3dAssets)

if(BUILD_TESTING)
    add_executable(Toy3dAssetCookTests "tests/asset_cook_tests.cpp")
    target_compile_features(Toy3dAssetCookTests PRIVATE cxx_std_17)
    set_target_properties(Toy3dAssetCookTests PROPERTIES CXX_EXTENSIONS OFF FOLDER "Tests/Tools")
    target_link_libraries(Toy3dAssetCookTests PRIVATE Toy3dAssetPipeline)
    target_compile_definitions(Toy3dAssetCookTests PRIVATE
        "TOY3D_COOK_TEST_ROOT=\"${CMAKE_BINARY_DIR}/asset-cook-test-output\"")
    add_test(NAME Toy3dTools.AssetCook COMMAND Toy3dAssetCookTests)
    add_executable(Toy3dEnvironmentImportTests "tests/environment_import_tests.cpp")
    target_compile_features(Toy3dEnvironmentImportTests PRIVATE cxx_std_17)
    set_target_properties(Toy3dEnvironmentImportTests PROPERTIES CXX_EXTENSIONS OFF FOLDER "Tests/Tools")
    target_link_libraries(Toy3dEnvironmentImportTests PRIVATE Toy3dAssetPipeline)
    target_compile_definitions(Toy3dEnvironmentImportTests PRIVATE
        "TOY3D_ENVIRONMENT_TEST_ROOT=\"${CMAKE_BINARY_DIR}/environment-test-output\"")
    add_test(NAME Toy3dTools.EnvironmentImport COMMAND Toy3dEnvironmentImportTests)
    add_executable(Toy3dMeshTangentTests "tests/mesh_tangent_tests.cpp")
    target_compile_features(Toy3dMeshTangentTests PRIVATE cxx_std_17)
    set_target_properties(Toy3dMeshTangentTests PROPERTIES CXX_EXTENSIONS OFF FOLDER "Tests/Tools")
    target_link_libraries(Toy3dMeshTangentTests PRIVATE Toy3dAssetPipeline)
    target_compile_definitions(Toy3dMeshTangentTests PRIVATE
        "TOY3D_TANGENT_CONTROLLED_ASSET_ROOT=\"${CMAKE_SOURCE_DIR}/project/asset\"")
    add_test(NAME Toy3dTools.MeshTangents COMMAND Toy3dMeshTangentTests)

    add_executable(Toy3dTextureImportTests "tests/texture_import_tests.cpp")
    target_compile_features(Toy3dTextureImportTests PRIVATE cxx_std_17)
    set_target_properties(Toy3dTextureImportTests PROPERTIES CXX_EXTENSIONS OFF FOLDER "Tests/Tools")
    target_link_libraries(Toy3dTextureImportTests PRIVATE Toy3dAssetPipeline Toy3dCore)
    target_compile_definitions(Toy3dTextureImportTests PRIVATE
        "TOY3D_TEXTURE_TEST_JPEG=\"${CMAKE_CURRENT_SOURCE_DIR}/tests/solid_color.jpg\""
        "TOY3D_TEXTURE_PAIR_TEST_ROOT=\"${CMAKE_BINARY_DIR}/texture-pair-test-output\"")
    add_test(NAME Toy3dTools.TextureImport COMMAND Toy3dTextureImportTests)
endif()

if(TOY3D_ENABLE_ASSIMP_MODEL_IMPORT)
add_executable(Toy3dAssimpProbe "assimp_probe.cpp")
add_executable(Toy3dModelImport "model_import_main.cpp")
target_compile_features(Toy3dModelImport PRIVATE cxx_std_17)
set_target_properties(Toy3dModelImport PROPERTIES CXX_EXTENSIONS OFF FOLDER "Engine/Tools")
target_link_libraries(Toy3dModelImport PRIVATE Toy3dAssetPipeline)
target_compile_options(Toy3dModelImport PRIVATE "$<$<COMPILE_LANG_AND_ID:CXX,MSVC>:/utf-8>")
target_compile_features(Toy3dAssimpProbe PRIVATE cxx_std_17)
set_target_properties(Toy3dAssimpProbe PROPERTIES CXX_EXTENSIONS OFF FOLDER "Engine/Tools")
target_link_libraries(Toy3dAssimpProbe PRIVATE assimp::assimp)
target_compile_options(Toy3dAssimpProbe PRIVATE "$<$<COMPILE_LANG_AND_ID:CXX,MSVC>:/utf-8>")

if(BUILD_TESTING)
    add_executable(Toy3dStaticMeshImportTests "tests/static_mesh_import_tests.cpp")
    add_executable(Toy3dSkeletalMeshImportTests "tests/skeletal_mesh_import_tests.cpp")
    target_compile_features(Toy3dSkeletalMeshImportTests PRIVATE cxx_std_17)
    set_target_properties(Toy3dSkeletalMeshImportTests PROPERTIES CXX_EXTENSIONS OFF FOLDER "Engine/Tests")
    target_link_libraries(Toy3dSkeletalMeshImportTests PRIVATE Toy3dAssetPipeline)
    target_compile_definitions(Toy3dSkeletalMeshImportTests PRIVATE
        "TOY3D_SKELETAL_IMPORT_SAMPLES=\"${CMAKE_CURRENT_SOURCE_DIR}/tests\""
        "TOY3D_SKELETAL_IMPORT_OUTPUT=\"${CMAKE_CURRENT_BINARY_DIR}/skeletal-test-output\"")
    add_test(NAME Toy3dAsset.SkeletalMeshImport COMMAND Toy3dSkeletalMeshImportTests)
    target_compile_features(Toy3dStaticMeshImportTests PRIVATE cxx_std_17)
    set_target_properties(Toy3dStaticMeshImportTests PROPERTIES CXX_EXTENSIONS OFF FOLDER "Engine/Tests")
    target_link_libraries(Toy3dStaticMeshImportTests PRIVATE Toy3dAssetPipeline)
    target_compile_definitions(Toy3dStaticMeshImportTests PRIVATE
        "TOY3D_IMPORT_TEST_SAMPLES=\"${CMAKE_CURRENT_SOURCE_DIR}/tests\""
        "TOY3D_IMPORT_TEST_OUTPUT=\"${CMAKE_CURRENT_BINARY_DIR}/test-output\"")
    add_test(NAME Toy3dAsset.StaticMeshImport COMMAND Toy3dStaticMeshImportTests)
    add_test(NAME Toy3dAssimp.ObjProbe
        COMMAND Toy3dAssimpProbe "${CMAKE_CURRENT_SOURCE_DIR}/tests/triangle.obj")
    add_test(NAME Toy3dAssimp.FbxProbe
        COMMAND Toy3dAssimpProbe "${CMAKE_CURRENT_SOURCE_DIR}/tests/phong_cube.fbx")
    add_test(NAME Toy3dAssimp.GltfProbe
        COMMAND Toy3dAssimpProbe "${CMAKE_CURRENT_SOURCE_DIR}/tests/triangle.gltf")
    add_test(NAME Toy3dAssimp.GlbProbe
        COMMAND Toy3dAssimpProbe "${CMAKE_CURRENT_SOURCE_DIR}/tests/triangle.glb")
endif()
endif()
