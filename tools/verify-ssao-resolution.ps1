param([string]$Executable = "build/Release/VulkanRenderingLab.exe")
$ErrorActionPreference = "Stop"
$root = Split-Path $PSScriptRoot -Parent
$exe = (Resolve-Path (Join-Path $root $Executable)).Path
$logDir = Join-Path $root "build/ssao-resolution-validation"
New-Item -ItemType Directory -Force -Path $logDir | Out-Null
$oldLayers=$env:VK_INSTANCE_LAYERS; $oldSync=$env:VK_LAYER_VALIDATE_SYNC
Push-Location $root
try {
    $env:VK_INSTANCE_LAYERS="VK_LAYER_KHRONOS_validation"
    $env:VK_LAYER_VALIDATE_SYNC="1"
    foreach($scene in @("plane","corner","thin-layers")) {
        foreach($scale in @("full","half")) {
            $log=Join-Path $logDir "$scene-$scale.log"
            & $exe --scene "assets/models/ssao-validation/$scene.gltf" --ssao-blurred --ao-resolution $scale --verify-prepass --paused --width 641 --height 361 --yaw 45 --pitch 25 --frames 2 *> $log
            if($LASTEXITCODE -ne 0){throw "Resolution test failed: $log"}
            $content=Get-Content -Raw $log
            if($content -match 'VUID-|SYNC-HAZARD|Validation Error'){throw "Vulkan validation failed: $log"}
            $resolve=[regex]::Match($content,'\[Resolve Reference\] (\d+) pixels max error ([0-9.eE+-]+)')
            if(!$resolve.Success -or [int]$resolve.Groups[1].Value -eq 0){throw "Missing resolve reference: $log"}
            $raw=[regex]::Match($content,'\[SSAO Verify\].*covered (\d+) min ([0-9.eE+-]+)')
            if(!$raw.Success -or [int]$raw.Groups[1].Value -eq 0){throw "Empty AO scene: $log"}
            $minimum=[double]::Parse($raw.Groups[2].Value,[Globalization.CultureInfo]::InvariantCulture)
            if($scene -eq "plane" -and $minimum -lt .995){throw "Plane self-occludes: $log"}
            Write-Host "PASS $scene resolution=$scale NPOT map + blur + resolve reference"
        }
    }
} finally {
    Pop-Location
    $env:VK_INSTANCE_LAYERS=$oldLayers; $env:VK_LAYER_VALIDATE_SYNC=$oldSync
}
