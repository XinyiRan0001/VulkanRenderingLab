param([string]$Executable="build/Release/VulkanRenderingLab.exe",[switch]$Varying,[switch]$Adaptive)
$ErrorActionPreference="Stop"
$root=Split-Path $PSScriptRoot -Parent
$exe=(Resolve-Path (Join-Path $root $Executable)).Path
$logDir=Join-Path $root "build/ssao-temporal-validation"
New-Item -ItemType Directory -Force -Path $logDir | Out-Null
$oldLayers=$env:VK_INSTANCE_LAYERS; $oldSync=$env:VK_LAYER_VALIDATE_SYNC
Push-Location $root
try {
    $env:VK_INSTANCE_LAYERS="VK_LAYER_KHRONOS_validation"; $env:VK_LAYER_VALIDATE_SYNC="1"
    $cases=@(
        @{Name="stationary";Args=@("--scene","assets/models/ssao-validation/corner.gltf","--frames","4")},
        @{Name="camera";Args=@("--frames","5","--camera-sweep",".5")},
        @{Name="thin-half";Args=@("--scene","assets/models/ssao-validation/thin-layers.gltf","--ao-resolution","half","--frames","5","--camera-sweep","1")},
        @{Name="instance";Args=@("--frames","5","--motion-instance-test")},
        @{Name="reset";Args=@("--frames","5","--camera-sweep",".5","--motion-reset-at","2")},
        @{Name="toggle";Args=@("--frames","6","--camera-sweep",".5","--ao-temporal-cycle-test")},
        @{Name="gap";Args=@("--frames","10","--ao-cycle-test")},
        @{Name="rebuild";Args=@("--frames","5","--camera-sweep",".5","--ao-resolution","half","--resize-after","2","--resize-width","961","--resize-height","541")},
        @{Name="scale";Args=@("--frames","6","--camera-sweep",".5","--ao-resolution-cycle-test")},
        @{Name="off";Args=@("--frames","3","--camera-sweep",".5","--ao-temporal","off")}
    )
    $response=if($Adaptive){"adaptive"}else{"fixed"}
    $sampling=if($Varying){"varying"}else{"fixed"}
    foreach($case in $cases) {
        $log=Join-Path $logDir ($case.Name+".log");$extra=$case.Args
        & $exe --ao-response $response --ao-sampling $sampling --ssao-blurred --ao-temporal on --verify-prepass --paused --width 641 --height 361 --ao-samples 8 @extra *> $log
        if($LASTEXITCODE -ne 0){throw "Temporal AO failed: $log"}
        $content=Get-Content -Raw $log
        if($content -match 'VUID-|SYNC-HAZARD|Validation Error'){throw "Vulkan validation failed: $log"}
        $rows=[regex]::Matches($content,'\[Temporal AO Verify\] history=(\d+) enabled=(\d+) accepted=(\d+) rejected=(\d+) max_change=([0-9.eE+-]+) max_error=([0-9.eE+-]+)')
        if($rows.Count -lt 3 -or $rows[0].Groups[1].Value -ne "0"){throw "Missing reset/reference results: $log"}
        $valid=@($rows | Where-Object {$_.Groups[1].Value -eq "1" -and [int]$_.Groups[3].Value -gt 0})
        if($case.Name -notin @("gap","off") -and $valid.Count -eq 0){throw "No accepted history: $log"}
        if($case.Name -in @("camera","thin-half","instance")) {
            $changed=@($rows | Where-Object {[double]::Parse($_.Groups[5].Value,[Globalization.CultureInfo]::InvariantCulture) -gt .0001})
            if($changed.Count -eq 0){throw "History produced no AO change: $log"}
        }
        if($case.Name -eq "reset" -and $rows[2].Groups[1].Value -ne "0"){throw "Camera reset retained AO history: $log"}
        if($case.Name -eq "toggle" -and ($rows[2].Groups[2].Value -ne "0" -or $rows[4].Groups[1].Value -ne "0" -or $rows[5].Groups[1].Value -ne "1")){throw "Toggle failed: $log"}
        if($case.Name -in @("rebuild","scale") -and @($rows | Where-Object {$_.Groups[1].Value -eq "0"}).Count -lt 2){throw "Rebuild retained AO history: $log"}
        if($case.Name -eq "gap" -and @($rows | Where-Object {$_.Groups[1].Value -ne "0"}).Count -gt 0){throw "Nonconsecutive AO reused stale history: $log"}
        if($case.Name -eq "off" -and @($rows | Where-Object {$_.Groups[2].Value -ne "0" -or $_.Groups[3].Value -ne "0"}).Count -gt 0){throw "Disabled temporal path accumulated history: $log"}
        Write-Host "PASS temporal $($case.Name): GPU/CPU reference + lifecycle + validation"
    }
} finally {
    Pop-Location
    $env:VK_INSTANCE_LAYERS=$oldLayers; $env:VK_LAYER_VALIDATE_SYNC=$oldSync
}
