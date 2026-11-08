# Builds the project

$Original_Dir = (Get-Location)
$Project_Dir = (get-item $PSScriptRoot).Parent.FullName
$Build_Dir = "$Project_Dir\build"

$MSBuild = "C:\Program Files\Microsoft Visual Studio\2022\Community\MSBuild\Current\Bin\MSBuild.exe"

# Runs a native tool and returns its exit code.
# Uses the call operator instead of Start-Process -Wait: -Wait also waits for every child process the tool
# spawned, and MSBuild worker nodes / mspdbsrv.exe outlive the build, which made the script hang forever.
# Output goes to the host so it doesn't end up in the function's return value.
function Run {
  param([string]$Exe, [string[]]$ToolArgs)
  & $Exe @ToolArgs | Out-Host
  return $LASTEXITCODE
}

function Build {
  if(-Not (Test-Path -Path "$Build_Dir")){ New-Item -ItemType Directory "$Build_Dir" | Out-Null }
  Set-Location -Path "$Build_Dir"

  Write-Host "`n##### Running CMake #####`n"
  $RCode = Run cmake.exe @("-G", "Visual Studio 17 2022", "..")
  if($RCode -ne 0) {
    Write-Host "##### Failed CMake #####"
    return $RCode
  }

  Write-Host "`n##### Running MSBuild Mod #####`n"
  $RCode = Run $MSBuild @("/p:Configuration=Game__Shipping__Win64", "/nodeReuse:false", ".\StriveFrameData\StriveFrameData.sln")
  if($RCode -ne 0) {
    Write-Host "`n##### Failed MSBuild Mod #####"
    return $RCode
  }

  Write-Host "`n##### Running MSBuild Proxy #####`n"
  $RCode = Run $MSBuild @("/p:Configuration=Game__Shipping__Win64", "/nodeReuse:false", ".\RE-UE4SS\UE4SS\proxy_generator\proxy\proxy.sln")
  if($RCode -ne 0) {
    Write-Host "`n##### Failed MSBuild Proxy #####"
  }
  return $RCode
}

#$Version = $PSVersionTable.PSVersion
#if(($Version -lt 7) -or ($Version.Minor -lt 4) -or ($Version.Patch -lt 1)){
#  Write-Host "Build Script requires powershell version 7.4.1 or newer.`nCurrent version: $Version"
#  Exit 1
#}

$Build_Code = Build
#Write-Host "Build Code: $Build_Code"
Set-Location -Path "$Original_Dir"
Exit $Build_Code