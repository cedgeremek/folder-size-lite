# GPL-3.0. Package an explicitly bounded set of public files; no installation.
[CmdletBinding()]
param([string]$OutputDirectory = (Join-Path $PSScriptRoot 'dist'))
$ErrorActionPreference = 'Stop'
$version = (Get-Content -LiteralPath (Join-Path $PSScriptRoot 'VERSION') -Raw).Trim()
if ($version -notmatch '^\d+\.\d+\.\d+(?:-[A-Za-z0-9.-]+)?$') { throw 'Invalid VERSION.' }
$relativeFiles = @(
    'FolderSizeLite.dll', 'symbols.ini', 'VERSION', 'README.md', 'VERIFICATION.md',
    'CHANGELOG.md', 'LICENSE.txt', 'THIRD-PARTY.md', 'Build.ps1', 'Prepare.ps1',
    'Install.ps1', 'Install-Computer.ps1', 'Uninstall.ps1', 'Uninstall-Computer.ps1',
    'Run-Tests.ps1', 'Package.ps1'
)
foreach ($directory in @('src','tests','vendor','verification')) {
    foreach ($file in Get-ChildItem -LiteralPath (Join-Path $PSScriptRoot $directory) -Recurse -File) {
        if ($file.Attributes -band [IO.FileAttributes]::ReparsePoint) { throw 'Package inputs cannot be reparse points.' }
        $relativeFiles += $file.FullName.Substring($PSScriptRoot.Length + 1).Replace('\','/')
    }
}
$relativeFiles = @($relativeFiles | Sort-Object -Unique)
$manifest = foreach ($relative in $relativeFiles) {
    $file = Get-Item -LiteralPath (Join-Path $PSScriptRoot $relative) -ErrorAction Stop
    if ($file.PSIsContainer -or ($file.Attributes -band [IO.FileAttributes]::ReparsePoint)) { throw "Invalid package input: $relative" }
    [ordered]@{ path=$relative; bytes=$file.Length; sha256=(Get-FileHash -LiteralPath $file.FullName -Algorithm SHA256).Hash.ToLowerInvariant() }
}
New-Item -ItemType Directory -Path $OutputDirectory -Force | Out-Null
$output = (Resolve-Path -LiteralPath $OutputDirectory).Path
$archivePath = Join-Path $output "FolderSizeLite-v$version-windows-x64.zip"
if (Test-Path -LiteralPath $archivePath) { throw 'Archive already exists; choose another output directory or remove that exact file explicitly.' }
Add-Type -AssemblyName System.IO.Compression.FileSystem
$archive = [IO.Compression.ZipFile]::Open($archivePath, [IO.Compression.ZipArchiveMode]::Create)
try {
    foreach ($relative in $relativeFiles) {
        [IO.Compression.ZipFileExtensions]::CreateEntryFromFile(
            $archive, (Join-Path $PSScriptRoot $relative), "FolderSizeLite/$relative",
            [IO.Compression.CompressionLevel]::Optimal) | Out-Null
    }
    $entry = $archive.CreateEntry('FolderSizeLite/SHA256.json')
    $writer = [IO.StreamWriter]::new($entry.Open(), [Text.UTF8Encoding]::new($false))
    try { $writer.Write(($manifest | ConvertTo-Json -Depth 4)) } finally { $writer.Dispose() }
} finally { $archive.Dispose() }
$hash = (Get-FileHash -LiteralPath $archivePath -Algorithm SHA256).Hash.ToLowerInvariant()
[IO.File]::WriteAllText("$archivePath.sha256", "$hash  $([IO.Path]::GetFileName($archivePath))`n", [Text.Encoding]::ASCII)
Write-Output "Created $archivePath ($((Get-Item -LiteralPath $archivePath).Length) bytes)"
Write-Output "SHA256 $hash"
