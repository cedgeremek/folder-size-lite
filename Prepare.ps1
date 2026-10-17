# Retrieves only symbol offsets, never executable code or full debug symbols.
# GPL-3.0. Requires Windows x64 and Windhawk's map for the exact installed build.
[CmdletBinding()]
param([switch]$LocalOnly, [switch]$NoElevation)
$ErrorActionPreference = 'Stop'
if (![Environment]::Is64BitProcess) { throw 'Use 64-bit PowerShell to prepare the symbol map.' }

function Get-PreparedMapHash([string]$Path) {
    # Avoid module-autoload differences when Windows PowerShell is launched via cmd.
    $stream = [IO.File]::OpenRead($Path)
    $algorithm = [Security.Cryptography.SHA256]::Create()
    try { return [BitConverter]::ToString($algorithm.ComputeHash($stream)).Replace('-','') }
    finally { $stream.Dispose(); $algorithm.Dispose() }
}

function Write-PreparedMap([string]$Path, [byte[]]$Content) {
    if (Test-Path -LiteralPath $Path) {
        if ((Get-Item -LiteralPath $Path).Attributes -band [IO.FileAttributes]::ReparsePoint) { throw "Map cannot be a reparse point: $Path" }
        $existing = [IO.File]::ReadAllBytes($Path)
        if ([Convert]::ToBase64String($existing) -ceq [Convert]::ToBase64String($Content)) { return }
    }
    $temporary = "$Path.$([Guid]::NewGuid().ToString('N')).tmp"
    try {
        [IO.File]::WriteAllBytes($temporary, $Content)
        if ([IO.File]::Exists($Path)) { [IO.File]::Replace($temporary, $Path, [NullString]::Value) }
        else { [IO.File]::Move($temporary, $Path) }
    } finally {
        if ([IO.File]::Exists($temporary)) { [IO.File]::Delete($temporary) }
    }
}

function Get-InstalledMap {
    $clsid = '{4AA0A230-6F44-4E64-813E-50132E6CD77F}'
    $classPath = "Software\Classes\CLSID\$clsid\InprocServer32"
    $userKey = [Microsoft.Win32.Registry]::CurrentUser.OpenSubKey($classPath)
    $machineKey = [Microsoft.Win32.Registry]::LocalMachine.OpenSubKey($classPath)
    try {
        if ($userKey) { $registeredDll = [string]$userKey.GetValue(''); $machine = $false }
        elseif ($machineKey) { $registeredDll = [string]$machineKey.GetValue(''); $machine = $true }
        else { return $null }
    } finally {
        if ($userKey) { $userKey.Dispose() }
        if ($machineKey) { $machineKey.Dispose() }
    }
    if (![IO.Path]::IsPathRooted($registeredDll) -or [IO.Path]::GetFileName($registeredDll) -ine 'FolderSizeLite.dll') {
        throw 'The registered Folder Size Lite DLL path is unexpected.'
    }
    $directory = [IO.Path]::GetDirectoryName([IO.Path]::GetFullPath($registeredDll))
    if (!(Test-Path -LiteralPath $registeredDll -PathType Leaf)) { throw "Registered runtime is missing: $registeredDll" }
    if ((Get-Item -LiteralPath $directory).Attributes -band [IO.FileAttributes]::ReparsePoint) { throw 'Installed directory cannot be a reparse point.' }
    if ($machine) {
        $expected = [IO.Path]::GetFullPath((Join-Path $env:ProgramFiles 'FolderSizeLite'))
        $marker = Join-Path $directory 'installation-id.txt'
        if ($directory -ine $expected -or !(Test-Path -LiteralPath $marker) -or
            ((Get-Item -LiteralPath $marker).Attributes -band [IO.FileAttributes]::ReparsePoint) -or
            (Get-Content -LiteralPath $marker -Raw).Trim() -ne $clsid) {
            throw 'Computer-wide installation ownership could not be verified.'
        }
    }
    return @{ Path=(Join-Path $directory 'symbols.ini'); Machine=$machine }
}
$dllPath = Join-Path $env:WINDIR 'System32\windows.storage.dll'
$data = [IO.File]::ReadAllBytes($dllPath)
function U16([int]$offset) { [BitConverter]::ToUInt16($data, $offset) }
function U32([int]$offset) { [BitConverter]::ToUInt32($data, $offset) }
$nt = [int](U32 0x3c)
if ((U16 ($nt + 4)) -ne 0x8664) { throw 'This build of Folder Size Lite supports x64 Windows only.' }
$optional = $nt + 24
$timestamp = U32 ($nt + 8)
$imageSize = U32 ($optional + 56)
$debugRva = U32 ($optional + 112 + 48)
$debugSize = U32 ($optional + 112 + 52)
$sectionStart = $optional + (U16 ($nt + 20))
$sections = @()
for ($j = 0; $j -lt (U16 ($nt + 6)); $j++) {
    $s = $sectionStart + $j * 40
    $sections += @{Va=(U32 ($s+12)); Size=[Math]::Max((U32 ($s+8)),(U32 ($s+16))); Raw=(U32 ($s+20)); Flags=(U32 ($s+36))}
}
function RawOffset([uint32]$rva) {
    foreach ($s in $sections) { if ($rva -ge $s.Va -and $rva -lt ($s.Va + $s.Size)) { return [int]($s.Raw + $rva - $s.Va) } }
    throw 'Invalid PE address.'
}
$debugOffset = RawOffset $debugRva
$pdbId = $null
for ($p = $debugOffset; $p -lt ($debugOffset + $debugSize); $p += 28) {
    if ((U32 ($p + 12)) -ne 2) { continue }
    $cv = [int](U32 ($p + 24))
    if ([Text.Encoding]::ASCII.GetString($data, $cv, 4) -ne 'RSDS') { continue }
    $guidBytes = New-Object byte[] 16
    [Array]::Copy($data, $cv + 4, $guidBytes, 0, 16)
    $guid = New-Object Guid (,$guidBytes)
    $pdbId = $guid.ToString('N').ToUpperInvariant() + (U32 ($cv + 20)).ToString('X')
    break
}
if (!$pdbId) { throw 'No usable symbol identity in windows.storage.dll.' }
$uri = "https://ramensoftware.github.io/windhawk-mod-symbol-cache/explorer-details-better-file-sizes/pdb_$pdbId.txt"
$response = Invoke-WebRequest -UseBasicParsing -Uri $uri -TimeoutSec 20
$parts = ([string]$response.Content).Trim().Split('#')
if ($parts.Length -lt 5 -or $parts[0] -ne '1' -or $parts[1] -ne 'windows.storage.dll' -or $parts[2] -ne "x86-64_${timestamp}_${imageSize}") {
    throw 'The symbol map does not match this Windows binary; Explorer will not be modified.'
}
$wanted = [ordered]@{
    GetSize='CFSFolder::_GetSize('; Prepare='CRecursiveFolderOperation::Prepare('; Do='CRecursiveFolderOperation::Do('
    ItemValue='CDefCollection::Item_GetValue('; ItemProperty='CDefCollection::_GetItemProperty('
    MapColumn='CFSFolder::MapColumnToSCID('; Details='CFSFolder::GetDetailsEx('; Compare='CFSFolder::CompareIDs('
}
$lines = @('[image]', "timestamp=$timestamp", "size=$imageSize", "pdb=$pdbId", '', '[symbols]')
foreach ($key in $wanted.Keys) {
    $found = @()
    for ($i = 3; $i + 1 -lt $parts.Length; $i += 2) { if ($parts[$i].Contains($wanted[$key])) { $found += $parts[$i + 1] } }
    if ($found.Count -ne 1 -or !$found[0]) { throw "Missing or ambiguous function: $key" }
    $rva = [uint32]::Parse($found[0])
    $valid = @($sections | Where-Object { $rva -ge $_.Va -and $rva -lt ($_.Va+$_.Size) -and ($_.Flags -band 0x20000000) })
    if ($valid.Count -ne 1) { throw "Function address is outside executable sections: $key" }
    $lines += "$key=$rva"
}
$output = Join-Path $PSScriptRoot 'symbols.ini'
$content = [Text.Encoding]::ASCII.GetBytes(($lines -join "`r`n") + "`r`n")
Write-PreparedMap $output $content
Write-Output "Prepared exact-build symbol map: $pdbId ($($response.RawContentLength) downloaded bytes)."
Write-Output "Package map: $output"
if ($LocalOnly) { Write-Output 'Local-only preparation: the installed map was not changed.'; return }
$installed = Get-InstalledMap
if (!$installed) { Write-Output 'No registered installation found. The package is ready to install.'; return }
$expectedHash = Get-PreparedMapHash $output
$mapMatches = (Test-Path -LiteralPath $installed.Path) -and
    (Get-PreparedMapHash $installed.Path) -eq $expectedHash
if (!$mapMatches) {
    $principal = [Security.Principal.WindowsPrincipal]::new([Security.Principal.WindowsIdentity]::GetCurrent())
    if ($installed.Machine -and !$principal.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)) {
        if ($NoElevation) { throw 'Updating the installed map needs administrator approval. Run Prepare.cmd or administrator PowerShell.' }
        Write-Output 'Updating the installed map requires Windows administrator approval.'
        $powerShell = Join-Path $env:WINDIR 'System32\WindowsPowerShell\v1.0\powershell.exe'
        $arguments = @('-NoProfile','-ExecutionPolicy','Bypass','-File',('"' + $PSCommandPath + '"'),'-NoElevation')
        $process = Start-Process -FilePath $powerShell -ArgumentList $arguments -Verb RunAs -WindowStyle Hidden -PassThru -Wait
        if ($process.ExitCode -ne 0) { throw "Administrator map update failed (exit $($process.ExitCode)). Run Prepare.cmd from administrator PowerShell for details." }
    } else { Write-PreparedMap $installed.Path $content }
}
if (!(Test-Path -LiteralPath $installed.Path) -or
    (Get-PreparedMapHash $installed.Path) -ne $expectedHash) {
    throw 'The installed map was not updated. Explorer would still use the old map.'
}
Write-Output "Verified installed map: $($installed.Path)"
Write-Output 'Ready. In Explorer, choose Folder Size Lite: show folder sizes again, then refresh.'
