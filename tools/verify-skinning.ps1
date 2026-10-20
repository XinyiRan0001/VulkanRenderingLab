param([string]$Executable="build/Release/VulkanRenderingLab.exe",[string]$Asset="assets/models/character/character.glb")
$ErrorActionPreference="Stop"
$root=Split-Path $PSScriptRoot -Parent
$exe=(Resolve-Path (Join-Path $root $Executable)).Path
$directory=Join-Path $root "build/skinning-validation"
New-Item -ItemType Directory -Force -Path $directory | Out-Null
# Exercise the automatic matrix fallback using the original analytic fixture.
$scaled = Get-Content -Raw (Join-Path $root "assets/models/animation-validation/rig.gltf") | ConvertFrom-Json
$scaled.nodes[0] | Add-Member -NotePropertyName scale -NotePropertyValue @(1.2,1,1)
$scaled | ConvertTo-Json -Depth 100 | Set-Content -Encoding UTF8 (Join-Path $directory "scaled-rig.gltf")
$oldLayers=$env:VK_INSTANCE_LAYERS; $oldSync=$env:VK_LAYER_VALIDATE_SYNC
Push-Location $root
try {
    $env:VK_INSTANCE_LAYERS="VK_LAYER_KHRONOS_validation"; $env:VK_LAYER_VALIDATE_SYNC="1"
    $cases=@(
        @{Name="analytic-rig";Asset="assets/models/animation-validation/rig.gltf";Args=@("--animation","joint-and-morph","--animation-time",".5","--yaw","0")},
        @{Name="analytic-authored";Asset="assets/models/animation-validation/rig.gltf";Args=@("--animation","joint-and-morph","--animation-time",".5","--yaw","0","--skinning-normals","transformed")},
        @{Name="scaled-rig";Asset="build/skinning-validation/scaled-rig.gltf";Args=@("--animation","joint-and-morph","--animation-time",".5","--yaw","0")},
        @{Name="idle-frozen";Args=@("--animation","Idle","--animation-time","0")},
        @{Name="hello";Args=@("--animation","Hello","--ao-temporal","on","--ao-sampling","varying")},
        @{Name="entry";Args=@("--animation","Entry")},
        @{Name="angry";Args=@("--animation","Angry")},
        @{Name="guide";Args=@("--animation","Guide")},
        @{Name="thinking";Args=@("--animation","Thinking")},
        @{Name="indroduction";Args=@("--animation","Indroduction")},
        @{Name="hello-pose";Args=@("--animation","Hello","--animation-time",".52")},
        @{Name="hello-late";Args=@("--animation","Hello","--animation-time","3")},
        @{Name="guide-bent";Args=@("--animation","Guide","--animation-time","1")},
        @{Name="thinking-bent";Args=@("--animation","Thinking","--animation-time","1")},
        @{Name="hello-linear";Args=@("--animation","Hello","--skinning","linear")},
        @{Name="hello-authored";Args=@("--animation","Hello","--skinning-normals","transformed")},
        @{Name="entry-middle";Args=@("--animation","Entry","--animation-time","2.5")},
        @{Name="reset";Args=@("--animation","Hello","--motion-reset-at","2")},
        @{Name="scale";Args=@("--animation","Hello","--ao-resolution-cycle-test","--frames","6")},
        @{Name="back-camera";Args=@("--animation","Hello","--yaw","270","--camera-sweep","2")},
        @{Name="resize";Args=@("--animation","Hello","--ao-resolution","half","--resize-after","2","--resize-width","801","--resize-height","451")},
        @{Name="toggle";Args=@("--animation","Hello","--ao-cycle-test","--frames","8")},
        @{Name="motion-only";Args=@("--animation","Hello","--motion-view")}
    )
    foreach($case in $cases){
        $log=Join-Path $directory ($case.Name+".log");$extra=$case.Args
        $assetPath=if($case.Asset){$case.Asset}else{$Asset}
        & $exe --animated-scene $assetPath --verify-skinning --paused --width 641 --height 361 --frames 4 @extra *> $log
        if($LASTEXITCODE -ne 0){throw "Skinning test failed: $log"}
        $content=Get-Content -Raw $log
        if($content -match 'VUID-|SYNC-HAZARD|Validation Error'){throw "Vulkan validation failed: $log"}
        $skin=[regex]::Matches($content,'\[Skinning Verify\] vertices=(\d+) position=([\d.eE+-]+) normal=([\d.eE+-]+) previous=([\d.eE+-]+)')
        if($skin.Count -lt 3){throw "Missing per-vertex skinning verification: $log"}
        if($case.Name -ne "toggle"){
            $motion=[regex]::Matches($content,'\[Deformed Motion Verify\] pixels=(\d+) max_error_px=([\d.eE+-]+)')
            if(@($motion | Where-Object {[int]$_.Groups[1].Value -gt 0}).Count -eq 0){throw "No CPU triangle motion reference pixels: $log"}
        }
        $motionRows=[regex]::Matches($content,'\[Motion Verify\] history=(\d+).*?max_px=([\d.eE+-]+)')
        $parse={param($v)[double]::Parse($v,[Globalization.CultureInfo]::InvariantCulture)}
        if($case.Name -eq "idle-frozen" -and (& $parse $motionRows[$motionRows.Count-1].Groups[2].Value) -gt .02){throw "Frozen rig produced motion: $log"}
        if($case.Name -in @("hello","motion-only") -and (& $parse $motionRows[$motionRows.Count-1].Groups[2].Value) -lt .1){throw "Deformation failed to produce motion: $log"}
        if($case.Name -eq "reset" -and $motionRows[2].Groups[1].Value -ne "0"){throw "Deformed history did not reset: $log"}
        Write-Host "PASS skinning $($case.Name): current/previous vertices, normals, depth + sync"
    }
} finally {
    Pop-Location
    $env:VK_INSTANCE_LAYERS=$oldLayers; $env:VK_LAYER_VALIDATE_SYNC=$oldSync
}
