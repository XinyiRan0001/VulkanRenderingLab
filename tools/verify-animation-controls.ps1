param([string]$Executable="build/Release/VulkanRenderingLab.exe")
$ErrorActionPreference="Stop"
$root=Split-Path $PSScriptRoot -Parent
$exe=(Resolve-Path (Join-Path $root $Executable)).Path
$directory=Join-Path $root "build/animation-controls-validation"
New-Item -ItemType Directory -Force -Path $directory | Out-Null
$oldLayers=$env:VK_INSTANCE_LAYERS; $oldSync=$env:VK_LAYER_VALIDATE_SYNC
Push-Location $root
try {
    $env:VK_INSTANCE_LAYERS="VK_LAYER_KHRONOS_validation"; $env:VK_LAYER_VALIDATE_SYNC="1"
    foreach($case in @(
        @{Name="resize";Args=@("--resize-after","6","--resize-width","801","--resize-height","601")},
        @{Name="resolution";Args=@("--ao-resolution-cycle-test")}
    )){
        $log=Join-Path $directory ($case.Name+".log");$extra=$case.Args
        & $exe --animated-scene assets/models/character/character.glb --animation Hello --animation-controls-test --verify-skinning --ao-temporal on --ao-sampling varying --paused --width 641 --height 361 --frames 13 @extra *> $log
        if($LASTEXITCODE -ne 0){throw "Animation controls failed: $log"}
        $content=Get-Content -Raw $log
        if($content -match 'VUID-|SYNC-HAZARD|Validation Error'){throw "Vulkan validation failed: $log"}
        $rows=[regex]::Matches($content,'\[Animation Controls\] frame=(\d+) clip=(\d+) time=([\d.eE+-]+) speed=([\d.eE+-]+) playing=(\d) dual=(\d) surface=(\d) panel=(\d)')
        if($rows.Count -ne 13){throw "Missing controller states: $log"}
        $parse={param($v)[double]::Parse($v,[Globalization.CultureInfo]::InvariantCulture)}
        if($rows[1].Groups[5].Value -ne "0" -or [Math]::Abs((& $parse $rows[2].Groups[3].Value)-.52) -gt .00001){throw "Pause/seek failed: $log"}
        if($rows[4].Groups[6].Value -ne "0" -or $rows[4].Groups[7].Value -ne "0"){throw "Mode toggle failed: $log"}
        if($rows[5].Groups[2].Value -eq $rows[4].Groups[2].Value){throw "Clip switch failed: $log"}
        if([Math]::Abs((& $parse $rows[6].Groups[3].Value)-1.25/60) -gt .00001 -or $rows[6].Groups[5].Value -ne "1"){throw "Playback state lost across rebuild: $log"}
        if($rows[10].Groups[6].Value -ne "1" -or $rows[10].Groups[7].Value -ne "1" -or $rows[11].Groups[8].Value -ne "0" -or $rows[12].Groups[8].Value -ne "1"){throw "Mode/panel restore failed: $log"}
        if([regex]::Matches($content,'\[Skinning Verify\]').Count -lt 13){throw "Missing GPU deformation verification: $log"}
        if([regex]::Matches($content,'\[Motion Verify\] history=0').Count -lt 7){throw "Discontinuities retained motion history: $log"}
        Write-Host "PASS animation controls $($case.Name): playback + seek + modes + rebuild + history + sync"
    }
} finally {
    Pop-Location
    $env:VK_INSTANCE_LAYERS=$oldLayers; $env:VK_LAYER_VALIDATE_SYNC=$oldSync
}
