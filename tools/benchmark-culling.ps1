param(
    [int[]]$Counts = @(1600, 6400, 10000),
    [string]$OutputDirectory = "build/culling-benchmark",
    [int]$Warmup = 30,
    [int]$Samples = 120,
    [int]$Repeats = 3,
    [switch]$MixedScene
)
$ErrorActionPreference = "Stop"
$projectRoot = Split-Path -Parent $PSScriptRoot
Push-Location -LiteralPath $projectRoot
try {
    New-Item -ItemType Directory -Force -Path $OutputDirectory | Out-Null
    foreach ($count in $Counts) {
        if ($count -lt 1 -or $count -gt 10000) { throw "Counts must be 1..10000" }
        $prefix = if ($MixedScene) { "mixed" } else { "culling" }
        $csvPath = Join-Path $OutputDirectory "$prefix-$count.csv"
        $benchmarkArguments = @("--stress-count", $count, "--benchmark-culling", $csvPath, "--benchmark-warmup", $Warmup, "--benchmark-samples", $Samples, "--benchmark-repeats", $Repeats)
        if ($MixedScene) { $benchmarkArguments += "--mixed-scene" }
        & .\build\Release\VulkanRenderingLab.exe @benchmarkArguments
        if ($LASTEXITCODE -ne 0) { throw "Benchmark failed for $count instances" }
    }
} finally { Pop-Location }
