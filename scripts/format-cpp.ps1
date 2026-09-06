[CmdletBinding()]
param(
    # Format only files changed relative to HEAD. This is the normal developer mode.
    [switch]$Changed,

    # Check formatting without changing files. Suitable for CI and pre-commit use.
    [switch]$Check,

    # Override the formatter executable when a non-default LLVM installation is used.
    [string]$ClangFormat
)

$ErrorActionPreference = "Stop"

function Get-ClangFormatPath {
    param([string]$RequestedPath)

    if ($RequestedPath) {
        if (-not (Test-Path -LiteralPath $RequestedPath -PathType Leaf)) {
            throw "clang-format was not found at '$RequestedPath'."
        }
        return (Resolve-Path -LiteralPath $RequestedPath).Path
    }

    $fromPath = Get-Command "clang-format" -ErrorAction SilentlyContinue
    if ($fromPath) {
        return $fromPath.Source
    }

    $visualStudioPath =
        "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Tools\Llvm\x64\bin\clang-format.exe"
    if (Test-Path -LiteralPath $visualStudioPath -PathType Leaf) {
        return $visualStudioPath
    }

    throw "clang-format was not found. Install LLVM or Visual Studio's C++ Clang tools, or pass -ClangFormat."
}

function Test-FirstPartyCppPath {
    param([string]$Path)

    $normalizedPath = $Path.Replace("\\", "/")
    if ($normalizedPath -match "^(engine/thirdparty|engine/runtime/generated|build|bin)/") {
        return $false
    }

    return $normalizedPath -match "\.(c|cc|cpp|cxx|h|hpp)$"
}

$repositoryRoot = (git rev-parse --show-toplevel).Trim()
if (-not $repositoryRoot) {
    throw "The script must run inside a Git worktree."
}
Set-Location -LiteralPath $repositoryRoot

$formatter = Get-ClangFormatPath $ClangFormat
if ($Changed) {
    $candidates = git diff --name-only --diff-filter=ACMR HEAD
}
else {
    $candidates = git ls-files
}

$files = @($candidates | Where-Object {
    $_ -and (Test-FirstPartyCppPath $_)
})
if ($files.Count -eq 0) {
    Write-Host "No first-party C/C++ files require formatting."
    exit 0
}

$arguments = @("--style=file", "--fallback-style=none")
if ($Check) {
    $arguments += "--dry-run"
    $arguments += "--Werror"
}
else {
    $arguments += "-i"
}

# Keep command lines under the Windows process length limit while avoiding a
# process launch per file.
$batchSize = 64
for ($offset = 0; $offset -lt $files.Count; $offset += $batchSize) {
    $last = [Math]::Min($offset + $batchSize - 1, $files.Count - 1)
    $batch = $files[$offset..$last]
    & $formatter @arguments -- $batch
    if ($LASTEXITCODE -ne 0) {
        exit $LASTEXITCODE
    }
}

if ($Check) {
    Write-Host "Formatting check passed for $($files.Count) first-party C/C++ files."
}
else {
    Write-Host "Formatted $($files.Count) first-party C/C++ files."
}
