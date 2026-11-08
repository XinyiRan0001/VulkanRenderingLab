$ErrorActionPreference="Stop"
$root=Split-Path $PSScriptRoot -Parent
$exe=Join-Path $root "build/Release/VulkanRenderingLab.exe"
$directory=Join-Path $root "build/leg-ik-validation"
New-Item -ItemType Directory -Force -Path $directory | Out-Null
$oldLayers=$env:VK_INSTANCE_LAYERS;$oldSync=$env:VK_LAYER_VALIDATE_SYNC
Push-Location $root
try{
    $env:VK_INSTANCE_LAYERS="VK_LAYER_KHRONOS_validation";$env:VK_LAYER_VALIDATE_SYNC="1"
    foreach($case in @(
        @{Name="left-rest";Args=@("--animation-time","0")},
        @{Name="left-lift-off";Args=@("--animation-time","1")},
        @{Name="left-lift";Args=@("--animation-time","1.5")},
        @{Name="left-land";Args=@("--animation-time","2")},
        @{Name="left-support";Args=@("--animation-time","2.5")},
        @{Name="left-return";Args=@("--animation-time","3.5")},
        @{Name="right-lift";Args=@("--leg-ik","right","--animation-time","1.5")},
        @{Name="linear";Args=@("--skinning","linear","--skinning-normals","transformed","--animation-time","1.5")},
        @{Name="moving";Args=@()},
        @{Name="toggle";Frames=6;Args=@("--animation-time","2","--leg-ik-cycle-test")},
        @{Name="resize";Args=@("--animation-time","2","--resize-after","2","--resize-width","801","--resize-height","451")},
        @{Name="mirror";Args=@("--animation-time","1.5","--ray-query-scene","--verify-ray-query")}
    )){
        $frames=4;if($case.Frames){$frames=$case.Frames}
        $log=Join-Path $directory ($case.Name+".log");$extra=$case.Args
        & $exe --animated-scene assets/models/character/character.glb --leg-ik left --paused --ssao --verify-skinning --no-animation-panel --width 960 --height 640 --frames $frames --capture-frame $frames --capture "$directory/$($case.Name).png" @extra *> $log
        if($LASTEXITCODE -ne 0){throw "Leg IK failed: $log"}
        $content=Get-Content -Raw $log
        if($content -match 'VUID-|SYNC-HAZARD|Validation Error'){throw "Vulkan validation failed: $log"}
        $rows=[regex]::Matches($content,'\[Leg IK Verify\] side=(left|right) enabled=([01]) time=([\d.eE+-]+) target_error=([\d.eE+-]+) clamped=0 lengths=OK')
        if($rows.Count -lt $frames){throw "Missing IK verification: $log"}
        $contacts=[regex]::Matches($content,'\[Leg Contact Verify\] phase=(stance|swing) error=([\d.eE+-]+) clearance=([\d.eE+-]+)')
        if($contacts.Count -ne $rows.Count){throw "Missing sole contact verification: $log"}
        for($i=0;$i -lt $contacts.Count;++$i){
            if($rows[$i].Groups[2].Value -ne "1"){continue}
            $contact=$contacts[$i];$clearance=[double]$contact.Groups[3].Value
            if([double]$contact.Groups[2].Value -gt .0003 -or $clearance -lt -.0003){throw "Sole target or floor penetration: $log"}
            if($contact.Groups[1].Value -eq "stance" -and [Math]::Abs($clearance) -gt .0003){throw "Support foot left floor: $log"}
            if($case.Name -in @("left-lift","left-return","right-lift","linear","mirror") -and $clearance -lt .1){throw "Missing swing clearance: $log"}
        }
        foreach($row in $rows){
            if($row.Groups[2].Value -eq "1" -and [double]$row.Groups[4].Value -gt .0003){throw "Foot missed reachable target: $log"}
        }
        if($case.Name -eq "toggle"){
            $disabled=@($rows | Where-Object {$_.Groups[2].Value -eq "0"})
            if($disabled.Count -ne 2 -or [double]$disabled[0].Groups[4].Value -lt .1){throw "Missing IK/FK baseline toggle: $log"}
        }
        if($content -notmatch '\[Skinning Verify\]' -or $content -notmatch '\[Prepass Verify\]'){throw "Missing GPU deformation/depth checks: $log"}
        if($case.Name -eq "mirror" -and $content -notmatch '\[Ray Skinning Verify\]'){throw "Ray geometry did not follow IK: $log"}
        Write-Host "PASS leg IK $($case.Name): endpoint + sole contact + lengths + skinning + prepass + sync"
    }
}finally{
    Pop-Location
    $env:VK_INSTANCE_LAYERS=$oldLayers;$env:VK_LAYER_VALIDATE_SYNC=$oldSync
}
