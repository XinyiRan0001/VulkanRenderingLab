param([string]$Executable = "build/Release/VulkanRenderingLab.exe")
$ErrorActionPreference = "Stop"
$root = Split-Path $PSScriptRoot -Parent
$exe = (Resolve-Path (Join-Path $root $Executable)).Path
$logDir = Join-Path $root "build/ssao-validation"
New-Item -ItemType Directory -Force -Path $logDir | Out-Null
$oldLayers = $env:VK_INSTANCE_LAYERS
$oldSync = $env:VK_LAYER_VALIDATE_SYNC
Push-Location $root
try {
    $env:VK_INSTANCE_LAYERS = "VK_LAYER_KHRONOS_validation"
    $env:VK_LAYER_VALIDATE_SYNC = "1"
    foreach ($distance in @(3.5, 6)) {
        foreach ($scene in @("plane", "corner")) {
            $log = Join-Path $logDir "$scene-$distance.log"
            & $exe --scene "assets/models/ssao-validation/$scene.gltf" --ssao-view --verify-prepass --paused --yaw 45 --pitch 25 --camera-distance $distance --width 640 --height 360 --ao-samples 32 --frames 2 *> $log
            if ($LASTEXITCODE -ne 0) { throw "Renderer failed: $log" }
            $content = Get-Content -Raw $log
            if ($content -match 'VUID-|SYNC-HAZARD|Validation Error') { throw "Vulkan validation failed: $log" }
            $result = [regex]::Match($content, '\[SSAO Verify\].*covered (\d+) min ([0-9.]+) max ([0-9.]+) mean ([0-9.]+)')
            if (!$result.Success -or [int]$result.Groups[1].Value -eq 0) { throw "Missing covered AO verification: $log" }
            $minimum = [double]::Parse($result.Groups[2].Value, [Globalization.CultureInfo]::InvariantCulture)
            if ($scene -eq "plane" -and $minimum -lt 0.995) { throw "Isolated plane self-occludes: $log" }
            if ($scene -eq "corner" -and $minimum -gt 0.95) { throw "Corner has no meaningful contact occlusion: $log" }
            Write-Host "PASS $scene distance=$distance minimum=$minimum"
        }
    }
} finally {
    Pop-Location
    $env:VK_INSTANCE_LAYERS = $oldLayers
    $env:VK_LAYER_VALIDATE_SYNC = $oldSync
}
