$ErrorActionPreference="Stop"
$root=Split-Path $PSScriptRoot -Parent
$exe=Join-Path $root "build/Release/VulkanRenderingLab.exe"
$directory=Join-Path $root "build/reflection-spatial-validation"
New-Item -ItemType Directory -Force -Path $directory | Out-Null
$oldLayers=$env:VK_INSTANCE_LAYERS; $oldSync=$env:VK_LAYER_VALIDATE_SYNC
Push-Location $root
try {
    $env:VK_INSTANCE_LAYERS="VK_LAYER_KHRONOS_validation"; $env:VK_LAYER_VALIDATE_SYNC="1"
    foreach($case in @(
        @{Name="raw";Args=@("--animation-time",".52","--reflection-denoise","off","--width","960","--height","640")},
        @{Name="filtered";Args=@("--animation-time",".52","--width","960","--height","640")},
        @{Name="receiver-only";Args=@("--animation-time",".52","--reflection-hit-guidance","off","--width","960","--height","640")},
        @{Name="moving";Args=@()},
        @{Name="resize";Args=@("--resize-after","2","--resize-width","801","--resize-height","451")},
        @{Name="resolution";Args=@("--ao-resolution-cycle-test","--frames","6")},
        @{Name="toggle";Args=@("--ray-query-cycle-test")}
    )){
        $log=Join-Path $directory ($case.Name+".log");$extra=$case.Args
        & $exe --rough-reflection-scene --animated-scene assets/models/character/character.glb --animation Hello --paused --verify-ray-query --no-animation-panel --ray-reflections on --width 641 --height 361 --frames 4 --capture "$directory/$($case.Name).png" @extra *> $log
        if($LASTEXITCODE -ne 0){throw "Reflection spatial failed: $log"}
        $content=Get-Content -Raw $log
        if($content -match 'VUID-|SYNC-HAZARD|Validation Error'){throw "Vulkan validation failed: $log"}
        if($content -notmatch '\[Reflection Hit Guide Verify\] finite=OK normals=OK geometry=\d+ sky=\d+ enabled=[01]'){throw "Missing hit guide verification: $log"}
        $rows=[regex]::Matches($content,'\[Reflection Spatial Verify\] finite=OK tags=OK invalid=(\d+) sharp=(\d+) sharp_error=([\d.eE+-]+) rough=(\d+) pairs=(\d+) variation_raw=([\d.eE+-]+) variation_filtered=([\d.eE+-]+)')
        if($rows.Count -lt 3){throw "Missing spatial verification: $log"}
        foreach($row in $rows){
            if([double]$row.Groups[3].Value -ne 0){throw "Sharp receiver modified: $log"}
            if([double]$row.Groups[6].Value -gt .0001 -and [double]$row.Groups[7].Value -ge [double]$row.Groups[6].Value){throw "No reduction in local rough variation: $log"}
        }
        if($content -notmatch '\[Ray Skinning Verify\]' -or $content -notmatch '\[Ray Query Verify\]'){throw "Missing ray/animation checks: $log"}
        Write-Host "PASS spatial $($case.Name): finite + exact sharp copy + receiver tags + reduced local variation + animation + sync"
    }
} finally {
    Pop-Location
    $env:VK_INSTANCE_LAYERS=$oldLayers; $env:VK_LAYER_VALIDATE_SYNC=$oldSync
}
