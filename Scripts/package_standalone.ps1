# Creates Standalone package

$Project_Dir = (get-item $PSScriptRoot).Parent.FullName
$Package_Dir = "$Project_Dir\Packages\Standalone"
$Build_Dir = "$Project_Dir\build"
$Mod_DLL = "$Build_Dir\StriveFrameData\Game__Shipping__Win64\StriveFrameData.dll"
$UE4SS_DLL = "$Build_Dir\Output\Game__Shipping__Win64\UE4SS\bin\UE4SS.dll"
$Proxy_DLL = "$Build_Dir\Output\Game__Shipping__Win64\proxy\bin\dwmapi.dll"

if(-Not (Test-Path -Path "$Mod_DLL")) {
  Write-Host "Failed to find Mod DLL"
  Exit 1
}
if(-Not (Test-Path -Path "$UE4SS_DLL")) {
  Write-Host "Failed to find UE4SS DLL"
  Exit 1
}
if(-Not (Test-Path -Path "$Proxy_DLL")) {
  Write-Host "Failed to find Proxy DLL"
  Exit 1
}

Write-Host "Cleaning Package Dir"
if(Test-Path -Path "$Package_Dir"){
  Remove-Item "$Package_Dir" -Recurse | Out-Null
}

Write-Host "Creating Package"
New-Item -ItemType Directory "$Package_Dir\Mods\StriveFrameViewer\dlls" | Out-Null
#New-Item -ItemType Directory "$Package_Dir\UE4SS_Signatures" | Out-Null
Copy-Item "$Mod_DLL" "$Package_Dir\Mods\StriveFrameViewer\dlls\main.dll" | Out-Null
Copy-Item "$UE4SS_DLL" "$Package_Dir\UE4SS.dll" | Out-Null
Copy-Item "$Proxy_DLL" "$Package_Dir\dwmapi.dll" | Out-Null
Copy-Item "$Project_Dir\prereqs\mods.txt" "$Package_Dir\Mods\mods.txt" | Out-Null
Copy-Item "$Project_Dir\prereqs\UE4SS-settings.ini" "$Package_Dir\UE4SS-settings.ini" | Out-Null
#Copy-Item "$Project_Dir\prereqs\FText_Constructor.lua" "$Package_Dir\UE4SS_Signatures\FText_Constructor.lua" | Out-Null