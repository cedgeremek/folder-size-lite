# GPL-3.0. Preparation regression; no installation, elevation, or Explorer control.
param([string]$WorkDirectory = (Join-Path $PSScriptRoot '..\build\prepare-tests'))
$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$run = Join-Path $WorkDirectory ('package with spaces ' + [Guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $run -Force | Out-Null
$script = Join-Path $run 'Prepare.ps1'
Copy-Item -LiteralPath (Join-Path $root 'Prepare.ps1') -Destination $script
$installedPath = Join-Path $env:ProgramFiles 'FolderSizeLite\symbols.ini'
$before = if (Test-Path -LiteralPath $installedPath) { (Get-FileHash -LiteralPath $installedPath).Hash } else { $null }
$powerShell = Join-Path $env:WINDIR 'System32\WindowsPowerShell\v1.0\powershell.exe'
& $powerShell -NoProfile -ExecutionPolicy Bypass -File $script -LocalOnly | Out-Null
if ($LASTEXITCODE) { throw 'Preparation in a new folder failed.' }
$map = Join-Path $run 'symbols.ini'
$expected = (Get-FileHash -LiteralPath $map).Hash
Write-Output 'PASS preparation from a new package folder containing spaces under Windows PowerShell 5.1'
[IO.File]::WriteAllText($map, '[image]')
& $powerShell -NoProfile -ExecutionPolicy Bypass -File $script -LocalOnly | Out-Null
if ($LASTEXITCODE -or (Get-FileHash -LiteralPath $map).Hash -ne $expected) { throw 'Stale local map was not replaced.' }
Write-Output 'PASS stale local map is replaced with the exact current Windows map'
$modified = (Get-Item -LiteralPath $map).LastWriteTimeUtc
& $powerShell -NoProfile -ExecutionPolicy Bypass -File $script -LocalOnly | Out-Null
if ($LASTEXITCODE -or (Get-Item -LiteralPath $map).LastWriteTimeUtc -ne $modified) { throw 'An identical map was rewritten.' }
Write-Output 'PASS repeated preparation preserves an already matching map'
$after = if (Test-Path -LiteralPath $installedPath) { (Get-FileHash -LiteralPath $installedPath).Hash } else { $null }
if ($before -ne $after) { throw 'Local-only preparation changed the installed map.' }
Write-Output 'PASS local-only preparation leaves the installed map untouched'
if (@(Get-ChildItem -LiteralPath $run -Filter '*.tmp').Count) { throw 'Preparation left a temporary file behind.' }
Write-Output 'PASS atomic map replacement leaves no temporary files'
