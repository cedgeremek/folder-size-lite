# GPL-3.0. Uses an existing Visual Studio C++ Build Tools installation.
param([string]$BuildDirectory = (Join-Path $PSScriptRoot 'build'), [switch]$Diagnostics)
$ErrorActionPreference = 'Stop'
$vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
$vs = & $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if (!$vs) { throw 'Install the Visual Studio C++ build tools to rebuild from source.' }
$dev = Join-Path $vs 'Common7\Tools\VsDevCmd.bat'
$environment = & $env:ComSpec /d /c "`"$dev`" -arch=x64 -host_arch=x64 >nul && set"
foreach ($line in $environment) { if ($line -match '^([^=]+)=(.*)$') { [Environment]::SetEnvironmentVariable($matches[1], $matches[2], 'Process') } }
New-Item -ItemType Directory -Force -Path $BuildDirectory | Out-Null
$build = (Resolve-Path -LiteralPath $BuildDirectory).Path
$src = Join-Path $PSScriptRoot 'src'
$mh = Join-Path $PSScriptRoot 'vendor\minhook'
$diagnosticFlags = @()
if ($Diagnostics) { $diagnosticFlags += '/DFSL_DIAGNOSTICS' }
Push-Location -LiteralPath $build
try {
    & cl.exe /nologo /c /O1 /MT /W3 /DUNICODE /D_UNICODE /DNDEBUG "/I$mh\include" "$mh\src\buffer.c" "$mh\src\hook.c" "$mh\src\trampoline.c" "$mh\src\hde\hde64.c"
    if ($LASTEXITCODE) { throw 'MinHook compile failed.' }
    & cl.exe /nologo /std:c++17 /O1 /MT /EHsc /W4 /DUNICODE /D_UNICODE /DNDEBUG @diagnosticFlags /LD "$src\hooks.cpp" "$src\extension.cpp" buffer.obj hook.obj trampoline.obj hde64.obj "/Fe:$PSScriptRoot\FolderSizeLite.dll" /link "/DEF:$src\FolderSizeLite.def" "/IMPLIB:$build\FolderSizeLite.lib" /OPT:REF /OPT:ICF ole32.lib oleaut32.lib shell32.lib propsys.lib uuid.lib user32.lib advapi32.lib
    if ($LASTEXITCODE) { throw 'Hook DLL compile failed.' }
    if (Test-Path -LiteralPath "$src\test.cpp") {
        & cl.exe /nologo /std:c++17 /O1 /MT /EHsc /W4 /DUNICODE /D_UNICODE /DNDEBUG "$src\test.cpp" "/Fe:$build\FolderSizeLiteTest.exe" /link /OPT:REF /OPT:ICF ole32.lib oleaut32.lib shell32.lib shlwapi.lib propsys.lib uuid.lib user32.lib psapi.lib
        if ($LASTEXITCODE) { throw 'Test compile failed.' }
    }
    if (Test-Path -LiteralPath "$src\menu_test.cpp") {
        & cl.exe /nologo /std:c++17 /O1 /MT /EHsc /W4 /DUNICODE /D_UNICODE /DNDEBUG "$src\menu_test.cpp" "/Fe:$build\FolderSizeLiteMenuTest.exe" /link /OPT:REF /OPT:ICF ole32.lib shell32.lib uuid.lib user32.lib
        if ($LASTEXITCODE) { throw 'Menu discovery test compile failed.' }
    }
} finally { Pop-Location }
