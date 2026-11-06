$ErrorActionPreference="Stop"
$root=Split-Path $PSScriptRoot -Parent
$exe=Join-Path $root "build/Release/VulkanRenderingLab.exe"
$directory=Join-Path $root "build/reflection-brdf-validation"
New-Item -ItemType Directory -Force -Path $directory | Out-Null
$oldLayers=$env:VK_INSTANCE_LAYERS; $oldSync=$env:VK_LAYER_VALIDATE_SYNC
Push-Location $root
try {
    $env:VK_INSTANCE_LAYERS="VK_LAYER_KHRONOS_validation"; $env:VK_LAYER_VALIDATE_SYNC="1"
    foreach($case in @(
        @{Name="furnace8";Args=@("--reflection-furnace","--reflection-samples","8")},
        @{Name="furnace256";Args=@("--reflection-furnace","--reflection-reference")},
        @{Name="furnace-grazing";Args=@("--reflection-furnace","--reflection-reference","--pitch","3","--camera-distance","14")},
        @{Name="scene8";Args=@("--reflection-samples","8")},
        @{Name="scene64";Args=@("--reflection-samples","64")},
        @{Name="scene256";Args=@("--reflection-reference")}
    )){
        $log=Join-Path $directory ($case.Name+".log");$extra=$case.Args
        & $exe --rough-reflection-scene --animated-scene assets/models/character/character.glb --animation Hello --animation-time .52 --paused --verify-ray-query --reflection-denoise off --no-animation-panel --lighting-view 3 --ray-reflections on --width 481 --height 321 --frames 2 --capture "$directory/$($case.Name).png" --reflection-dump "$directory/$($case.Name)" @extra *> $log
        if($LASTEXITCODE -ne 0){throw "Reflection BRDF failed: $log"}
        $content=Get-Content -Raw $log
        if($content -match 'VUID-|SYNC-HAZARD|Validation Error'){throw "Vulkan validation failed: $log"}
        if($content -notmatch 'denominator=all_samples receiver_Fresnel=once' -or $content -notmatch '\[Reflection Spatial Verify\]'){throw "Missing BRDF verification inputs: $log"}
        Write-Host "PASS BRDF capture $($case.Name): weighted HDR + sync"
    }
    & python (Join-Path $root "tools/analyze-reflection-brdf.py") $directory
    if($LASTEXITCODE -ne 0){throw "Reflection estimator analysis failed"}
} finally {
    Pop-Location
    $env:VK_INSTANCE_LAYERS=$oldLayers; $env:VK_LAYER_VALIDATE_SYNC=$oldSync
}
