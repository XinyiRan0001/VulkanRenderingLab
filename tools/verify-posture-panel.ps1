$ErrorActionPreference="Stop"
$root=Split-Path $PSScriptRoot -Parent
$exe=Join-Path $root "build/Release/VulkanRenderingLab.exe"
$directory=Join-Path $root "build/pose-panel-validation"
New-Item -ItemType Directory -Force -Path $directory | Out-Null
$oldLayers=$env:VK_INSTANCE_LAYERS;$oldSync=$env:VK_LAYER_VALIDATE_SYNC
Push-Location $root
try{
    $env:VK_INSTANCE_LAYERS="VK_LAYER_KHRONOS_validation";$env:VK_LAYER_VALIDATE_SYNC="1"
    foreach($case in @(
        @{Name="edit-resize";Frames=64;Capture=46;Args=@("--resize-after","24","--resize-width","801","--resize-height","601")},
        @{Name="mirror";Frames=48;Capture=46;Args=@("--ray-query-scene","--verify-ray-query")},
        @{Name="rough-history";Frames=48;Capture=46;Args=@("--rough-reflection-scene","--verify-ray-query","--reflection-temporal","on","--reflection-samples","1","--width","481","--height","321")}
    )){
        $log=Join-Path $directory ($case.Name+".log");$extra=$case.Args
        & $exe --animated-scene assets/models/character/character.glb --leg-ik control --pose-panel-test --verify-skinning --ssao --paused --width 960 --height 640 --frames $case.Frames --capture-frame $case.Capture --capture "$directory/$($case.Name).png" @extra *> $log
        if($LASTEXITCODE -ne 0){throw "Posture panel failed: $log"}
        $content=Get-Content -Raw $log
        if($content -match 'VUID-|SYNC-HAZARD|Validation Error|\[Posture\]'){throw "Validation/config error: $log"}
        $rows=[regex]::Matches($content,'\[Posture Verify\] custom=([01]) auto=([01]) paused=([01]) visible=([01]) error=([\d.eE+-]+) reach=([\d.eE+-]+),([\d.eE+-]+) correction=([\d.eE+-]+) clamped=([01])((?: [\d.eE+-]+){12})')
        $states=[regex]::Matches($content,'\[Character Control\] position=([\d.eE+-]+),([\d.eE+-]+) yaw=([\d.eE+-]+) speed=([\d.eE+-]+) blend=([\d.eE+-]+) active=(-?\d+) steps=(\d+) distance=([\d.eE+-]+)')
        if($rows.Count -ne $case.Frames -or $states.Count -ne $case.Frames){throw "Incomplete posture/controller checks: $log"}
        foreach($row in $rows){
            if($row.Groups[2].Value -eq "1" -and ([double]$row.Groups[5].Value -gt .0003 -or $row.Groups[9].Value -ne "0")){throw "Edited pose lost automatic contact: $log"}
        }
        for($i=18;$i -le 34;++$i){
            if($rows[$i].Groups[3].Value -ne "1"){throw "Posture editing did not remain paused: $log"}
            foreach($group in @(1,2,3,4,5,6,7,8)){
                if($states[$i].Groups[$group].Value -ne $states[17].Groups[$group].Value){throw "Editing advanced controller state: $log"}
            }
        }
        if($rows[26].Groups[1].Value -ne "0" -or $rows[27].Groups[1].Value -ne "1"){throw "Baseline/custom comparison failed: $log"}
        if($rows[28].Groups[2].Value -ne "0" -or $rows[29].Groups[2].Value -ne "1"){throw "Manual/automatic reach comparison failed: $log"}
        if($rows[33].Groups[4].Value -ne "0" -or $rows[34].Groups[4].Value -ne "1"){throw "Panel visibility failed: $log"}
        if($rows[35].Groups[3].Value -ne "0"){throw "Panel play action failed: $log"}
        $saved=@($rows[25].Groups[10].Value.Trim() -split ' ' | ForEach-Object {[double]$_})
        $restored=@($rows[32].Groups[10].Value.Trim() -split ' ' | ForEach-Object {[double]$_})
        $expected=@(0,.045,14.1,.95,.7,36,22.75,12,18,.06,0,0)
        for($i=0;$i -lt 12;++$i){
            if([Math]::Abs($saved[$i]-$expected[$i]) -gt .0001 -or [Math]::Abs($restored[$i]-$saved[$i]) -gt .00001){throw "Slider/config roundtrip failed: $log"}
        }
        $rollTuned=$rows[38].Groups[10].Value.Trim() -split ' '
        if([Math]::Abs([double]$rollTuned[7]-20) -gt .0001 -or [Math]::Abs([double]$rollTuned[8]-28) -gt .0001 -or [Math]::Abs([double]$rollTuned[9]-.07) -gt .00001){throw "Foot roll sliders failed: $log"}
        $defaults=$rows[31].Groups[10].Value.Trim() -split ' '
        if([Math]::Abs([double]$defaults[0]-0) -gt .00001){throw "Defaults action failed: $log"}
        $rest=[regex]::Matches($content,'\[Rest Pose Verify\] width=([\d.eE+-]+) fore_aft=([\d.eE+-]+) tilt=([\d.eE+-]+) knees=([\d.eE+-]+),([\d.eE+-]+)')
        if($rest.Count -ne $case.Frames){throw "Missing standing correction metrics: $log"}
        if($rows[39].Groups[3].Value -ne "1" -or $states[39].Groups[6].Value -ne "-1" -or [double]$states[39].Groups[4].Value -ne 0){throw "Stand/apply width failed: $log"}
        if($states[39].Groups[1].Value -ne $states[38].Groups[1].Value -or $states[39].Groups[2].Value -ne $states[38].Groups[2].Value){throw "Standing preview moved root: $log"}
        if([double]$rest[40].Groups[2].Value -gt -.04 -or [Math]::Abs([double]$rest[41].Groups[3].Value+6) -gt .001){throw "Idle backward hip/tilt correction failed: $log"}
        if([double]$rest[42].Groups[2].Value -lt .04 -or [Math]::Abs([double]$rest[43].Groups[3].Value-8) -gt .001){throw "Idle forward hip/tilt correction failed: $log"}
        if([double]$rest[45].Groups[1].Value -gt .34 -or [Math]::Abs([double]$rest[45].Groups[2].Value) -gt .001 -or [Math]::Abs([double]$rest[45].Groups[3].Value) -gt .001){throw "Corrected narrow/upright defaults failed: $log"}
        if([double]$rest[45].Groups[4].Value -gt 10 -or [double]$rest[45].Groups[5].Value -gt 10){throw "Standing knees still excessively bent: $log"}
        if($content -notmatch '\[Skinning Verify\]' -or $content -notmatch '\[Prepass Verify\]'){throw "Missing edited geometry checks: $log"}
        if($case.Name -ne "edit-resize" -and $content -notmatch '\[Ray Skinning Verify\]'){throw "Mirror geometry did not receive edited pose: $log"}
        if($case.Name -eq "rough-history"){
            $history=[regex]::Matches($content,'\[Reflection Temporal Verify\] enabled=1 previous=(\d+) phase=(\d+) reset=([01]) max_error=([\d.eE+-]+)')
            if($history.Count -ne $case.Frames){throw "Missing posture reflection history checks: $log"}
            foreach($index in @(21,22,24,25,26,27,31,32)){
                if([int]$history[$index].Groups[1].Value -ne 0){throw "Edited pose reused old reflection history: $log"}
            }
        }
        Write-Host "PASS posture $($case.Name): paused edits + contact + compare + save/load + graphics + sync"
    }
}finally{
    Pop-Location
    $env:VK_INSTANCE_LAYERS=$oldLayers;$env:VK_LAYER_VALIDATE_SYNC=$oldSync
}
