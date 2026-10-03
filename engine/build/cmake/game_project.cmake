# Explicit project build. The engine never scans or links arbitrary project directories.
function(toy3d_add_game_hosts module descriptor)
    if(NOT TARGET "${module}" OR NOT EXISTS "${descriptor}")
        message(FATAL_ERROR "A game host needs a Runtime target and an existing .toy descriptor")
    endif()
    set(TOY3D_GAME_ENGINE_ROOT "${TOY3D_ROOT_DIR}/engine")
    foreach(TOY3D_HOST_KIND IN ITEMS Editor Game)
        set(TOY3D_HOST_TARGET "${module}${TOY3D_HOST_KIND}")
        if(TOY3D_HOST_KIND STREQUAL "Editor")
            set(TOY3D_HOST_MAIN "${TOY3D_GAME_ENGINE_ROOT}/editor/source/main.cpp")
        else()
            set(TOY3D_HOST_MAIN "${TOY3D_GAME_ENGINE_ROOT}/runtime/application/game_main.cpp")
        endif()
        if(WIN32)
            add_executable("${TOY3D_HOST_TARGET}" WIN32 "${TOY3D_HOST_MAIN}" "${TOY3D_GAME_ENGINE_ROOT}/runtime/application/host_entry.cpp")
            set(TOY3D_HOST_RESOURCE_ROOT "${TOY3D_GAME_ENGINE_ROOT}/build/windows/resources")
            if(TOY3D_HOST_KIND STREQUAL "Editor")
                set(TOY3D_HOST_ICON_NAME "Toy3dEditor")
            else()
                set(TOY3D_HOST_ICON_NAME "Toy3d")
            endif()
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
        if(TOY3D_HOST_KIND STREQUAL "Editor")
            target_link_libraries("${TOY3D_HOST_TARGET}" PRIVATE Toy3dEditorCore)
            add_dependencies("${TOY3D_HOST_TARGET}" Toy3dShaderCompiler)
            if(APPLE)
                set(TOY3D_GAME_EXECUTABLE "${BINARY_ROOT_DIR}${module}Game.app/Contents/MacOS/${module}Game")
            else()
                set(TOY3D_GAME_EXECUTABLE "${BINARY_ROOT_DIR}${module}Game${CMAKE_EXECUTABLE_SUFFIX}")
            endif()
            target_compile_definitions("${TOY3D_HOST_TARGET}" PRIVATE TOY3D_LINKED_GAME_MODULE=1
                "TOY3D_PROJECT_GAME_EXECUTABLE=\"${TOY3D_GAME_EXECUTABLE}\"")
        endif()
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
        if(TOY3D_HOST_KIND STREQUAL "Editor")
            add_custom_command(TARGET "${TOY3D_HOST_TARGET}" POST_BUILD
                COMMAND "${CMAKE_COMMAND}" -E copy_directory "${TOY3D_GAME_ENGINE_ROOT}/editor/resources" "${BINARY_ROOT_DIR}editor/resources" VERBATIM)
        endif()
        toy3d_deploy_common_resources("${TOY3D_HOST_TARGET}")
    endforeach()
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
    # A project Editor is deployed together with the Game it launches for Scene Play.
    add_dependencies("${module}Editor" "${module}Game")
endfunction()
