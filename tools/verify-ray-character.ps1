param([string]$Executable="build/Release/VulkanRenderingLab.exe")
$ErrorActionPreference="Stop"
$root=Split-Path $PSScriptRoot -Parent
$exe=(Resolve-Path (Join-Path $root $Executable)).Path
$directory=Join-Path $root "build/ray-character-validation"
New-Item -ItemType Directory -Force -Path $directory | Out-Null
$oldLayers=$env:VK_INSTANCE_LAYERS; $oldSync=$env:VK_LAYER_VALIDATE_SYNC
Push-Location $root
try {
    $env:VK_INSTANCE_LAYERS="VK_LAYER_KHRONOS_validation"; $env:VK_LAYER_VALIDATE_SYNC="1"
    foreach($case in @(
        @{Name="hello";Args=@()},
        @{Name="frozen";Args=@("--animation-time",".52")},
        @{Name="guide";Args=@("--animation","Guide","--animation-time","1")},
        @{Name="linear";Args=@("--skinning","linear","--skinning-normals","transformed","--animation-time",".52")},
        @{Name="controls";Args=@("--animation-controls-test","--frames","13")},
        @{Name="resize";Args=@("--resize-after","2","--resize-width","801","--resize-height","451")},
        @{Name="resolution";Args=@("--ao-resolution-cycle-test","--frames","6")},
        @{Name="toggle";Args=@("--ray-query-cycle-test")}
    )){
        $log=Join-Path $directory ($case.Name+".log");$extra=$case.Args
        & $exe --ray-query-scene --animated-scene assets/models/character/character.glb --animation Hello --verify-ray-query --ao-temporal on --ao-sampling varying --paused --width 641 --height 361 --frames 4 @extra *> $log
        if($LASTEXITCODE -ne 0){throw "Ray character failed: $log"}
        $content=Get-Content -Raw $log
        if($content -match 'VUID-|SYNC-HAZARD|Validation Error'){throw "Vulkan validation failed: $log"}
        $rows=[regex]::Matches($content,'\[Ray Skinning Verify\] triangles=(\d+) raster_position_error=([\d.eE+-]+) normals=OK refit=BLAS\+TLAS')
        if($rows.Count -lt 3){throw "Missing per-frame AS/raster agreement: $log"}
        if([regex]::Matches($content,'\[Ray Query Verify\]').Count -lt 4 -or $content -notmatch '\[Skinning Verify\]' -or $content -notmatch '\[Prepass Verify\]'){throw "Missing CPU/GPU deformation + ray + prepass checks: $log"}
        if($case.Name -in @("resize","resolution") -and [regex]::Matches($content,'\[Ray Query Build\] updatable').Count -lt 2){throw "Dynamic AS not rebuilt: $log"}
        Write-Host "PASS ray character $($case.Name): raster/AS vertices + normals + alpha-aware rays + motion + sync"
    }
} finally {
    Pop-Location
    $env:VK_INSTANCE_LAYERS=$oldLayers; $env:VK_LAYER_VALIDATE_SYNC=$oldSync
}
