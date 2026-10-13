# Retrieves only symbol offsets, never executable code or full debug symbols.
# GPL-3.0. Requires Windows x64 and Windhawk's map for the exact installed build.
$ErrorActionPreference = 'Stop'
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
$temp = Join-Path $PSScriptRoot 'symbols.ini.new'
[IO.File]::WriteAllLines($temp, $lines, [Text.Encoding]::ASCII)
Move-Item -LiteralPath $temp -Destination $output -Force
Write-Output "Prepared exact-build symbol map: $pdbId ($($response.RawContentLength) downloaded bytes)."
