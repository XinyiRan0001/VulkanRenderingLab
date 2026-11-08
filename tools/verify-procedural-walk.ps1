$ErrorActionPreference="Stop"
$root=Split-Path $PSScriptRoot -Parent
$exe=Join-Path $root "build/Release/VulkanRenderingLab.exe"
$directory=Join-Path $root "build/walk-validation"
New-Item -ItemType Directory -Force -Path $directory | Out-Null
$oldLayers=$env:VK_INSTANCE_LAYERS;$oldSync=$env:VK_LAYER_VALIDATE_SYNC
Push-Location $root
try{
    $env:VK_INSTANCE_LAYERS="VK_LAYER_KHRONOS_validation";$env:VK_LAYER_VALIDATE_SYNC="1"
    foreach($case in @(
        @{Name="start";Time="0";Args=@()},
        @{Name="left-swing";Time="3.2";Args=@()},
        @{Name="right-swing";Time="4.8";Args=@()},
        @{Name="end";Time="8";Args=@()},
        @{Name="linear";Time="3.2";Args=@("--skinning","linear","--skinning-normals","transformed")},
        @{Name="toggle";Time="3.2";Frames=6;Args=@("--leg-ik-cycle-test")},
        @{Name="resize";Time="4.8";Args=@("--resize-after","2","--resize-width","801","--resize-height","451")},
        @{Name="mirror";Time="4.8";Args=@("--ray-query-scene","--verify-ray-query")},
        @{Name="controls";Frames=9;Args=@("--walk-controls-test","--resize-after","3","--resize-width","801","--resize-height","451")},
        @{Name="moving";Frames=160;Args=@("--width","481","--height","321")}
    )){
        $frames=4;if($case.Frames){$frames=$case.Frames}
        $log=Join-Path $directory ($case.Name+".log");$extra=$case.Args
        if($case.ContainsKey("Time")){$extra+=@("--animation-time",$case.Time)}
        & $exe --animated-scene assets/models/character/character.glb --leg-ik walk --paused --ssao --verify-skinning --no-animation-panel --width 960 --height 640 --frames $frames --capture-frame $frames --capture "$directory/$($case.Name).png" @extra *> $log
        if($LASTEXITCODE -ne 0){throw "Walk failed: $log"}
        $content=Get-Content -Raw $log
        if($content -match 'VUID-|SYNC-HAZARD|Validation Error'){throw "Vulkan validation failed: $log"}
        $roots=[regex]::Matches($content,'\[Walk Verify\] root=([\d.eE+-]+),([\d.eE+-]+),([\d.eE+-]+) distance=([\d.eE+-]+) duration=8')
        $poses=[regex]::Matches($content,'\[Gait Verify\] enabled=([01]) left=([01]) right=([01]) pelvis=([\d.eE+-]+),([\d.eE+-]+),([\d.eE+-]+)')
        $legs=[regex]::Matches($content,'\[Leg IK Verify\] side=(left|right) enabled=([01]) time=([\d.eE+-]+) target_error=([\d.eE+-]+) clamped=([01]) lengths=OK')
        $contacts=[regex]::Matches($content,'\[Leg Contact Verify\] phase=(stance|swing) error=([\d.eE+-]+) clearance=([\d.eE+-]+)')
        if($roots.Count -ne $frames -or $poses.Count -ne $frames -or $legs.Count -ne 2*$frames -or $contacts.Count -ne $legs.Count){throw "Missing walk verification: $log"}
        foreach($pose in $poses){
            if($pose.Groups[2].Value -eq "0" -and $pose.Groups[3].Value -eq "0"){throw "Walk has no supporting foot: $log"}
        }
        for($i=0;$i -lt $legs.Count;++$i){
            if($legs[$i].Groups[2].Value -eq "0"){continue}
            if($legs[$i].Groups[5].Value -ne "0" -or [double]$legs[$i].Groups[4].Value -gt .0003 -or [double]$contacts[$i].Groups[2].Value -gt .0003){throw "Walking target missed: $log"}
            $height=[double]$contacts[$i].Groups[3].Value
            if($height -lt -.0003 -or ($contacts[$i].Groups[1].Value -eq "stance" -and [Math]::Abs($height) -gt .0003)){throw "Walking foot contact lost: $log"}
        }
        if($case.Name -eq "start" -and [double]$roots[0].Groups[4].Value -ne 0){throw "Walk starts displaced: $log"}
        if($case.Name -eq "end" -and ([double]$roots[0].Groups[4].Value -lt .9 -or $poses[0].Groups[2].Value -ne "1" -or $poses[0].Groups[3].Value -ne "1")){throw "Walk did not finish on both feet: $log"}
        if($case.Name -eq "moving"){
            if([double]$roots[$frames-1].Groups[4].Value -lt .18){throw "Root did not advance: $log"}
            for($i=1;$i -lt $frames;++$i){
                if([double]$roots[$i].Groups[4].Value -lt [double]$roots[$i-1].Groups[4].Value){throw "Root reversed: $log"}
            }
        }
        if($case.Name -eq "controls"){
            $states=[regex]::Matches($content,'\[Walk Controls\] frame=(\d+) time=([\d.eE+-]+) playing=([01]) loop=([01]) speed=([\d.eE+-]+) reset=([01])')
            if($states.Count -ne 9){throw "Missing walk controls: $log"}
            foreach($index in @(1,2,8)){
                if([double]$states[$index].Groups[2].Value -ne 8 -or $states[$index].Groups[3].Value -ne "0"){throw "Walk did not stop and hold: $log"}
            }
            if([double]$states[3].Groups[2].Value -gt .02 -or $states[3].Groups[3].Value -ne "1"){throw "Walk restart/rebuild failed: $log"}
            if([double]$states[4].Groups[5].Value -le 1 -or [Math]::Abs([double]$states[5].Groups[2].Value-3.2) -gt .00001){throw "Walk speed/seek failed: $log"}
            if([double]$states[6].Groups[2].Value -gt .03 -or $states[6].Groups[6].Value -ne "1"){throw "Walk loop retained stale motion history: $log"}
        }
        if($content -notmatch '\[Skinning Verify\]' -or $content -notmatch '\[Prepass Verify\]'){throw "Missing GPU deformation/depth checks: $log"}
        if($case.Name -eq "mirror" -and $content -notmatch '\[Ray Skinning Verify\]'){throw "Ray geometry did not follow walking: $log"}
        Write-Host "PASS walk $($case.Name): root + contact + skinning + prepass + sync"
    }
}finally{
    Pop-Location
    $env:VK_INSTANCE_LAYERS=$oldLayers;$env:VK_LAYER_VALIDATE_SYNC=$oldSync
}
