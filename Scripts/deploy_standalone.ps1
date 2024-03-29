# Updates Game with mod and UE4SS

# modify to target the your Guilty Gear installation root folder
$Game_Dir = "C:\Program Files (x86)\Steam\steamapps\common\GUILTY GEAR STRIVE"


$Project_Dir = (get-item $PSScriptRoot).Parent.FullName
$Package_Dir = "$Project_Dir\Packages\Standalone"
$Install_Dir = "$Game_Dir\RED\Binaries\Win64\"

Write-Host "Building Package"
& "$PSScriptRoot/package_standalone.ps1"
#pwsh -File "$PSScriptRoot/package_standalone.ps1"
#if($LastExitCode -ne 0) { Exit $LastExitCode }

Write-Host "Cleaning Install Dir"
$Cleanup_Targets = @("cache", "Mods", "UE4SS_Signatures", "UE4SS.log", "dwmapi.dll", "imgui.ini", "UE4SS.dll", "UE4SS.log", "UE4SS-settings.ini")
foreach ($Target in $Cleanup_Targets) {
  if(Test-Path -Path "$Install_Dir\$Target"){
    Remove-Item "$Install_Dir\$Target" -Recurse | Out-Null
  }
}

Write-Host "Installing Package"
Copy-Item -Path "$Package_Dir\*" -Destination "$Install_Dir" -Recurse | Out-Null
Write-Host "Finished Installing Package"