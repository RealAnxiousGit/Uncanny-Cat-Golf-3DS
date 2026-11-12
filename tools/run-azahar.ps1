# Opens a build in a visible Azahar window (the portable copy set up by the Fruit Slicer project).
#   tools\run-azahar.ps1 [path\to\file.3dsx]   default: dist\UncannyCatGolf-test.3dsx
param([string]$Rom)
$ErrorActionPreference = 'Stop'
$root = Split-Path $PSScriptRoot -Parent
$exe = Join-Path $root '..\Fruit Slicer 3DS\.tools\azahar\azahar.exe'
if (-not (Test-Path $exe)) { throw "Azahar not found at $exe" }
if (-not $Rom) { $Rom = Join-Path $root 'dist\UncannyCatGolf-test.3dsx' }
if (-not (Test-Path $Rom)) { throw "No build at $Rom" }
if (Get-Process azahar -ErrorAction SilentlyContinue) { throw 'Azahar is already open; close it first.' }
Start-Process (Resolve-Path $exe).Path -ArgumentList '-w', "`"$((Resolve-Path $Rom).Path)`""