$ErrorActionPreference="Stop"
$root=Split-Path $PSScriptRoot -Parent
$exe=Join-Path $root "build/Release/VulkanRenderingLab.exe"
if(!(Test-Path -LiteralPath $exe)){throw "Build Release first: cmake --build build --config Release"}
Push-Location $root
try{
    & $exe --animated-scene assets/models/character/character.glb --leg-ik left --paused @args
    if($LASTEXITCODE -ne 0){throw "Renderer exited with code $LASTEXITCODE"}
}finally{Pop-Location}
