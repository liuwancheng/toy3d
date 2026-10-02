add_library(Toy3dAssetPipeline STATIC)
target_sources(Toy3dAssetPipeline PRIVATE
    "skeletal_mesh_builder.h" "skeletal_mesh_builder.cpp"
    "static_mesh_builder.h" "static_mesh_builder.cpp"
    "texture_import.h" "texture_import.cpp")
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
