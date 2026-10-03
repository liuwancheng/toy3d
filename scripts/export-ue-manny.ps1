param(
    [Parameter(Mandatory = $true)]
    [string]$UnrealRoot,
    [string]$OutputDirectory
)

$ErrorActionPreference = 'Stop'
$repoRoot = Split-Path -Parent $PSScriptRoot
$unrealRootPath = (Resolve-Path -LiteralPath $UnrealRoot).Path
$editor = Join-Path $unrealRootPath 'Engine/Binaries/Win64/UnrealEditor-Cmd.exe'
$source = Join-Path $unrealRootPath 'Templates/TemplateResources/High/Characters/Content/Mannequins'
if (!(Test-Path -LiteralPath $editor -PathType Leaf) -or !(Test-Path -LiteralPath $source -PathType Container))
{
    throw 'UE editor commandlet and the High/Characters Manny template are required.'
}
if (!$OutputDirectory)
{
    $OutputDirectory = Join-Path $repoRoot 'project/source/animation/ue_manny'
}
$output = [IO.Path]::GetFullPath($OutputDirectory)
$staging = Join-Path $repoRoot 'build/verification/ue-manny-export'
$content = Join-Path $staging 'Content/Characters'
New-Item -ItemType Directory -Force -Path $content, $output | Out-Null
# Preserve the original /Game/Characters/Mannequins package paths and never save the source UE assets.
Copy-Item -LiteralPath $source -Destination $content -Recurse -Force
$projectFile = Join-Path $staging 'MannyExport.uproject'
@{
    FileVersion = 3
    Description = 'Temporary local Manny resource export'
    Plugins = @(@{ Name = 'PythonScriptPlugin'; Enabled = $true })
} | ConvertTo-Json -Depth 4 | Set-Content -LiteralPath $projectFile -Encoding utf8
$exportScript = Join-Path $PSScriptRoot 'export_ue_manny.py'
$log = Join-Path $staging 'export.log'
$previousOutput = $env:TOY3D_MANNY_EXPORT_OUTPUT
$previousSource = $env:TOY3D_MANNY_EXPORT_SOURCE
$env:TOY3D_MANNY_EXPORT_OUTPUT = $output
$env:TOY3D_MANNY_EXPORT_SOURCE = $source
try
{
    # UE 5.5's skeletal FBX exporter retrieves a CPU-skinned mesh through a registered render component.
    # NullRHI cannot create that component's MeshObject, even when material baking is disabled.
    & $editor $projectFile '-run=pythonscript' "-script=$exportScript" '-unattended' '-AllowCommandletRendering' '-d3d11' '-nosplash' '-nop4' '-nosound' '-stdout' '-DDC=NoZenLocalFallback' "-ShaderWorkingDir=$staging/ShaderWorkingDir" "-LocalDataCachePath=$staging/DerivedDataCache" "-abslog=$log"
    if ($LASTEXITCODE -ne 0)
    {
        throw "UE export failed with exit code $LASTEXITCODE. See $log"
    }
    if (!(Test-Path -LiteralPath (Join-Path $output 'manifest.json') -PathType Leaf))
    {
        throw "UE did not publish the export manifest. See $log"
    }
}
finally
{
    $env:TOY3D_MANNY_EXPORT_OUTPUT = $previousOutput
    $env:TOY3D_MANNY_EXPORT_SOURCE = $previousSource
}
Write-Output "Manny resources exported to $output"
