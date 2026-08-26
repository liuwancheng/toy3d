#!/bin/bash

set -euo pipefail

script_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
configuration="${1:-Debug}"

case "${configuration}" in
    Debug|Release|RelWithDebInfo|MinSizeRel)
        ;;
    *)
        echo "Unsupported configuration: ${configuration}" >&2
        echo "Usage: $0 [Debug|Release|RelWithDebInfo|MinSizeRel]" >&2
        exit 2
        ;;
esac

default_generator="Xcode"
if [[ -n "${CODEX_SANDBOX:-}" || -n "${CODEX_CI:-}" ]]; then
    # CMake's Xcode compiler probe invokes xcodebuild before native build
    # arguments can redirect DerivedData. Sandboxed automation cannot write
    # Xcode's user Library directories, so use an in-repository build backend.
    default_generator="Unix Makefiles"
fi
generator="${TOY3D_MACOS_GENERATOR:-${default_generator}}"

case "${generator}" in
    Xcode)
        build_dir="${script_dir}/build/macos-xcode"
        cache_file="${build_dir}/CMakeCache.txt"
        xcode_no_sign_config="${script_dir}/cmake/xcode_no_sign.xcconfig"
        if [[ -f "${cache_file}" ]] && ! grep -q '^CMAKE_CXX_COMPILER:' "${cache_file}"; then
            echo "Removing incomplete Xcode configuration: ${build_dir}"
            cmake -E remove_directory "${build_dir}"
        fi
        XCODE_XCCONFIG_FILE="${xcode_no_sign_config}" cmake \
            -S "${script_dir}" \
            -B "${build_dir}" \
            -G "Xcode" \
            -DTOY3D_ENABLE_VULKAN_RHI=ON
        XCODE_XCCONFIG_FILE="${xcode_no_sign_config}" cmake \
            --build "${build_dir}" \
            --config "${configuration}" \
            --target Toy3dEditor
        ;;
    "Unix Makefiles")
        build_dir="${script_dir}/build/macos-make"
        cmake \
            -S "${script_dir}" \
            -B "${build_dir}" \
            -G "Unix Makefiles" \
            -DCMAKE_BUILD_TYPE="${configuration}" \
            -DTOY3D_ENABLE_VULKAN_RHI=ON
        cmake \
            --build "${build_dir}" \
            --target Toy3dEditor \
            --parallel
        ;;
    *)
        echo "Unsupported generator: ${generator}" >&2
        echo "Set TOY3D_MACOS_GENERATOR to Xcode or Unix Makefiles." >&2
        exit 2
        ;;
esac
