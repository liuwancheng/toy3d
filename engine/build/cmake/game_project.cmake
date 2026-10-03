# Explicit project build. The engine never scans or links arbitrary project directories.
function(toy3d_add_game_hosts module descriptor)
    if(NOT TARGET "${module}" OR NOT EXISTS "${descriptor}")
        message(FATAL_ERROR "A game host needs a Runtime target and an existing .toy descriptor")
    endif()
    set(TOY3D_GAME_ENGINE_ROOT "${TOY3D_ROOT_DIR}/engine")
    get_filename_component(TOY3D_GAME_PROJECT_ROOT "${descriptor}" DIRECTORY)
    set(TOY3D_MODULE_ENTRY "${CMAKE_CURRENT_BINARY_DIR}/generated/game_module_entry.cpp")
    file(WRITE "${TOY3D_MODULE_ENTRY}" "#include \"application/game_module.h\"\n#include \"application/game_host.h\"\n#include \"platform/platform_defines.h\"\n#if WITH_WIN\n#define TOY3D_MODULE_EXPORT __declspec(dllexport)\n#else\n#define TOY3D_MODULE_EXPORT __attribute__((visibility(\"default\")))\n#endif\nextern \"C\" TOY3D_MODULE_EXPORT const toy3d::GameModuleApi* toy3d_game_module()\n{\n    static const auto registration = toy3d::linked_game_module();\n    static const toy3d::GameModuleApi api{1u, TOY3D_MODULE_BUILD_ID, registration.name.c_str(), registration.register_types};\n    return &api;\n}\n")
    add_library("${module}Module" MODULE "${TOY3D_MODULE_ENTRY}")
    target_link_libraries("${module}Module" PRIVATE "${module}" Toy3dRuntime)
    target_compile_features("${module}Module" PRIVATE cxx_std_17)
    set_target_properties("${module}Module" PROPERTIES PREFIX "" OUTPUT_NAME "${module}"
        ARCHIVE_OUTPUT_NAME "${module}Module" DEBUG_POSTFIX ""
        CXX_EXTENSIONS OFF FOLDER "Game/${module}")
    if(APPLE)
        set_target_properties("${module}Module" PROPERTIES SUFFIX ".dylib")
    endif()
    add_custom_command(TARGET "${module}Module" POST_BUILD
        COMMAND "${CMAKE_COMMAND}" -E make_directory "${TOY3D_GAME_PROJECT_ROOT}/binaries/$<CONFIG>"
        COMMAND "${CMAKE_COMMAND}" -E copy_if_different "$<TARGET_FILE:${module}Module>"
            "${TOY3D_GAME_PROJECT_ROOT}/binaries/$<CONFIG>/"
        VERBATIM)
    set(TOY3D_HOST_TARGET "${module}Game")
    set(TOY3D_HOST_MAIN "${TOY3D_GAME_ENGINE_ROOT}/runtime/application/game_main.cpp")
    if(WIN32)
        add_executable("${TOY3D_HOST_TARGET}" WIN32 "${TOY3D_HOST_MAIN}" "${TOY3D_GAME_ENGINE_ROOT}/runtime/application/host_entry.cpp")
        set(TOY3D_HOST_ICON_NAME "Toy3d")
        set(TOY3D_HOST_RESOURCE_ROOT "${TOY3D_GAME_ENGINE_ROOT}/build/windows/resources")
        target_sources("${TOY3D_HOST_TARGET}" PRIVATE "${TOY3D_HOST_RESOURCE_ROOT}/${TOY3D_HOST_ICON_NAME}.rc")
        target_include_directories("${TOY3D_HOST_TARGET}" PRIVATE "${TOY3D_HOST_RESOURCE_ROOT}")
        set_source_files_properties("${TOY3D_HOST_RESOURCE_ROOT}/${TOY3D_HOST_ICON_NAME}.rc" PROPERTIES
            OBJECT_DEPENDS "${TOY3D_HOST_RESOURCE_ROOT}/resource.h;${TOY3D_HOST_RESOURCE_ROOT}/${TOY3D_HOST_ICON_NAME}.ico")
        target_link_libraries("${TOY3D_HOST_TARGET}" PRIVATE shell32)
    elseif(APPLE)
        add_executable("${TOY3D_HOST_TARGET}" MACOSX_BUNDLE "${TOY3D_HOST_MAIN}" "${TOY3D_GAME_ENGINE_ROOT}/runtime/application/host_entry.cpp")
        set_target_properties("${TOY3D_HOST_TARGET}" PROPERTIES
            MACOSX_BUNDLE_INFO_PLIST "${TOY3D_GAME_ENGINE_ROOT}/build/mac/resources/Info.plist.in"
            MACOSX_BUNDLE_BUNDLE_NAME "${TOY3D_HOST_TARGET}" MACOSX_BUNDLE_GUI_IDENTIFIER "com.toy3d.${TOY3D_HOST_TARGET}"
            MACOSX_BUNDLE_ICON_FILE "Toy3d.icns"
            MACOSX_BUNDLE_BUNDLE_VERSION "${CMAKE_PROJECT_VERSION}" MACOSX_BUNDLE_SHORT_VERSION_STRING "${CMAKE_PROJECT_VERSION}"
            XCODE_ATTRIBUTE_CODE_SIGNING_ALLOWED "NO" XCODE_ATTRIBUTE_CODE_SIGNING_REQUIRED "NO")
        set_source_files_properties("${TOY3D_GAME_ENGINE_ROOT}/build/mac/resources/Toy3d.icns" PROPERTIES MACOSX_PACKAGE_LOCATION "Resources")
        target_sources("${TOY3D_HOST_TARGET}" PRIVATE "${TOY3D_GAME_ENGINE_ROOT}/build/mac/resources/Toy3d.icns")
    else()
        add_executable("${TOY3D_HOST_TARGET}" "${TOY3D_HOST_MAIN}" "${TOY3D_GAME_ENGINE_ROOT}/runtime/application/host_entry.cpp")
    endif()
    target_link_libraries("${TOY3D_HOST_TARGET}" PRIVATE "${module}" Toy3dRuntime)
    target_compile_features("${TOY3D_HOST_TARGET}" PRIVATE cxx_std_17)
    set_target_properties("${TOY3D_HOST_TARGET}" PROPERTIES CXX_EXTENSIONS OFF FOLDER "Game/${module}")
    target_compile_options("${TOY3D_HOST_TARGET}" PRIVATE "$<$<COMPILE_LANG_AND_ID:CXX,MSVC>:/utf-8>")
    target_compile_definitions("${TOY3D_HOST_TARGET}" PRIVATE
        "TOY3D_PROJECT_DESCRIPTOR=\"${descriptor}\""
        "TOY3D_EDITOR_DEPLOY_ROOT=\"${BINARY_ROOT_DIR}\""
        "TOY3D_EDITOR_ENGINE_ASSET_ROOT=\"${TOY3D_GAME_ENGINE_ROOT}/asset\""
        "TOY3D_EDITOR_ENGINE_CONFIG_ROOT=\"${TOY3D_GAME_ENGINE_ROOT}/config\"")
    add_custom_command(TARGET "${TOY3D_HOST_TARGET}" POST_BUILD
        COMMAND "${CMAKE_COMMAND}" -E make_directory "${BINARY_ROOT_DIR}"
        COMMAND "${CMAKE_COMMAND}" -E copy_if_different "$<TARGET_FILE:${TOY3D_HOST_TARGET}>" "${BINARY_ROOT_DIR}"
        VERBATIM)
    if(APPLE)
        add_custom_command(TARGET "${TOY3D_HOST_TARGET}" POST_BUILD
            COMMAND "${CMAKE_COMMAND}" -E copy_directory "$<TARGET_BUNDLE_DIR:${TOY3D_HOST_TARGET}>" "${BINARY_ROOT_DIR}${TOY3D_HOST_TARGET}.app" VERBATIM)
    endif()
    toy3d_deploy_common_resources("${TOY3D_HOST_TARGET}")
    get_filename_component(TOY3D_GAME_PROJECT_ROOT "${descriptor}" DIRECTORY)
    set(TOY3D_GAME_SHADER_BASE "${CMAKE_BINARY_DIR}/generated/game_shader/${module}")
    set(TOY3D_GAME_SHADER_OUTPUT "${TOY3D_GAME_SHADER_BASE}/output")
    set(TOY3D_GAME_SHADER_WORK "${TOY3D_GAME_SHADER_BASE}/work")
    set(TOY3D_GAME_SHADER_DEPLOY "${BINARY_ROOT_DIR}shader/${module}_player")
    file(GLOB_RECURSE TOY3D_GAME_SHADER_INPUTS CONFIGURE_DEPENDS
        "${TOY3D_GAME_PROJECT_ROOT}/shader/*" "${TOY3D_GAME_PROJECT_ROOT}/asset/*"
        "${TOY3D_GAME_PROJECT_ROOT}/config/*" "${TOY3D_GAME_ENGINE_ROOT}/shader/builtin/*"
        "${TOY3D_GAME_ENGINE_ROOT}/shader/include/*" "${TOY3D_GAME_ENGINE_ROOT}/asset/*")
    add_custom_command(OUTPUT "${TOY3D_GAME_SHADER_BASE}/cook.stamp"
        COMMAND "${CMAKE_COMMAND}" -E make_directory "${TOY3D_GAME_SHADER_BASE}"
        COMMAND "${CMAKE_COMMAND}" -E remove_directory "${TOY3D_GAME_SHADER_OUTPUT}"
        COMMAND "${CMAKE_COMMAND}" -E remove_directory "${TOY3D_GAME_SHADER_WORK}"
        COMMAND $<TARGET_FILE:Toy3dShaderCompiler> --toolchain-root "${TOY3D_SHADER_TOOLCHAIN_ROOT}" cook-vulkan
            "${TOY3D_GAME_ENGINE_ROOT}" "${TOY3D_GAME_PROJECT_ROOT}" "${TOY3D_GAME_SHADER_OUTPUT}"
            "${TOY3D_GAME_SHADER_WORK}" Player
        COMMAND "${CMAKE_COMMAND}" -E remove_directory "${TOY3D_GAME_SHADER_DEPLOY}"
        COMMAND "${CMAKE_COMMAND}" -E copy_directory "${TOY3D_GAME_SHADER_OUTPUT}" "${TOY3D_GAME_SHADER_DEPLOY}"
        COMMAND "${CMAKE_COMMAND}" -E touch "${TOY3D_GAME_SHADER_BASE}/cook.stamp"
        DEPENDS Toy3dShaderCompiler ${TOY3D_GAME_SHADER_INPUTS}
            "${TOY3D_GAME_ENGINE_ROOT}/config/shader_build.settings" VERBATIM)
    add_custom_target("${module}Shaders" DEPENDS "${TOY3D_GAME_SHADER_BASE}/cook.stamp")
    set_target_properties("${module}Shaders" PROPERTIES FOLDER "Game/${module}")
    add_dependencies("${module}Game" "${module}Shaders")
    target_compile_definitions("${module}Game" PRIVATE "TOY3D_GAME_SHADER_ROOT=\"${TOY3D_GAME_SHADER_DEPLOY}\"")
    # The shared Editor builds the module and standalone Game used by Scene Play.
    add_dependencies(Toy3dEditor "${module}Module" "${module}Game")
    set_property(GLOBAL PROPERTY TOY3D_GAME_HOST_TARGET "${module}Game")
    set_property(GLOBAL PROPERTY TOY3D_GAME_MODULE_TARGET "${module}Module")
endfunction()
