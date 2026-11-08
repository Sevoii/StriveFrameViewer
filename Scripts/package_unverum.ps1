# Creates Unverum package

$Project_Dir = (get-item $PSScriptRoot).Parent.FullName
$Package_Dir = "$Project_Dir\Packages\Unverum"
$Build_Dir = "$Project_Dir\build"
$Mod_DLL = "$Build_Dir\StriveFrameData\Game__Shipping__Win64\StriveFrameData.dll"

if(-Not (Test-Path -Path "$Mod_DLL")) {
  Write-Host "Failed to find Mod DLL"
  Exit 1
}

Write-Host "Cleaning Package Dir"
if(Test-Path -Path "$Package_Dir"){
  Remove-Item "$Package_Dir" -Recurse | Out-Null
}

Write-Host "Creating Package"
New-Item -ItemType Directory "$Package_Dir\ue4ss\StriveFrameViewer\dlls" | Out-Null
Copy-Item "$Mod_DLL" "$Package_Dir\ue4ss\StriveFrameViewer\dlls\main.dll" | Out-Null

Write-Host "Zipping Package"
$Zip_Path = "$Project_Dir\Packages\StriveFrameViewer-Unverum.zip"
Compress-Archive -Path "$Package_Dir\*" -DestinationPath "$Zip_Path" -Force
Write-Host "Created $Zip_Path"
