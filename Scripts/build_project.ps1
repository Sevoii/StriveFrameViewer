# Builds the project

$Original_Dir = (Get-Location)
$Project_Dir = (get-item $PSScriptRoot).Parent.FullName
$Build_Dir = "$Project_Dir\build"

function Run { return (Start-Process $args[0] -ArgumentList $args[1] -PassThru -NoNewWindow -Wait).ExitCode }

function Build {
  if(-Not (Test-Path -Path "$Build_Dir")){ New-Item -ItemType Directory "$Build_Dir" | Out-Null }
  Set-Location -Path "$Build_Dir"

  Write-Host "`n##### Running CMake #####`n"
  $RCode = Run cmake.exe "-G `"Visual Studio 17 2022`" .."
  if($RCode -ne 0) {
    Write-Host "##### Failed CMake #####"
    return $RCode
  }

  Write-Host "`n##### Running MSBuild Mod #####`n"
  $RCode = Run MSBuild.exe "/p:Configuration=Game__Shipping__Win64 .\StriveFrameData\StriveFrameData.sln"
  if($RCode -ne 0) {
    Write-Host "`n##### Failed MSBuild Mod #####"
    return $RCode
  }

  Write-Host "`n##### Running MSBuild Proxy #####`n"
  $RCode = Run MSBuild.exe "/p:Configuration=Game__Shipping__Win64 .\RE-UE4SS\UE4SS\proxy_generator\proxy\proxy.sln"
  if($RCode -ne 0) {
    Write-Host "`n##### Failed MSBuild Proxy #####"
  }
  return $RCode
}

$Version = $PSVersionTable.PSVersion
if(($Version -lt 7) -or ($Version.Minor -lt 4) -or ($Version.Patch -lt 1)){
  Write-Host "Build Script requires powershell version 7.4.1 or newer.`nCurrent version: $Version"
  Exit 1
}

$Build_Code = Build
#Write-Host "Build Code: $Build_Code"
Set-Location -Path "$Original_Dir"
Exit $Build_Code