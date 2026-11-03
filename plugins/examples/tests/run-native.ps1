param([string]$Zig = "zig")
$ErrorActionPreference = "Stop"
$repo = (Resolve-Path (Join-Path $PSScriptRoot "../../..")).Path
$output = Join-Path $repo ".cache/sample-tests"
New-Item -ItemType Directory -Path $output -Force | Out-Null
foreach ($name in @("list-collection", "struct-state")) {
    $exe = Join-Path $output "$name-test.exe"
    & $Zig c++ -std=c++17 -O0 -I"$repo/plugins/examples/common" -I"$repo/libraries/SkyBootstrap/include" (Join-Path $PSScriptRoot "$name-test.cpp") -o $exe
    if ($LASTEXITCODE -ne 0) { throw "Compilation failed: $name" }
    & $exe
    if ($LASTEXITCODE -ne 0) { throw "Tests failed: $name" }
    Write-Host "$name tests passed"
}
