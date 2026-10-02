# Shared development deployment: copy source inputs without deleting shared
# destinations. Projects remain at their descriptor location, outside bin.
function(toy3d_deploy_common_resources target)
    foreach(TOY3D_RESOURCE_DIR IN ITEMS
            "engine/asset" "engine/config")
        add_custom_command(TARGET ${target} POST_BUILD
            COMMAND "${CMAKE_COMMAND}" -E make_directory
                    "${BINARY_ROOT_DIR}${TOY3D_RESOURCE_DIR}"
            COMMAND "${CMAKE_COMMAND}" -E copy_directory
                    "${TOY3D_ROOT_DIR}/${TOY3D_RESOURCE_DIR}"
                    "${BINARY_ROOT_DIR}${TOY3D_RESOURCE_DIR}"
            VERBATIM)
    endforeach()
endfunction()
