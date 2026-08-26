#!/usr/bin/env bash

# Starts the locked macOS ShaderToolchain publish flow in the background.
# The publisher reuses build/shader-toolchain/sources when its locked checkouts
# already exist, then builds, validates, and publishes the host bundle.
set -euo pipefail

project_root="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
work_root="${project_root}/build/shader-toolchain"
log_path="${work_root}/publish-macos-x64.log"
pid_path="${work_root}/publish-macos-x64.pid"
publisher="${project_root}/engine/thirdparty/ShaderToolchain/Build/publish_macos.sh"

mkdir -p "${work_root}"

if [[ -f "${pid_path}" ]]; then
    previous_pid="$(<"${pid_path}")"
    if [[ "${previous_pid}" =~ ^[0-9]+$ ]] && kill -0 "${previous_pid}" 2>/dev/null; then
        printf 'ShaderToolchain publishing is already running (PID: %s).\n' "${previous_pid}"
        printf 'Log: %s\n' "${log_path}"
        exit 0
    fi
fi

nohup bash "${publisher}" --work-root "${work_root}" \
    >"${log_path}" 2>&1 < /dev/null &
publisher_pid=$!
printf '%s\n' "${publisher_pid}" > "${pid_path}"

printf 'ShaderToolchain publishing started in the background (PID: %s).\n' "${publisher_pid}"
printf 'Log: %s\n' "${log_path}"
printf 'PID file: %s\n' "${pid_path}"
