param([string]$Executable="build/Release/VulkanRenderingLab.exe")
$ErrorActionPreference="Stop"
$root=Split-Path $PSScriptRoot -Parent
$exe=(Resolve-Path (Join-Path $root $Executable)).Path
$directory=Join-Path $root "build/ray-query-validation"
New-Item -ItemType Directory -Force -Path $directory | Out-Null
$oldLayers=$env:VK_INSTANCE_LAYERS; $oldSync=$env:VK_LAYER_VALIDATE_SYNC
Push-Location $root
try {
    $env:VK_INSTANCE_LAYERS="VK_LAYER_KHRONOS_validation"; $env:VK_LAYER_VALIDATE_SYNC="1"
    foreach($case in @(
        @{Name="on";Args=@()},
        @{Name="off";Args=@("--ray-reflections","off")},
        @{Name="reflection";Args=@("--ray-view","1")},
        @{Name="hit-mask";Args=@("--ray-view","2")},
        @{Name="camera";Args=@("--camera-sweep","4","--yaw","20")},
        @{Name="resize";Args=@("--resize-after","2","--resize-width","801","--resize-height","451")},
        @{Name="resolution";Args=@("--ao-resolution-cycle-test","--frames","6")},
        @{Name="toggle";Args=@("--ray-query-cycle-test")}
    )){
        $log=Join-Path $directory ($case.Name+".log");$extra=$case.Args
        & $exe --ray-query-scene --verify-ray-query --paused --width 641 --height 361 --frames 4 @extra *> $log
        if($LASTEXITCODE -ne 0){throw "Ray Query failed: $log"}
        $content=Get-Content -Raw $log
        if($content -match 'VUID-|SYNC-HAZARD|Validation Error'){throw "Vulkan validation failed: $log"}
        $rows=[regex]::Matches($content,'\[Ray Query Verify\] rays=128 hits=(\d+) misses=(\d+) max_distance_error=([\d.eE+-]+) material=OK')
        if($rows.Count -lt 1){throw "No CPU/GPU ray verification: $log"}
        foreach($row in $rows){
            if([int]$row.Groups[1].Value -lt 16 -or [int]$row.Groups[2].Value -lt 16){throw "Missing ray coverage: $log"}
            $errorValue=[double]::Parse($row.Groups[3].Value,[Globalization.CultureInfo]::InvariantCulture)
            if($errorValue -gt .001){throw "Ray distance error: $log"}
        }
        if($content -notmatch '\[Prepass Verify\]'){throw "Missing main/prepass depth check: $log"}
        if($case.Name -in @("resize","resolution") -and $rows.Count -lt 2){throw "Ray structures were not verified after rebuild: $log"}
        Write-Host "PASS Ray Query $($case.Name): nearest hit + barycentrics + material + main depth + sync"
    }
} finally {
    Pop-Location
    $env:VK_INSTANCE_LAYERS=$oldLayers; $env:VK_LAYER_VALIDATE_SYNC=$oldSync
}
