#!/usr/bin/env python3

import argparse
import hashlib
import json
import os
from pathlib import Path
import platform
import shutil
import subprocess
import sys


SCRIPT_DIR = Path(__file__).resolve().parent
REPOSITORY_ROOT = SCRIPT_DIR.parents[3]
DEFAULT_LOCK_FILE = SCRIPT_DIR / "toolchain-lock.json"
DEFAULT_WORK_ROOT = REPOSITORY_ROOT / "build/shader-toolchain"
SAFE_GIT_DIRECTORIES = []


def command_environment():
    environment = os.environ.copy()
    if SAFE_GIT_DIRECTORIES:
        environment["GIT_CONFIG_COUNT"] = str(len(SAFE_GIT_DIRECTORIES))
        for index, directory in enumerate(SAFE_GIT_DIRECTORIES):
            environment[f"GIT_CONFIG_KEY_{index}"] = "safe.directory"
            environment[f"GIT_CONFIG_VALUE_{index}"] = str(directory)
    return environment


def run(command, cwd=None):
    display = subprocess.list2cmdline([str(item) for item in command])
    print(f"[Toy3dShaderToolchain] {display}", flush=True)
    subprocess.run(
        [str(item) for item in command], cwd=cwd, check=True,
        env=command_environment())


def capture(command, cwd=None):
    return subprocess.check_output(
        [str(item) for item in command], cwd=cwd, text=True,
        env=command_environment()).strip()


def require_tool(name):
    path = shutil.which(name)
    if not path:
        raise RuntimeError(f"Required tool is unavailable: {name}")
    return path


def host_platform():
    machine = platform.machine().lower()
    if os.name == "nt" and machine in {"amd64", "x86_64"}:
        return "windows-x64"
    if sys.platform == "darwin" and machine == "x86_64":
        return "macos-x64"
    if sys.platform == "darwin" and machine in {"arm64", "aarch64"}:
        return "macos-arm64"
    raise RuntimeError(
        f"Unsupported Shader toolchain host: system={platform.system()}, machine={platform.machine()}.")


def platform_configuration(host):
    if host == "windows-x64":
        return {
            "generator": "Visual Studio 17 2022",
            "generator_arguments": ["-G", "Visual Studio 17 2022", "-A", "x64"],
            "cmake_arguments": [],
            "executable_suffix": ".exe",
            "dxc_library_name": "dxcompiler.dll",
            "reflect_library_name": "spirv-reflect-static.lib",
        }
    architecture = "x86_64" if host == "macos-x64" else "arm64"
    deployment_target = "10.15" if host == "macos-x64" else "11.0"
    return {
        "generator": "Unix Makefiles",
        "generator_arguments": ["-G", "Unix Makefiles"],
        "cmake_arguments": [
            "-DCMAKE_BUILD_TYPE=Release",
            f"-DCMAKE_OSX_ARCHITECTURES={architecture}",
            f"-DCMAKE_OSX_DEPLOYMENT_TARGET={deployment_target}",
        ],
        "executable_suffix": "",
        "dxc_library_name": "libdxcompiler.dylib",
        "reflect_library_name": "libspirv-reflect.a",
    }


def ensure_checkout(url, tag, commit, destination):
    if not destination.exists():
        destination.parent.mkdir(parents=True, exist_ok=True)
        run(["git", "clone", "--filter=blob:none", "--no-checkout", url, destination])
    if not (destination / ".git").exists():
        raise RuntimeError(f"Existing source directory is not a Git checkout: {destination}")
    origin = capture(["git", "remote", "get-url", "origin"], destination)
    if origin.rstrip("/") != url.rstrip("/"):
        raise RuntimeError(f"Git origin mismatch for {destination}: {origin}")
    if capture(["git", "status", "--porcelain", "--untracked-files=no"], destination):
        raise RuntimeError(f"Refusing to replace a modified dependency checkout: {destination}")
    exists = subprocess.run(
        ["git", "cat-file", "-e", f"{commit}^{{commit}}"],
        cwd=destination, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL,
        env=command_environment()).returncode == 0
    if not exists:
        run(["git", "fetch", "--depth", "1", "origin", commit], destination)
    tag_exists = subprocess.run(
        ["git", "show-ref", "--verify", "--quiet", f"refs/tags/{tag}"],
        cwd=destination, env=command_environment()).returncode == 0
    if not tag_exists:
        run(["git", "fetch", "--depth", "1", "origin", f"refs/tags/{tag}:refs/tags/{tag}"], destination)
    tagged_commit = capture(["git", "rev-list", "-n", "1", tag], destination)
    if tagged_commit != commit:
        raise RuntimeError(
            f"Locked tag {tag} does not resolve to expected commit {commit}: {tagged_commit}")
    run(["git", "checkout", "--detach", commit], destination)
    actual = capture(["git", "rev-parse", "HEAD"], destination)
    if actual != commit:
        raise RuntimeError(f"Dependency checkout mismatch: expected {commit}, got {actual}")


def ensure_dxc_submodules(dxc_source, expected_submodules):
    run([
        "git", "submodule", "update", "--init", "--recursive",
        "--depth", "1", "--filter=blob:none"
    ], dxc_source)
    for relative_path, expected_commit in expected_submodules.items():
        actual = capture(["git", "rev-parse", "HEAD"], dxc_source / relative_path)
        if actual != expected_commit:
            raise RuntimeError(
                f"DXC submodule mismatch for {relative_path}: expected {expected_commit}, got {actual}")


def verify_lock_consistency(lock):
    dxc_submodules = lock["dxc"]["submodules"]
    if dxc_submodules["external/SPIRV-Tools"] != lock["spirv_tools"]["commit"]:
        raise RuntimeError("SPIRV-Tools lock does not match the DXC submodule revision.")
    if dxc_submodules["external/SPIRV-Headers"] != lock["spirv_tools"]["headers_commit"]:
        raise RuntimeError("SPIRV-Headers lock does not match the DXC submodule revision.")
    version_input = (SCRIPT_DIR / "dxc-version/version.inc").read_text(encoding="utf-8")
    if lock["dxc"]["commit"][:16] not in version_input:
        raise RuntimeError("The fixed DXC version input does not identify the locked DXC commit.")


def configure(source, build_directory, arguments, host_config, cache_file=None, extra_arguments=None):
    command = ["cmake", "-S", source, "-B", build_directory]
    command.extend(host_config["generator_arguments"])
    if cache_file:
        command.extend(["-C", cache_file])
    command.extend(host_config["cmake_arguments"])
    command.extend(arguments)
    if extra_arguments:
        command.extend(extra_arguments)
    run(command)


def build(build_directory, targets):
    command = ["cmake", "--build", build_directory, "--config", "Release", "--target", *targets]
    if sys.platform == "darwin":
        command.append("--parallel")
    run(command)


def find_artifact(root, candidates, artifact_name):
    for relative_path in candidates:
        candidate = root / relative_path
        if candidate.is_file():
            return candidate
    names = {Path(candidate).name for candidate in candidates}
    matches = sorted(path for path in root.rglob("*") if path.is_file() and path.name in names)
    if len(matches) == 1:
        return matches[0]
    if not matches:
        raise RuntimeError(f"Expected locked build artifact is missing: {artifact_name} under {root}")
    raise RuntimeError(f"Ambiguous locked build artifact {artifact_name}: {matches}")


def sha256_file(path):
    digest = hashlib.sha256()
    with path.open("rb") as source:
        for block in iter(lambda: source.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest()


def copy_file(source, destination):
    destination.parent.mkdir(parents=True, exist_ok=True)
    shutil.copy2(source, destination)


def manifest_artifact(prefix, relative_path, source_path, component, build_parameters):
    return [
        f"{prefix}.path={relative_path.as_posix()}",
        f"{prefix}.sha256={sha256_file(source_path)}",
        f"{prefix}.source_revision={component['commit']}",
        f"{prefix}.build_parameters={' '.join(build_parameters)}",
        f"{prefix}.license={component['license']}",
        f"{prefix}.source_url={component['url']}"
    ]


def cache_value(cache_path, key):
    prefix = f"{key}:"
    for line in cache_path.read_text(encoding="utf-8", errors="replace").splitlines():
        if line.startswith(prefix):
            separator = line.find("=")
            return line[separator + 1:] if separator >= 0 else "unknown"
    return "unknown"


def path_version(cache_path, key, marker):
    value = cache_value(cache_path, key).replace("\\", "/")
    marker_index = value.lower().find(marker.lower())
    if marker_index < 0:
        return "unknown"
    suffix = value[marker_index + len(marker):]
    return suffix.split("/", 1)[0] or "unknown"


def locate_artifacts(sources, builds, host, host_config):
    suffix = host_config["executable_suffix"]
    dxc_name = f"dxc{suffix}"
    spirv_val_name = f"spirv-val{suffix}"
    if host == "windows-x64":
        dxc_library_candidates = ["Release/bin/dxcompiler.dll", "bin/Release/dxcompiler.dll"]
        reflect_candidates = ["Release/spirv-reflect-static.lib", "Release/lib/spirv-reflect-static.lib"]
    else:
        dxc_library_candidates = ["lib/libdxcompiler.dylib", "bin/libdxcompiler.dylib"]
        reflect_candidates = ["libspirv-reflect.a", "lib/libspirv-reflect.a"]
    return {
        "dxc": find_artifact(builds / "dxc", [f"Release/bin/{dxc_name}", f"bin/{dxc_name}"], "DXC"),
        "dxc_library": find_artifact(
            builds / "dxc", dxc_library_candidates, host_config["dxc_library_name"]),
        "spirv_val": find_artifact(
            builds / "spirv-tools", [f"tools/Release/{spirv_val_name}", f"tools/{spirv_val_name}", f"bin/{spirv_val_name}"],
            "spirv-val"),
        "spirv_reflect": find_artifact(
            builds / "spirv-reflect", reflect_candidates, host_config["reflect_library_name"]),
        "spirv_reflect_header": sources / "spirv-reflect/spirv_reflect.h",
    }


def prepare_macos_runtime(bundle_root):
    dxc_path = bundle_root / "bin/dxc"
    library_path = bundle_root / "lib/libdxcompiler.dylib"
    dependencies = capture(["otool", "-L", dxc_path]).splitlines()[1:]
    for dependency in dependencies:
        old_path = dependency.strip().split(" ", 1)[0]
        if old_path.endswith("libdxcompiler.dylib") and old_path != "@rpath/libdxcompiler.dylib":
            run(["install_name_tool", "-change", old_path, "@rpath/libdxcompiler.dylib", dxc_path])
    rpaths = capture(["otool", "-l", dxc_path])
    if "@executable_path/../lib" not in rpaths:
        run(["install_name_tool", "-add_rpath", "@executable_path/../lib", dxc_path])
    run(["install_name_tool", "-id", "@rpath/libdxcompiler.dylib", library_path])


def validate_bundle(bundle_root, host):
    executable_suffix = ".exe" if host == "windows-x64" else ""
    dxc_path = bundle_root / f"bin/dxc{executable_suffix}"
    spirv_val_path = bundle_root / f"bin/spirv-val{executable_suffix}"
    validation_root = bundle_root / ".validation"
    if validation_root.exists():
        shutil.rmtree(validation_root)
    validation_root.mkdir(parents=True)
    shaders = {
        "vertex": (
            "vs_6_0",
            "struct VSOutput { float4 position : SV_Position; };\n"
            "VSOutput main(float3 position : POSITION) { VSOutput result; result.position = float4(position, 1.0); return result; }\n"),
        "pixel": ("ps_6_0", "float4 main() : SV_Target0 { return float4(1.0, 0.0, 1.0, 1.0); }\n"),
    }
    try:
        run([dxc_path, "--version"])
        for name, (profile_name, source) in shaders.items():
            source_path = validation_root / f"{name}.hlsl"
            output_path = validation_root / f"{name}.spv"
            source_path.write_text(source, encoding="utf-8", newline="\n")
            run([
                dxc_path, "-spirv", "-fspv-target-env=vulkan1.1", "-fvk-use-dx-layout", "-Zpc",
                "-E", "main", "-T", profile_name, "-O3", "-Fo", output_path, source_path
            ])
            run([spirv_val_path, "--target-env", "vulkan1.1", output_path])
    finally:
        if validation_root.exists():
            shutil.rmtree(validation_root)
    print(f"[Toy3dShaderToolchain] Real DXC validation passed for {host}.")


def create_bundle(lock, sources, builds, bundle_root, host, host_config):
    if bundle_root.name != host or bundle_root.parent == bundle_root:
        raise RuntimeError(
            f"Bundle output must be a host-named directory ({host}), got: {bundle_root}")
    artifacts_on_disk = locate_artifacts(sources, builds, host, host_config)
    if not artifacts_on_disk["spirv_reflect_header"].is_file():
        raise RuntimeError("Expected SPIRV-Reflect public header is missing.")
    if bundle_root.exists():
        shutil.rmtree(bundle_root)
    dxc_library_relative = Path("bin/dxcompiler.dll") if host == "windows-x64" else Path("lib/libdxcompiler.dylib")
    reflect_relative = Path("lib/spirv-reflect-static.lib") if host == "windows-x64" else Path("lib/libspirv-reflect.a")
    executable_suffix = host_config["executable_suffix"]
    fixed_version_file = SCRIPT_DIR / "dxc-version/version.inc"
    common_build_parameters = host_config["generator_arguments"] + host_config["cmake_arguments"] + ["--config", "Release"]
    dxc_build_parameters = common_build_parameters + [
        "-C", "cmake/caches/PredefinedParams.cmake",
        "-DHLSL_FIXED_VERSION_LOCATION=engine/thirdparty/ShaderToolchain/Build/dxc-version",
        f"-DTOY3D_DXC_VERSION_INPUT_SHA256={sha256_file(fixed_version_file)}"
    ] + lock["dxc"]["cmake_arguments"]
    spirv_tools_build_parameters = common_build_parameters + lock["spirv_tools"]["cmake_arguments"] + [
        f"-DSPIRV-Headers_SOURCE_REVISION={lock['spirv_tools']['headers_commit']}"]
    reflect_build_parameters = common_build_parameters + lock["spirv_reflect"]["cmake_arguments"]
    artifacts = {
        "dxc": (artifacts_on_disk["dxc"], Path(f"bin/dxc{executable_suffix}"), lock["dxc"], dxc_build_parameters),
        "dxc_library": (artifacts_on_disk["dxc_library"], dxc_library_relative, lock["dxc"], dxc_build_parameters),
        "spirv_val": (
            artifacts_on_disk["spirv_val"], Path(f"bin/spirv-val{executable_suffix}"),
            lock["spirv_tools"], spirv_tools_build_parameters),
        "spirv_reflect": (
            artifacts_on_disk["spirv_reflect"], reflect_relative,
            lock["spirv_reflect"], reflect_build_parameters),
        "spirv_reflect_header": (
            artifacts_on_disk["spirv_reflect_header"], Path("include/spirv_reflect.h"),
            lock["spirv_reflect"], ["public-header"]),
    }
    for source, relative_path, _, _ in artifacts.values():
        copy_file(source, bundle_root / relative_path)
    license_files = {
        sources / "dxc/LICENSE.TXT": bundle_root / "licenses/DXC-LICENSE.txt",
        sources / "dxc/external/SPIRV-Tools/LICENSE": bundle_root / "licenses/SPIRV-Tools-LICENSE.txt",
        sources / "dxc/external/SPIRV-Headers/LICENSE": bundle_root / "licenses/SPIRV-Headers-LICENSE.txt",
        sources / "dxc/external/DirectX-Headers/LICENSE": bundle_root / "licenses/DirectX-Headers-LICENSE.txt",
        sources / "spirv-reflect/LICENSE": bundle_root / "licenses/SPIRV-Reflect-LICENSE.txt",
    }
    for source, destination in license_files.items():
        copy_file(source, destination)
    if host.startswith("macos-"):
        require_tool("otool")
        require_tool("install_name_tool")
        prepare_macos_runtime(bundle_root)
    artifact_identity = hashlib.sha256()
    for prefix in sorted(artifacts):
        artifact_identity.update(prefix.encode("utf-8"))
        artifact_identity.update(bytes.fromhex(sha256_file(bundle_root / artifacts[prefix][1])))
    bundle_identity = f"{lock['bundle_identity']}/{host}/{artifact_identity.hexdigest()[:16]}"
    dxc_cache = builds / "dxc/CMakeCache.txt"
    lines = [
        "# Generated by bootstrap_shader_toolchain.py. Do not edit.",
        "manifest_version=1",
        f"bundle_identity={bundle_identity}",
        f"host_platform={host}",
        f"build.cmake_version={capture(['cmake', '--version']).splitlines()[0]}",
        f"build.generator={host_config['generator']}",
        "build.configuration=Release",
        f"build.cxx_compiler={cache_value(dxc_cache, 'CMAKE_CXX_COMPILER')}",
    ]
    if host == "windows-x64":
        lines.extend([
            "build.generator_platform=x64",
            f"build.msvc_toolset={path_version(dxc_cache, 'CMAKE_AR', '/VC/Tools/MSVC/')}",
            f"build.windows_sdk={path_version(dxc_cache, 'D3D12_INCLUDE_DIR', '/Windows Kits/10/Include/')}",
        ])
    else:
        lines.extend([
            f"build.osx_architectures={cache_value(dxc_cache, 'CMAKE_OSX_ARCHITECTURES')}",
            f"build.osx_deployment_target={cache_value(dxc_cache, 'CMAKE_OSX_DEPLOYMENT_TARGET')}",
            f"build.macos_sdk={capture(['xcrun', '--sdk', 'macosx', '--show-sdk-version'])}",
        ])
    for prefix, (_, relative_path, component, parameters) in artifacts.items():
        lines.extend(manifest_artifact(
            prefix, relative_path, bundle_root / relative_path, component, parameters))
    lines.extend([
        f"dxc.source_tag={lock['dxc']['tag']}",
        f"dxc.directx_headers_revision={lock['dxc']['submodules']['external/DirectX-Headers']}",
        f"dxc.spirv_headers_revision={lock['dxc']['submodules']['external/SPIRV-Headers']}",
        f"dxc.spirv_tools_revision={lock['dxc']['submodules']['external/SPIRV-Tools']}",
        f"spirv_val.source_tag={lock['spirv_tools']['tag']}",
        f"spirv_val.headers_source_tag={lock['spirv_tools']['headers_tag']}",
        f"spirv_val.headers_source_revision={lock['spirv_tools']['headers_commit']}",
        f"spirv_reflect.source_tag={lock['spirv_reflect']['tag']}",
    ])
    deferred = lock["deferred_microsoft_binaries"]
    lines.extend([
        f"d3dcompiler.version={deferred['d3dcompiler_version']}",
        f"d3dcompiler.source_url={deferred['d3dcompiler_source_url']}",
        f"d3dcompiler.license={deferred['d3dcompiler_license']}",
        f"dxil_validator.version={deferred['dxil_validator_version']}",
        f"dxil_validator.source_url={deferred['dxil_validator_source_url']}",
        f"dxil_validator.license={deferred['dxil_validator_license']}",
    ])
    manifest_path = bundle_root / "Toy3dShaderToolchain.manifest"
    manifest_path.write_text("\n".join(lines) + "\n", encoding="utf-8", newline="\n")
    validate_bundle(bundle_root, host)
    print(f"[Toy3dShaderToolchain] Bundle ready: {bundle_root}")
    print(f"[Toy3dShaderToolchain] Manifest: {manifest_path}")


def parse_arguments():
    parser = argparse.ArgumentParser(
        description="Build and validate the locked Toy3d Shader toolchain outside normal CMake builds.")
    parser.add_argument("--lock-file", type=Path, default=DEFAULT_LOCK_FILE)
    parser.add_argument("--work-root", type=Path, default=DEFAULT_WORK_ROOT)
    parser.add_argument("--bundle-root", type=Path)
    mode = parser.add_mutually_exclusive_group()
    mode.add_argument("--skip-build", action="store_true", help="Only fetch and verify locked source revisions.")
    mode.add_argument("--package-only", action="store_true", help="Regenerate and validate a bundle from existing builds.")
    return parser.parse_args()


def main():
    arguments = parse_arguments()
    require_tool("git")
    require_tool("cmake")
    lock = json.loads(arguments.lock_file.resolve().read_text(encoding="utf-8"))
    if lock.get("lock_version") != 1:
        raise RuntimeError("Unsupported Shader toolchain lock version.")
    verify_lock_consistency(lock)
    host = host_platform()
    host_config = platform_configuration(host)
    work_root = arguments.work_root.resolve()
    sources = work_root / "sources"
    builds = work_root / f"build/{host}"
    legacy_builds = work_root / "build"
    if arguments.package_only and not builds.exists() and (legacy_builds / "dxc").exists():
        builds = legacy_builds
        print(f"[Toy3dShaderToolchain] Using legacy locked build tree: {builds}")
    bundle_root = (arguments.bundle_root or (work_root / f"bundle/{host}")).resolve()
    dxc_source = sources / "dxc"
    reflect_source = sources / "spirv-reflect"
    SAFE_GIT_DIRECTORIES.extend([
        dxc_source,
        dxc_source / "external/DirectX-Headers",
        dxc_source / "external/SPIRV-Headers",
        dxc_source / "external/SPIRV-Tools",
        reflect_source,
    ])
    ensure_checkout(lock["dxc"]["url"], lock["dxc"]["tag"], lock["dxc"]["commit"], dxc_source)
    ensure_dxc_submodules(dxc_source, lock["dxc"]["submodules"])
    ensure_checkout(
        lock["spirv_reflect"]["url"], lock["spirv_reflect"]["tag"],
        lock["spirv_reflect"]["commit"], reflect_source)
    if arguments.skip_build:
        print("[Toy3dShaderToolchain] Locked dependency sources are ready.")
        return 0
    if not arguments.package_only:
        configure(
            dxc_source, builds / "dxc", lock["dxc"]["cmake_arguments"], host_config,
            dxc_source / "cmake/caches/PredefinedParams.cmake",
            [f"-DHLSL_FIXED_VERSION_LOCATION={(SCRIPT_DIR / 'dxc-version').as_posix()}"])
        build(builds / "dxc", ["dxc", "dxcompiler"])
        spirv_tools_source = dxc_source / "external/SPIRV-Tools"
        spirv_headers_source = dxc_source / "external/SPIRV-Headers"
        configure(
            spirv_tools_source, builds / "spirv-tools", lock["spirv_tools"]["cmake_arguments"], host_config,
            extra_arguments=[f"-DSPIRV-Headers_SOURCE_DIR={spirv_headers_source.as_posix()}"])
        build(builds / "spirv-tools", ["spirv-val"])
        configure(
            reflect_source, builds / "spirv-reflect", lock["spirv_reflect"]["cmake_arguments"], host_config)
        build(builds / "spirv-reflect", ["spirv-reflect-static"])
    create_bundle(lock, sources, builds, bundle_root, host, host_config)
    return 0


if __name__ == "__main__":
    try:
        sys.exit(main())
    except (OSError, RuntimeError, subprocess.CalledProcessError, json.JSONDecodeError) as error:
        print(f"[Toy3dShaderToolchain] ERROR: {error}", file=sys.stderr)
        sys.exit(1)
