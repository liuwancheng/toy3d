param(
    [Parameter(Mandatory = $true)][string]$Project,
    [Parameter(Mandatory = $true)][string]$Output,
    [string]$Build = "$PSScriptRoot/../build",
    [ValidateSet('Debug', 'Release', 'RelWithDebInfo', 'MinSizeRel')][string]$Configuration = 'Debug',
    [switch]$Run
)
$ErrorActionPreference = 'Stop'
$projectPath = (Resolve-Path -LiteralPath $Project).Path
$outputPath = [System.IO.Path]::GetFullPath($Output)
$buildPath = [System.IO.Path]::GetFullPath($Build)
& cmake "-DTOY3D_PROJECT=$projectPath" "-DTOY3D_OUTPUT=$outputPath" "-DTOY3D_BUILD=$buildPath" `
    "-DTOY3D_CONFIGURATION=$Configuration" "-DTOY3D_RUN=$($Run.IsPresent)" `
    -P "$PSScriptRoot/../engine/build/cmake/package_project.cmake"
exit $LASTEXITCODE
