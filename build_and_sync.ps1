param(
    [ValidateSet("Debug", "Release")]
    [string]$Config = "Debug",
    [switch]$Release,
    [string]$Target = "vulkan_guide",
    [int]$Parallel = 0,
    [switch]$AllTargets
)

$ErrorActionPreference = "Stop"

if ($Release) {
    $Config = "Release"
}

if ($Parallel -lt 1) {
    $Parallel = [Math]::Max(1, [Environment]::ProcessorCount)
}

$repoRoot = Split-Path -Parent $MyInvocation.MyCommand.Path
$buildDir = Join-Path $repoRoot "build"
$shaderSourceDir = Join-Path $repoRoot "shaders"
$runtimeDir = Join-Path $repoRoot ("bin\" + $Config)
$runtimeShaderDir = Join-Path $runtimeDir "shaders"

if ($AllTargets) {
    Write-Host "Building configuration '$Config' (ALL_BUILD) with $Parallel workers..."
    $cmakeArgs = @("--build", $buildDir, "--config", $Config, "--parallel", $Parallel)
} else {
    Write-Host "Building configuration '$Config' target '$Target' with $Parallel workers..."
    $cmakeArgs = @("--build", $buildDir, "--config", $Config, "--target", $Target, "--parallel", $Parallel)
}

& cmake @cmakeArgs

# Write-Host "Building configuration '$Config'..."
# cmake --build $buildDir --config $Config


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
