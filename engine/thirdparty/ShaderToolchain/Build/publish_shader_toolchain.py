#!/usr/bin/env python3

import argparse
from pathlib import Path
import shutil
import subprocess
import sys

import bootstrap_shader_toolchain as bootstrap


SCRIPT_DIR = Path(__file__).resolve().parent
TOOLCHAIN_ROOT = SCRIPT_DIR.parent


def parse_arguments():
    parser = argparse.ArgumentParser(
        description="Build, validate, and publish the current-host Shader toolchain bundle.")
    parser.add_argument("--package-only", action="store_true", help="Publish from the existing locked build tree.")
    parser.add_argument("--work-root", type=Path, default=bootstrap.DEFAULT_WORK_ROOT)
    return parser.parse_args()


def replace_published_bundle(staging_root, target_root, backup_root):
    expected_parent = TOOLCHAIN_ROOT.resolve()
    if target_root.resolve().parent != expected_parent:
        raise RuntimeError(f"Refusing to publish outside ShaderToolchain: {target_root}")
    if backup_root.exists():
        shutil.rmtree(backup_root)
    had_previous_bundle = target_root.exists()
    if had_previous_bundle:
        backup_root.parent.mkdir(parents=True, exist_ok=True)
        shutil.move(str(target_root), str(backup_root))
    try:
        shutil.move(str(staging_root), str(target_root))
    except Exception:
        if target_root.exists():
            shutil.rmtree(target_root)
        if had_previous_bundle and backup_root.exists():
            shutil.move(str(backup_root), str(target_root))
        raise
    if backup_root.exists():
        shutil.rmtree(backup_root)


def main():
    arguments = parse_arguments()
    host = bootstrap.host_platform()
    work_root = arguments.work_root.resolve()
    staging_root = work_root / f"staging/{host}"
    backup_root = work_root / f"publish-backup/{host}"
    target_root = TOOLCHAIN_ROOT / host
    if staging_root.exists():
        shutil.rmtree(staging_root)
    command = [
        sys.executable,
        SCRIPT_DIR / "bootstrap_shader_toolchain.py",
        "--work-root", work_root,
        "--bundle-root", staging_root,
    ]
    if arguments.package_only:
        command.append("--package-only")
    bootstrap.run(command)
    replace_published_bundle(staging_root, target_root, backup_root)
    print(f"[Toy3dShaderToolchain] Published verified {host} bundle: {target_root}")
    print("[Toy3dShaderToolchain] Review and commit the updated bundle and manifest.")
    return 0


if __name__ == "__main__":
    try:
        sys.exit(main())
    except (OSError, RuntimeError, subprocess.CalledProcessError) as error:
        print(f"[Toy3dShaderToolchain] ERROR: {error}", file=sys.stderr)
        sys.exit(1)
