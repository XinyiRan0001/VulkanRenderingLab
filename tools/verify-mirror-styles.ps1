$ErrorActionPreference="Stop"
$root=Split-Path $PSScriptRoot -Parent
$exe=Join-Path $root "build/Release/VulkanRenderingLab.exe"
$directory=Join-Path $root "build/mirror-style-validation"
New-Item -ItemType Directory -Force -Path $directory | Out-Null
$oldLayers=$env:VK_INSTANCE_LAYERS; $oldSync=$env:VK_LAYER_VALIDATE_SYNC
Push-Location $root
try {
    $env:VK_INSTANCE_LAYERS="VK_LAYER_KHRONOS_validation"; $env:VK_LAYER_VALIDATE_SYNC="1"
    foreach($mode in @("on","off")){
        $log=Join-Path $directory "$mode.log"
        & $exe --ray-query-scene --animated-scene assets/models/character/character.glb --animation Hello --animation-time .52 --no-animation-panel --paused --width 960 --height 640 --frames 3 --verify-ray-query --mirror-styles $mode --capture "$directory/$mode.png" *> $log
        if($LASTEXITCODE -ne 0){throw "Mirror style failed: $log"}
        $content=Get-Content -Raw $log
        if($content -match 'VUID-|SYNC-HAZARD|Validation Error'){throw "Vulkan validation failed: $log"}
        if($content -notmatch '\[Ray Skinning Verify\]' -or $content -notmatch '\[Ray Query Verify\]'){throw "Missing geometry/ray verification: $log"}
        Write-Host "PASS mirror styles $mode : animated geometry + ray queries + sync + capture"
    }
} finally {
    Pop-Location
    $env:VK_INSTANCE_LAYERS=$oldLayers; $env:VK_LAYER_VALIDATE_SYNC=$oldSync
}
