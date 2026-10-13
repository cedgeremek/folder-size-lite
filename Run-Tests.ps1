# GPL-3.0. Isolated native tests; no installation or Explorer automation.
param([string]$BuildDirectory = (Join-Path $PSScriptRoot 'build'))
$ErrorActionPreference = 'Stop'
$build = (Resolve-Path -LiteralPath $BuildDirectory).Path
$test = Join-Path $build 'FolderSizeLiteTest.exe'
if (!(Test-Path -LiteralPath $test)) { throw 'Run Build.ps1 first, using the same BuildDirectory.' }
$dll = Join-Path $PSScriptRoot 'FolderSizeLite.dll'
$run = Join-Path $build ('test-run-' + [Guid]::NewGuid().ToString('N'))
$fixture = Join-Path $run 'Explorer size demo'
& (Join-Path $PSScriptRoot 'tests\Create-Fixture.ps1') -Fixture $fixture | Out-Null
& $test $dll $fixture | Tee-Object -FilePath (Join-Path $run 'native-results.txt')
if ($LASTEXITCODE) { throw 'Native shell regression failed.' }
& (Join-Path $PSScriptRoot 'tests\Test-Edges.ps1') -Edge (Join-Path $run 'edges') -TestExe $test |
    Tee-Object -FilePath (Join-Path $run 'edge-results.txt')
$mismatch = Join-Path $run 'mismatch'
New-Item -ItemType Directory -Path $mismatch | Out-Null
Copy-Item -LiteralPath $dll -Destination (Join-Path $mismatch 'FolderSizeLite.dll')
[IO.File]::WriteAllText((Join-Path $mismatch 'symbols.ini'), '[image]', [Text.Encoding]::ASCII)
& $test (Join-Path $mismatch 'FolderSizeLite.dll') $fixture --mismatch |
    Tee-Object -FilePath (Join-Path $run 'mismatch-results.txt')
if ($LASTEXITCODE) { throw 'Version mismatch regression failed.' }
& $test $dll $fixture --measure | Tee-Object -FilePath (Join-Path $run 'measurement-results.txt')
if ($LASTEXITCODE) { throw 'Resource measurement failed.' }
Write-Host "Tests passed. Evidence and fixtures are in $run"
