param([string]$Executable="build/Release/VulkanRenderingLab.exe")
$ErrorActionPreference="Stop"
$root=Split-Path $PSScriptRoot -Parent
$exe=(Resolve-Path (Join-Path $root $Executable)).Path
$directory=Join-Path $root "build/ao-motion-validation"
New-Item -ItemType Directory -Force -Path $directory | Out-Null
$oldLayers=$env:VK_INSTANCE_LAYERS; $oldSync=$env:VK_LAYER_VALIDATE_SYNC
Push-Location $root
try {
    $env:VK_INSTANCE_LAYERS="VK_LAYER_KHRONOS_validation"; $env:VK_LAYER_VALIDATE_SYNC="1"
    $cases=@(
        @{Name="fixed";Args=@("--ao-box-step","--frames","32","--ao-response","fixed")},
        @{Name="adaptive";Args=@("--ao-box-step","--frames","32","--ao-response","adaptive")},
        @{Name="status";Args=@("--ao-box-step","--frames","26","--ao-history-view","status","--ao-response","adaptive")},
        @{Name="weight";Args=@("--ao-box-step","--frames","26","--ao-history-view","weight")},
        @{Name="continuous";Args=@("--frames","66","--ao-sampling","varying","--ao-samples","8","--ao-response","adaptive")},
        @{Name="resize";Args=@("--ao-box-step","--frames","28","--ao-resolution","half","--resize-after","24","--resize-width","801","--resize-height","451","--ao-history-view","status","--ao-response","adaptive")}
    )
    $results=@{}
    foreach($case in $cases) {
        $log=Join-Path $directory ($case.Name+".log");$capture=Join-Path $directory ($case.Name+".png");$extra=$case.Args
        & $exe --ao-motion-scene --verify-prepass --paused --yaw 25 --pitch 28 --width 641 --height 361 --capture $capture --capture-frame 25 @extra *> $log
        if($LASTEXITCODE -ne 0){throw "AO motion test failed: $log"}
        $content=Get-Content -Raw $log
        if($content -match 'VUID-|SYNC-HAZARD|Validation Error'){throw "Vulkan validation failed: $log"}
        $rows=[regex]::Matches($content,'\[AO Receiver Verify\] stable=(\d+) newly_revealed=(\d+) max_reveal_weight=([0-9.eE+-]+) mean_abs_delta=([0-9.eE+-]+)')
        if($rows.Count -lt 26){throw "Missing receiver readback: $log"}
        $parse={param($v)[double]::Parse($v,[Globalization.CultureInfo]::InvariantCulture)}
        if($case.Name -ne "resize") {
            $revealed=@($rows | Where-Object {[int]$_.Groups[2].Value -gt 0})
            if($revealed.Count -eq 0){throw "No new receiver pixels exposed: $log"}
            foreach($row in $revealed) {
                if((& $parse $row.Groups[3].Value) -gt .01){throw "Newly revealed receiver retained substantial box history: $log"}
            }
        }
        if($case.Name -in @("fixed","adaptive")) {
            $values=@();$counts=@()
            for($i=24;$i -lt 32;++$i){$values+=(& $parse $rows[$i].Groups[4].Value);$counts+=[int]$rows[$i].Groups[1].Value}
            $results[$case.Name]=@{Mean=($values | Measure-Object -Average).Average;Counts=$counts}
        }
        Write-Host "PASS AO motion $($case.Name): fixed receiver + GPU reference + sync"
    }
    if(($results.fixed.Counts -join ',') -ne ($results.adaptive.Counts -join ',')){throw "Response comparison used different receiver masks"}
    if($results.adaptive.Mean -ge $results.fixed.Mean*.9){throw "Adaptive response did not reduce fixture lag sufficiently"}
    Write-Host "Step frames 24..31, mean receiver |temporal-current spatial|: fixed=$($results.fixed.Mean), adaptive=$($results.adaptive.Mean)"
    Write-Host "This is response lag relative to current spatial AO, not ground-truth accuracy."
} finally {
    Pop-Location
    $env:VK_INSTANCE_LAYERS=$oldLayers; $env:VK_LAYER_VALIDATE_SYNC=$oldSync
}
