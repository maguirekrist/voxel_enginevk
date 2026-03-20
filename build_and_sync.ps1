param(
    [string]$Config = "Debug"
)

$ErrorActionPreference = "Stop"

$repoRoot = Split-Path -Parent $MyInvocation.MyCommand.Path
$buildDir = Join-Path $repoRoot "build"
$shaderSourceDir = Join-Path $repoRoot "shaders"
$runtimeDir = Join-Path $repoRoot ("bin\" + $Config)
$runtimeShaderDir = Join-Path $runtimeDir "shaders"

Write-Host "Building configuration '$Config'..."
cmake --build $buildDir --config $Config
if ($LASTEXITCODE -ne 0) {
    throw "Build failed."
}

if (-not (Test-Path $runtimeDir)) {
    throw "Runtime output directory not found: $runtimeDir"
}

Write-Host "Refreshing runtime shader directory..."
if (Test-Path $runtimeShaderDir) {
    Remove-Item -Path $runtimeShaderDir -Recurse -Force
}

New-Item -ItemType Directory -Path $runtimeShaderDir | Out-Null
Copy-Item -Path (Join-Path $shaderSourceDir "*") -Destination $runtimeShaderDir -Recurse -Force

Write-Host "Build and shader sync complete."
