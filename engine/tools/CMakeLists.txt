add_subdirectory(shader_compiler)
add_subdirectory(reflection_codegen)
add_subdirectory(mesh_builder)
add_subdirectory(texture_import)

if(TOY3D_ENABLE_ASSIMP_MODEL_IMPORT)
    add_subdirectory(asset_import)
    add_subdirectory(model_import)
endif()
