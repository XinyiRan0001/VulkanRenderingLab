param([string]$Executable="build/Release/VulkanRenderingLab.exe",[string]$Output="build/ao-sequence",[switch]$Adaptive)
$ErrorActionPreference="Stop"
$root=Split-Path $PSScriptRoot -Parent
$exe=(Resolve-Path (Join-Path $root $Executable)).Path
$directory=[IO.Path]::GetFullPath((Join-Path $root $Output))
New-Item -ItemType Directory -Force -Path $directory | Out-Null
Push-Location $root
try {
    # Identical deterministic sample phases and camera poses in both runs.
    # Frames 0..24 stationary; orbit through 36; hold 36..47. No timing claims.
    $response=if($Adaptive){"adaptive"}else{"fixed"}
    foreach($mode in @("off","on")) {
        $prefix=Join-Path $directory $mode
        & $exe --ao-response $response --scene assets/models/ssao-validation/corner.gltf --ssao-blurred --ao-sampling varying --ao-temporal $mode --ao-samples 4 --paused --width 641 --height 361 --yaw 45 --pitch 25 --camera-distance 3.5 --frames 48 --camera-sweep .4 --camera-sweep-start-at 24 --camera-sweep-stop-at 36 --capture-every-frame --capture $prefix *> "$prefix.log"
        if($LASTEXITCODE -ne 0){throw "Sequence render failed: $prefix.log"}
        for($frame=0;$frame -lt 48;++$frame) {
            if(!(Test-Path -LiteralPath "$prefix.frame$frame.png")){throw "Missing frame $frame in $mode"}
        }
        $content=Get-Content -Raw "$prefix.log"
        if($content -match 'VUID-|SYNC-HAZARD|Validation Error'){throw "Vulkan validation failed: $prefix.log"}
        Write-Host "Captured $mode (48 frames) -> $directory"
    }
    Write-Host "Analyze: python tools/analyze-ssao-sequence.py `"$directory`""
} finally {Pop-Location}
