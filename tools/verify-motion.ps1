param([string]$Executable = "build/Release/VulkanRenderingLab.exe")
$ErrorActionPreference="Stop"
$root=Split-Path $PSScriptRoot -Parent
$exe=(Resolve-Path (Join-Path $root $Executable)).Path
$logDir=Join-Path $root "build/motion-validation"
New-Item -ItemType Directory -Force -Path $logDir | Out-Null
$oldLayers=$env:VK_INSTANCE_LAYERS; $oldSync=$env:VK_LAYER_VALIDATE_SYNC
Push-Location $root
try {
    $env:VK_INSTANCE_LAYERS="VK_LAYER_KHRONOS_validation"; $env:VK_LAYER_VALIDATE_SYNC="1"
    $cases=@(
        @{Name="stationary";Args=@("--frames","3")},
        @{Name="camera";Args=@("--frames","4","--camera-sweep","3")},
        @{Name="instance";Args=@("--frames","4","--motion-instance-test")},
        @{Name="reset";Args=@("--frames","5","--camera-sweep","3","--motion-reset-at","2")},
        @{Name="rebuild";Args=@("--frames","4","--ao-resolution","half","--camera-sweep","3","--resize-after","2","--resize-width","961","--resize-height","541")}
    )
    foreach($case in $cases) {
        $log=Join-Path $logDir ($case.Name+".log"); $extra=$case.Args
        & $exe --motion-view --verify-motion --paused --width 641 --height 361 @extra *> $log
        if($LASTEXITCODE -ne 0){throw "Motion run failed: $log"}
        $content=Get-Content -Raw $log
        if($content -match 'VUID-|SYNC-HAZARD|Validation Error'){throw "Vulkan validation failed: $log"}
        $rows=[regex]::Matches($content,'\[Motion Verify\] history=(\d+) candidates=(\d+) invalid=(\d+) max_px=([0-9.eE+-]+) max_error_px=([0-9.eE+-]+)')
        if($rows.Count -lt 3 -or $rows[0].Groups[1].Value -ne "0"){throw "Missing first-frame reset: $log"}
        $last=$rows[$rows.Count-1]
        if($last.Groups[1].Value -ne "1" -or [int]$last.Groups[2].Value -eq 0){throw "No previous-projection candidates: $log"}
        $maximum=[double]::Parse($last.Groups[4].Value,[Globalization.CultureInfo]::InvariantCulture)
        if($case.Name -eq "stationary" -and $maximum -gt .02){throw "Stationary motion exceeds tolerance: $log"}
        if($case.Name -in @("camera","instance") -and $maximum -lt .1){throw "Motion stimulus produced no motion: $log"}
        if($case.Name -eq "reset" -and ($rows[2].Groups[1].Value -ne "0" -or [double]$rows[2].Groups[4].Value -ne 0)){throw "Explicit reset failed: $log"}
        if($case.Name -eq "rebuild" -and @($rows | Where-Object {$_.Groups[1].Value -eq "0"}).Count -lt 2){throw "Rebuild retained stale history: $log"}
        Write-Host "PASS motion $($case.Name) GPU projection + history lifecycle"
    }
} finally {
    Pop-Location
    $env:VK_INSTANCE_LAYERS=$oldLayers; $env:VK_LAYER_VALIDATE_SYNC=$oldSync
}
