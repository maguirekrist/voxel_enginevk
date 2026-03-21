param(
    [ValidateSet("Debug", "Release")]
    [string]$Config = "Debug",
    [switch]$Release
)

$ErrorActionPreference = "Stop"

if ($Release) {
    $Config = "Release"
}

$repoRoot = Split-Path -Parent $MyInvocation.MyCommand.Path
$buildDir = Join-Path $repoRoot "build"

Write-Host "Running tests for configuration '$Config'..."
ctest --test-dir $buildDir --output-on-failure -C $Config

if ($LASTEXITCODE -ne 0) {
    throw "Tests failed."
}

Write-Host "Tests passed."
