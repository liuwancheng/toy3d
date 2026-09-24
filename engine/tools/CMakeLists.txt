add_subdirectory(shader_compiler)
add_subdirectory(reflection_codegen)

if(TOY3D_ENABLE_ASSIMP_MODEL_IMPORT)
    add_subdirectory(model_import)
endif()
