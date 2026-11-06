$ErrorActionPreference="Stop"
$root=Split-Path $PSScriptRoot -Parent
$exe=Join-Path $root "build/Release/VulkanRenderingLab.exe"
$directory=Join-Path $root "build/reflection-temporal-validation"
New-Item -ItemType Directory -Force -Path $directory | Out-Null
$oldLayers=$env:VK_INSTANCE_LAYERS; $oldSync=$env:VK_LAYER_VALIDATE_SYNC
Push-Location $root
try {
    $env:VK_INSTANCE_LAYERS="VK_LAYER_KHRONOS_validation"; $env:VK_LAYER_VALIDATE_SYNC="1"
    foreach($case in @(
        @{Name="single";Frames=1;Args=@()},
        @{Name="accumulated";Frames=32;Args=@()},
        @{Name="animated";Frames=4;Animate=$true;Args=@()},
        @{Name="light";Frames=4;Light=$true;Args=@()},
        @{Name="camera";Frames=8;Args=@("--camera-sweep","2","--camera-sweep-stop-at","3")},
        @{Name="cycle";Frames=8;Args=@("--reflection-temporal-cycle-test")},
        @{Name="resize";Frames=6;Args=@("--resize-after","2","--resize-width","801","--resize-height","451")},
        @{Name="resolution";Frames=6;Args=@("--ao-resolution-cycle-test")},
        @{Name="disabled";Frames=4;Args=@("--reflection-temporal","off")},
        @{Name="furnace";Frames=8;Args=@("--reflection-furnace")},
        @{Name="reference";Frames=2;Args=@("--reflection-reference")}
    )){
        $log=Join-Path $directory ($case.Name+".log");$extra=$case.Args
        $poseArgs=@("--animation-time",".52");$lightArgs=@("--paused")
        if($case.Animate){$poseArgs=@()}
        if($case.Light){$lightArgs=@()}
        & $exe --rough-reflection-scene --animated-scene assets/models/character/character.glb --animation Hello --reflection-temporal on --verify-ray-query --no-animation-panel --ray-reflections on --width 481 --height 321 --frames $case.Frames --capture-frame $case.Frames --capture "$directory/$($case.Name).png" --reflection-dump "$directory/$($case.Name)" @poseArgs @lightArgs @extra *> $log
        if($LASTEXITCODE -ne 0){throw "Reflection temporal failed: $log"}
        $content=Get-Content -Raw $log
        if($content -match 'VUID-|SYNC-HAZARD|Validation Error'){throw "Vulkan validation failed: $log"}
        $rows=[regex]::Matches($content,'\[Reflection Temporal Verify\] enabled=([01]) previous=(\d+) phase=(\d+) reset=([01]) max_error=([\d.eE+-]+)')
        if($rows.Count -ne $case.Frames){throw "Incomplete temporal equation checks: $log"}
        $previous=@($rows | ForEach-Object { [int]$_.Groups[2].Value })
        if($case.Name -in @("single","accumulated","furnace")){
            for($i=0;$i -lt $rows.Count;++$i){
                if($previous[$i] -ne $i -or [int]$rows[$i].Groups[3].Value -ne $i){throw "Static history did not accumulate: $log"}
            }
        }
        if($case.Name -in @("animated","light","disabled","reference") -and @($previous | Where-Object {$_ -ne 0}).Count){throw "History reused during motion or disabled mode: $log"}
        if($case.Name -eq "camera" -and (($previous -join ",") -ne "0,0,0,0,1,2,3,4")){throw "Camera reset/stop mismatch: $log"}
        if($case.Name -eq "cycle" -and (($previous -join ",") -ne "0,1,0,0,0,0,0,1")){throw "Toggle or parameter reset mismatch: $log"}
        if($case.Name -in @("resize","resolution") -and @($previous | Where-Object {$_ -eq 0}).Count -lt 2){throw "Rebuilt resources reused history: $log"}
        if($content -notmatch '\[Reflection Hit Guide Verify\]' -or $content -notmatch '\[Ray Skinning Verify\]'){throw "Missing geometry/guide verification: $log"}
        Write-Host "PASS reflection temporal $($case.Name): equation + reset lifecycle + finite + geometry + sync"
    }
    & python (Join-Path $root "tools/analyze-reflection-temporal.py") $directory
    if($LASTEXITCODE -ne 0){throw "Temporal reflection quality analysis failed"}
} finally {
    Pop-Location
    $env:VK_INSTANCE_LAYERS=$oldLayers; $env:VK_LAYER_VALIDATE_SYNC=$oldSync
}
