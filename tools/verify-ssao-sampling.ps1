param([string]$Executable="build/Release/VulkanRenderingLab.exe")
$ErrorActionPreference="Stop"
$root=Split-Path $PSScriptRoot -Parent
$exe=(Resolve-Path (Join-Path $root $Executable)).Path
$directory=Join-Path $root "build/ssao-sampling-validation"
New-Item -ItemType Directory -Force -Path $directory | Out-Null
$oldLayers=$env:VK_INSTANCE_LAYERS; $oldSync=$env:VK_LAYER_VALIDATE_SYNC
Push-Location $root
try {
    $env:VK_INSTANCE_LAYERS="VK_LAYER_KHRONOS_validation"; $env:VK_LAYER_VALIDATE_SYNC="1"
    foreach($mode in @("fixed","varying")) {
        $frames=if($mode -eq "fixed"){3}else{65}
        $prefix=Join-Path $directory $mode
        & $exe --scene assets/models/ssao-validation/corner.gltf --ssao-view --ao-temporal off --ao-sampling $mode --ao-samples 4 --paused --width 321 --height 181 --yaw 45 --pitch 25 --frames $frames --capture-every-frame --capture $prefix *> "$prefix.log"
        if($LASTEXITCODE -ne 0){throw "Sampling test failed: $prefix.log"}
        if((Get-Content -Raw "$prefix.log") -match 'VUID-|SYNC-HAZARD|Validation Error'){throw "Vulkan validation failed: $prefix.log"}
    }
    $hash={param($file)(Get-FileHash -LiteralPath (Join-Path $directory $file) -Algorithm SHA256).Hash}
    $fixed0=& $hash "fixed.frame0.png"
    if($fixed0 -ne (& $hash "fixed.frame1.png") -or $fixed0 -ne (& $hash "fixed.frame2.png")){throw "Fixed sampling changed while stationary"}
    if($fixed0 -ne (& $hash "varying.frame0.png")){throw "Phase zero changed baseline samples"}
    if((& $hash "varying.frame0.png") -eq (& $hash "varying.frame1.png")){throw "Varying sampling did not change raw AO"}
    if((& $hash "varying.frame0.png") -ne (& $hash "varying.frame64.png")){throw "64-phase sequence did not repeat exactly"}
    Write-Host "PASS fixed stationary identity, phase-zero baseline, varying stimulus, exact 64-frame repeat"
} finally {
    Pop-Location
    $env:VK_INSTANCE_LAYERS=$oldLayers; $env:VK_LAYER_VALIDATE_SYNC=$oldSync
}
