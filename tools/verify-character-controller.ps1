param([switch]$RoughOnly)
$ErrorActionPreference="Stop"
$root=Split-Path $PSScriptRoot -Parent
$exe=Join-Path $root "build/Release/VulkanRenderingLab.exe"
$directory=Join-Path $root "build/controller-validation"
New-Item -ItemType Directory -Force -Path $directory | Out-Null
$oldLayers=$env:VK_INSTANCE_LAYERS;$oldSync=$env:VK_LAYER_VALIDATE_SYNC
Push-Location $root
try{
    $env:VK_INSTANCE_LAYERS="VK_LAYER_KHRONOS_validation";$env:VK_LAYER_VALIDATE_SYNC="1"
    foreach($case in @(
        @{Name="idle";Frames=3;Args=@()},
        @{Name="movement-resize-stop";Frames=160;Args=@("--character-control-test","--resize-after","55","--resize-width","641","--resize-height","361")},
        @{Name="linear";Frames=34;Args=@("--character-control-test","--skinning","linear","--skinning-normals","transformed","--width","960","--height","640")},
        @{Name="mirror";Frames=48;Args=@("--character-control-test","--ray-query-scene","--verify-ray-query")},
        @{Name="rough-history";Frames=160;Args=@("--character-control-test","--rough-reflection-scene","--verify-ray-query","--reflection-temporal","on","--reflection-samples","1","--width","241","--height","161")}
    )){
        if($RoughOnly -and $case.Name -ne "rough-history"){continue}
        $log=Join-Path $directory ($case.Name+".log");$extra=$case.Args;$frames=$case.Frames
        & $exe --animated-scene assets/models/character/character.glb --leg-ik control --paused --ssao --verify-skinning --width 481 --height 321 --frames $frames --capture-frame $frames --capture "$directory/$($case.Name).png" @extra *> $log
        if($LASTEXITCODE -ne 0){throw "Controller failed: $log"}
        $content=Get-Content -Raw $log
        if($content -match 'VUID-|SYNC-HAZARD|Validation Error'){throw "Vulkan validation failed: $log"}
        $states=[regex]::Matches($content,'\[Character Control\] position=([\d.eE+-]+),([\d.eE+-]+) yaw=([\d.eE+-]+) speed=([\d.eE+-]+) blend=([\d.eE+-]+) active=(-?\d+) steps=(\d+) distance=([\d.eE+-]+)')
        $legs=[regex]::Matches($content,'\[Leg IK Verify\] side=(left|right) enabled=1 time=([\d.eE+-]+) target_error=([\d.eE+-]+) clamped=0 lengths=OK')
        $contacts=[regex]::Matches($content,'\[Leg Contact Verify\] phase=(stance|swing) error=([\d.eE+-]+) clearance=([\d.eE+-]+)')
        if($states.Count -ne $frames -or $legs.Count -ne 2*$frames -or $contacts.Count -ne $legs.Count){throw "Missing control state/IK verification: $log"}
        foreach($leg in $legs){if([double]$leg.Groups[3].Value -gt .0003){throw "Controlled target missed: $log"}}
        foreach($contact in $contacts){
            $height=[double]$contact.Groups[3].Value
            if([double]$contact.Groups[2].Value -gt .0003 -or $height -lt -.0003 -or ($contact.Groups[1].Value -eq "stance" -and [Math]::Abs($height) -gt .0003)){throw "Controlled sole contact lost: $log"}
        }
        $rolling=[regex]::Matches($content,'\[Foot Roll Verify\] left=(FLAT|TOE|AIR|HEEL) right=(FLAT|TOE|AIR|HEEL) pitch=([\d.eE+-]+),([\d.eE+-]+) clearance=([\d.eE+-]+),([\d.eE+-]+)')
        if($rolling.Count -ne $frames){throw "Missing foot roll verification: $log"}
        foreach($row in $rolling){
            if([double]$row.Groups[5].Value -lt -.0003 -or [double]$row.Groups[6].Value -lt -.0003){throw "Rigid shoe penetrated ground: $log"}
        }
        if($case.Name -in @("movement-resize-stop","rough-history")){
            foreach($phase in @("TOE","AIR","HEEL")){
                if(@($rolling | Where-Object {$_.Groups[1].Value -eq $phase -or $_.Groups[2].Value -eq $phase}).Count -lt 2){throw "Foot roll phase missing: $phase / $log"}
            }
        }
        if($case.Name -ne "idle"){
            for($i=20;$i -le 22;++$i){
                foreach($group in @(1,2,3,4,5,6,7,8)){
                    if($states[$i].Groups[$group].Value -ne $states[19].Groups[$group].Value){throw "Paused controller advanced: $log"}
                }
            }
            if([double]$states[$frames-1].Groups[8].Value -lt .2){throw "Controller did not move at 30 FPS: $log"}
        }
        if($case.Name -in @("movement-resize-stop","rough-history")){
            for($i=1;$i -lt $frames;++$i){
                $dx=[double]$states[$i].Groups[1].Value-[double]$states[$i-1].Groups[1].Value
                $dz=[double]$states[$i].Groups[2].Value-[double]$states[$i-1].Groups[2].Value
                if($dx*$dx+$dz*$dz -gt .0002){throw "Rebuild or movement teleported root: $log"}
            }
            $last=$states[$frames-1]
            if([Math]::Abs([double]$last.Groups[3].Value) -lt 1.5 -or [double]$last.Groups[4].Value -gt .001 -or [double]$last.Groups[5].Value -gt .01 -or $last.Groups[6].Value -ne "-1" -or [int]$last.Groups[7].Value -lt 4){throw "Turn/release/idle transition failed: $log"}
        }
        if($content -notmatch '\[Skinning Verify\]' -or $content -notmatch '\[Prepass Verify\]'){throw "Missing GPU skinning/depth verification: $log"}
        if($case.Name -in @("mirror","rough-history") -and $content -notmatch '\[Ray Skinning Verify\]'){throw "Mirror did not receive controlled vertices: $log"}
        if($case.Name -eq "rough-history"){
            $history=[regex]::Matches($content,'\[Reflection Temporal Verify\] enabled=1 previous=(\d+) phase=(\d+) reset=([01]) max_error=([\d.eE+-]+)')
            if($history.Count -ne $frames){throw "Missing reflection history check: $log"}
            foreach($index in @(20,21,22)){
                if([int]$history[$index].Groups[1].Value -lt 1){throw "Paused pose did not retain valid reflection history: $log"}
            }
            foreach($index in @(23,120,140)){
                if([int]$history[$index].Groups[1].Value -ne 0){throw "Moving or settling body reused reflection history: $log"}
            }
        }
        Write-Host "PASS controller $($case.Name): input + contact + body + skinning + sync"
    }
}finally{
    Pop-Location
    $env:VK_INSTANCE_LAYERS=$oldLayers;$env:VK_LAYER_VALIDATE_SYNC=$oldSync
}
