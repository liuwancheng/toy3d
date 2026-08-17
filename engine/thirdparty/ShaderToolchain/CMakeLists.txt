if(WIN32)
    if(NOT CMAKE_HOST_SYSTEM_PROCESSOR MATCHES "^(AMD64|amd64|x86_64)$")
        message(FATAL_ERROR "Toy3d Shader toolchain has no Windows bundle for host processor: ${CMAKE_HOST_SYSTEM_PROCESSOR}")
    endif()
    set(TOY3D_SHADER_TOOLCHAIN_PLATFORM "windows-x64")
    set(TOY3D_SHADER_TOOLCHAIN_REQUIRED_ARTIFACTS
        "bin/dxc.exe"
        "bin/dxcompiler.dll"
        "bin/spirv-val.exe"
        "lib/spirv-reflect-static.lib"
        "include/spirv_reflect.h")
    set(TOY3D_SHADER_TOOLCHAIN_REFLECT_LIBRARY "lib/spirv-reflect-static.lib")
elseif(APPLE)
    if(CMAKE_HOST_SYSTEM_PROCESSOR MATCHES "^(x86_64|AMD64|amd64)$")
        set(TOY3D_SHADER_TOOLCHAIN_PLATFORM "macos-x64")
    elseif(CMAKE_HOST_SYSTEM_PROCESSOR MATCHES "^(arm64|aarch64)$")
        set(TOY3D_SHADER_TOOLCHAIN_PLATFORM "macos-arm64")
    else()
        message(FATAL_ERROR "Toy3d Shader toolchain has no macOS bundle for host processor: ${CMAKE_HOST_SYSTEM_PROCESSOR}")
    endif()
    set(TOY3D_SHADER_TOOLCHAIN_REQUIRED_ARTIFACTS
        "bin/dxc"
        "bin/spirv-val"
        "lib/libdxcompiler.dylib"
        "lib/libspirv-reflect.a"
        "include/spirv_reflect.h")
    set(TOY3D_SHADER_TOOLCHAIN_REFLECT_LIBRARY "lib/libspirv-reflect.a")
else()
    message(FATAL_ERROR "Toy3d Shader toolchain currently supports only Windows and macOS hosts.")
endif()

set(TOY3D_SHADER_TOOLCHAIN_ROOT
    "${CMAKE_CURRENT_SOURCE_DIR}/${TOY3D_SHADER_TOOLCHAIN_PLATFORM}")
set(TOY3D_SHADER_TOOLCHAIN_MANIFEST
    "${TOY3D_SHADER_TOOLCHAIN_ROOT}/Toy3dShaderToolchain.manifest")
if(NOT EXISTS "${TOY3D_SHADER_TOOLCHAIN_MANIFEST}")
    message(FATAL_ERROR
        "The committed ${TOY3D_SHADER_TOOLCHAIN_PLATFORM} Shader toolchain bundle is missing. "
        "Publish it with engine/thirdparty/ShaderToolchain/Build/publish_shader_toolchain.py on a matching host.")
endif()

set(TOY3D_SHADER_TOOLCHAIN_DEPENDENCIES "${TOY3D_SHADER_TOOLCHAIN_MANIFEST}")
foreach(TOY3D_SHADER_TOOLCHAIN_ARTIFACT IN LISTS TOY3D_SHADER_TOOLCHAIN_REQUIRED_ARTIFACTS)
    set(TOY3D_SHADER_TOOLCHAIN_ARTIFACT_PATH
        "${TOY3D_SHADER_TOOLCHAIN_ROOT}/${TOY3D_SHADER_TOOLCHAIN_ARTIFACT}")
    if(NOT EXISTS "${TOY3D_SHADER_TOOLCHAIN_ARTIFACT_PATH}")
        message(FATAL_ERROR
            "The ${TOY3D_SHADER_TOOLCHAIN_PLATFORM} Shader toolchain is incomplete: "
            "${TOY3D_SHADER_TOOLCHAIN_ARTIFACT}")
    endif()
    file(SIZE "${TOY3D_SHADER_TOOLCHAIN_ARTIFACT_PATH}" TOY3D_SHADER_TOOLCHAIN_ARTIFACT_SIZE)
    if(TOY3D_SHADER_TOOLCHAIN_ARTIFACT_SIZE LESS 1024)
        file(READ "${TOY3D_SHADER_TOOLCHAIN_ARTIFACT_PATH}" TOY3D_SHADER_TOOLCHAIN_PREFIX LIMIT 128)
        if(TOY3D_SHADER_TOOLCHAIN_PREFIX MATCHES "version https://git-lfs.github.com/spec/v1")
            message(FATAL_ERROR
                "The Shader toolchain contains unresolved Git LFS pointers. Run 'git lfs pull' before configuring.")
        endif()
    endif()
    list(APPEND TOY3D_SHADER_TOOLCHAIN_DEPENDENCIES "${TOY3D_SHADER_TOOLCHAIN_ARTIFACT_PATH}")
endforeach()

set(TOY3D_SHADER_TOOLCHAIN_SYNC_ROOT
    "${BINARY_ROOT_DIR}ShaderToolchain/${TOY3D_SHADER_TOOLCHAIN_PLATFORM}")
set(TOY3D_SHADER_TOOLCHAIN_SYNC_MANIFEST
    "${TOY3D_SHADER_TOOLCHAIN_SYNC_ROOT}/Toy3dShaderToolchain.manifest")
add_custom_command(
    OUTPUT "${TOY3D_SHADER_TOOLCHAIN_SYNC_MANIFEST}"
    COMMAND ${CMAKE_COMMAND} -E remove_directory "${TOY3D_SHADER_TOOLCHAIN_SYNC_ROOT}"
    COMMAND ${CMAKE_COMMAND} -E make_directory "${TOY3D_SHADER_TOOLCHAIN_SYNC_ROOT}"
    COMMAND ${CMAKE_COMMAND} -E copy_directory
            "${TOY3D_SHADER_TOOLCHAIN_ROOT}"
            "${TOY3D_SHADER_TOOLCHAIN_SYNC_ROOT}"
    DEPENDS ${TOY3D_SHADER_TOOLCHAIN_DEPENDENCIES}
    COMMENT "Syncing ${TOY3D_SHADER_TOOLCHAIN_PLATFORM} Shader toolchain"
    VERBATIM)
add_custom_target(Toy3dShaderToolchainSync
    DEPENDS "${TOY3D_SHADER_TOOLCHAIN_SYNC_MANIFEST}")
set_target_properties(Toy3dShaderToolchainSync PROPERTIES FOLDER "ThirdParty/ShaderToolchain")

add_library(Toy3dSpirvReflect STATIC IMPORTED GLOBAL)
set_target_properties(Toy3dSpirvReflect PROPERTIES
    IMPORTED_LOCATION "${TOY3D_SHADER_TOOLCHAIN_ROOT}/${TOY3D_SHADER_TOOLCHAIN_REFLECT_LIBRARY}"
    INTERFACE_INCLUDE_DIRECTORIES "${TOY3D_SHADER_TOOLCHAIN_ROOT}/include")
add_library(Toy3d::SpirvReflect ALIAS Toy3dSpirvReflect)

set(TOY3D_SHADER_TOOLCHAIN_PLATFORM "${TOY3D_SHADER_TOOLCHAIN_PLATFORM}" CACHE INTERNAL
    "Current host Shader toolchain platform" FORCE)
set(TOY3D_SHADER_TOOLCHAIN_ROOT "${TOY3D_SHADER_TOOLCHAIN_ROOT}" CACHE INTERNAL
    "Committed current-host Shader toolchain root" FORCE)
set(TOY3D_SHADER_TOOLCHAIN_SYNC_ROOT "${TOY3D_SHADER_TOOLCHAIN_SYNC_ROOT}" CACHE INTERNAL
    "Synchronized current-host Shader toolchain root" FORCE)
