$ErrorActionPreference="Stop"
$root=Split-Path $PSScriptRoot -Parent
$exe=Join-Path $root "build/Release/VulkanRenderingLab.exe"
$directory=Join-Path $root "build/gait-validation"
New-Item -ItemType Directory -Force -Path $directory | Out-Null
$oldLayers=$env:VK_INSTANCE_LAYERS;$oldSync=$env:VK_LAYER_VALIDATE_SYNC
Push-Location $root
try{
    $env:VK_INSTANCE_LAYERS="VK_LAYER_KHRONOS_validation";$env:VK_LAYER_VALIDATE_SYNC="1"
    foreach($case in @(
        @{Name="double-support";Time="0";Left="1";Right="1";Args=@()},
        @{Name="left-swing";Time="1.1";Left="0";Right="1";Args=@()},
        @{Name="right-swing";Time="3.1";Left="1";Right="0";Args=@()},
        @{Name="linear";Time="1.1";Args=@("--skinning","linear","--skinning-normals","transformed")},
        @{Name="toggle";Time="1.1";Frames=6;Args=@("--leg-ik-cycle-test")},
        @{Name="resize";Time="3.1";Args=@("--resize-after","2","--resize-width","801","--resize-height","451")},
        @{Name="mirror";Time="3.1";Args=@("--ray-query-scene","--verify-ray-query")},
        @{Name="moving";Frames=80;Args=@("--width","481","--height","321")}
    )){
        $frames=4;if($case.Frames){$frames=$case.Frames}
        $log=Join-Path $directory ($case.Name+".log");$extra=$case.Args
        if($case.ContainsKey("Time")){$extra+=@("--animation-time",$case.Time)}
        & $exe --animated-scene assets/models/character/character.glb --leg-ik both --paused --ssao --verify-skinning --no-animation-panel --width 960 --height 640 --frames $frames --capture-frame $frames --capture "$directory/$($case.Name).png" @extra *> $log
        if($LASTEXITCODE -ne 0){throw "Gait failed: $log"}
        $content=Get-Content -Raw $log
        if($content -match 'VUID-|SYNC-HAZARD|Validation Error'){throw "Vulkan validation failed: $log"}
        $poses=[regex]::Matches($content,'\[Gait Verify\] enabled=([01]) left=([01]) right=([01]) pelvis=([\d.eE+-]+),([\d.eE+-]+),([\d.eE+-]+)')
        $legs=[regex]::Matches($content,'\[Leg IK Verify\] side=(left|right) enabled=([01]) time=([\d.eE+-]+) target_error=([\d.eE+-]+) clamped=0 lengths=OK')
        $contacts=[regex]::Matches($content,'\[Leg Contact Verify\] phase=(stance|swing) error=([\d.eE+-]+) clearance=([\d.eE+-]+)')
        if($poses.Count -ne $frames -or $legs.Count -ne 2*$frames -or $contacts.Count -ne $legs.Count){throw "Missing gait verification: $log"}
        foreach($pose in $poses){
            if($pose.Groups[2].Value -eq "0" -and $pose.Groups[3].Value -eq "0"){throw "Both feet airborne: $log"}
            if($case.ContainsKey("Left") -and ($pose.Groups[2].Value -ne $case.Left -or $pose.Groups[3].Value -ne $case.Right)){throw "Wrong alternating gait phase: $log"}
            $x=[double]$pose.Groups[4].Value;$y=[double]$pose.Groups[5].Value;$z=[double]$pose.Groups[6].Value
            if($x*$x+$y*$y+$z*$z -gt .01){throw "Unbounded pelvis motion: $log"}
            if($pose.Groups[1].Value -eq "0" -and ($x -ne 0 -or $y -ne 0 -or $z -ne 0)){throw "Disabled pelvis still moves: $log"}
        }
        for($i=0;$i -lt $legs.Count;++$i){
            if($legs[$i].Groups[1].Value -ne $(if($i%2 -eq 0){"left"}else{"right"})){throw "Missing leg ordering: $log"}
            if($legs[$i].Groups[2].Value -eq "0"){continue}
            if([double]$legs[$i].Groups[4].Value -gt .0003 -or [double]$contacts[$i].Groups[2].Value -gt .0003){throw "Bilateral target missed: $log"}
            $height=[double]$contacts[$i].Groups[3].Value
            if($height -lt -.0003 -or ($contacts[$i].Groups[1].Value -eq "stance" -and [Math]::Abs($height) -gt .0003)){throw "Sole contact lost: $log"}
        }
        if($case.Name -eq "toggle" -and @($poses | Where-Object {$_.Groups[1].Value -eq "0"}).Count -ne 2){throw "Missing IK comparison toggle: $log"}
        if($case.Name -eq "moving"){
            if(@($poses | Where-Object {$_.Groups[2].Value -eq "0"}).Count -lt 20){throw "Playback never reached swing: $log"}
        }
        if($content -notmatch '\[Skinning Verify\]' -or $content -notmatch '\[Prepass Verify\]'){throw "Missing GPU deformation/depth checks: $log"}
        if($case.Name -eq "mirror" -and $content -notmatch '\[Ray Skinning Verify\]'){throw "Ray geometry did not follow gait: $log"}
        Write-Host "PASS gait $($case.Name): bilateral contact + pelvis + skinning + prepass + sync"
    }
}finally{
    Pop-Location
    $env:VK_INSTANCE_LAYERS=$oldLayers;$env:VK_LAYER_VALIDATE_SYNC=$oldSync
}
