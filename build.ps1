# Builds Sunrise3.xex with the Xbox 360 XDK toolchain (VS2010 / MSBuild 4.0).
#
# UseEnv=true is required: MSBuild's SetBuildDefaultEnvironmentVariables replaces
# PATH with $(ExecutablePath), and the xkelib pre-build event then cannot find
# lib.exe. Supplying PATH/INCLUDE/LIB here and skipping that target fixes it.

param(
    [ValidateSet('Debug', 'Release_LTCG', 'Release21256.0')]
    [string]$Configuration = 'Release_LTCG',

    [ValidateSet('Build', 'Rebuild', 'Clean')]
    [string]$Target = 'Build'
)

$ErrorActionPreference = 'Stop'

$xedk = $env:XEDK
if (-not $xedk) { $xedk = 'C:\Program Files (x86)\Microsoft Xbox 360 SDK' }
if (-not (Test-Path "$xedk\bin\win32\cl.exe")) {
    throw "Xbox 360 SDK not found at '$xedk'. Set XEDK to the SDK root."
}

$msbuild = 'C:\Windows\Microsoft.NET\Framework\v4.0.30319\MSBuild.exe'
if (-not (Test-Path $msbuild)) { throw "MSBuild 4.0 not found at '$msbuild'." }

$env:XEDK = $xedk
$env:PATH = "$xedk\bin\win32;$env:PATH"
$env:INCLUDE = "$xedk\include\xbox"
$env:LIB = "$xedk\lib\xbox"

$sln = Join-Path $PSScriptRoot 'Sunrise3.sln'

& $msbuild $sln "/p:Configuration=$Configuration" '/p:Platform=Xbox 360' `
    "/t:$Target" '/p:UseEnv=true' '/v:minimal' '/nologo' '/nr:false'

if ($LASTEXITCODE -ne 0) { throw "Build failed with exit code $LASTEXITCODE." }

if ($Target -ne 'Clean') {
    $xex = Join-Path $PSScriptRoot "Sunrise2\$Configuration\Sunrise3.xex"
    if (Test-Path $xex) { Write-Host "Output: $xex" }
}
