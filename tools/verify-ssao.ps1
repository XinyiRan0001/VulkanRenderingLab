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
    # Same deterministic raw input, two guidance modes. The GPU readback also
    # checks the bilateral result against a CPU reference and white background.
    foreach ($scene in @("tilted-corner", "thin-layers")) {
        foreach ($guidance in @("depth", "plane")) {
            $log = Join-Path $logDir "$scene-$guidance.log"
            $cameraArgs = if ($scene -eq "tilted-corner") {
                @("--yaw", "20", "--pitch", "9", "--ao-radius", "0.2", "--ao-bias", "0.01")
            } else { @("--yaw", "0", "--pitch", "12") }
            & $exe --scene "assets/models/ssao-validation/$scene.gltf" --ssao-blurred --ao-blur $guidance --verify-prepass --paused --width 640 --height 360 --ao-samples 16 --frames 2 @cameraArgs *> $log
            if ($LASTEXITCODE -ne 0) { throw "AO guidance test failed: $log" }
            $content = Get-Content -Raw $log
            if ($content -match 'VUID-|SYNC-HAZARD|Validation Error') { throw "Vulkan validation failed: $log" }
            $reference = [regex]::Match($content, '\[Blur Reference\] (\d+) pixels max error ([0-9.eE+-]+).*guidance ' + $guidance)
            if (!$reference.Success -or [int]$reference.Groups[1].Value -eq 0) { throw "Missing covered blur reference: $log" }
            $blur = [regex]::Match($content, '\[Blur Verify\].*min ([0-9.eE+-]+) max ([0-9.eE+-]+)')
            if (!$blur.Success) { throw "Missing blurred AO verification: $log" }
            Write-Host "PASS $scene guidance=$guidance GPU/CPU reference checked"
        }
    }
} finally {
    Pop-Location
    $env:VK_INSTANCE_LAYERS = $oldLayers
    $env:VK_LAYER_VALIDATE_SYNC = $oldSync
}
