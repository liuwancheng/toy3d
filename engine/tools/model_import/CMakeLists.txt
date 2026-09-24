add_executable(Toy3dAssimpProbe "assimp_probe.cpp")
target_compile_features(Toy3dAssimpProbe PRIVATE cxx_std_17)
set_target_properties(Toy3dAssimpProbe PROPERTIES CXX_EXTENSIONS OFF FOLDER "Engine/Tools")
target_link_libraries(Toy3dAssimpProbe PRIVATE assimp::assimp)
target_compile_options(Toy3dAssimpProbe PRIVATE "$<$<COMPILE_LANG_AND_ID:CXX,MSVC>:/utf-8>")

if(BUILD_TESTING)
    add_test(NAME Toy3dAssimp.ObjProbe
        COMMAND Toy3dAssimpProbe "${CMAKE_CURRENT_SOURCE_DIR}/tests/triangle.obj")
    add_test(NAME Toy3dAssimp.FbxProbe
        COMMAND Toy3dAssimpProbe "${CMAKE_CURRENT_SOURCE_DIR}/tests/phong_cube.fbx")
    add_test(NAME Toy3dAssimp.GltfProbe
        COMMAND Toy3dAssimpProbe "${CMAKE_CURRENT_SOURCE_DIR}/tests/triangle.gltf")
    add_test(NAME Toy3dAssimp.GlbProbe
        COMMAND Toy3dAssimpProbe "${CMAKE_CURRENT_SOURCE_DIR}/tests/triangle.glb")
endif()
