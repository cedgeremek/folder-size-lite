param([Parameter(Mandatory=$true)][string]$Fixture)
$ErrorActionPreference = 'Stop'

New-Item -ItemType Directory -Force -Path "$fixture\00 Empty folder", "$fixture\01 Tiny folder", "$fixture\03 Nested folder\inside", "$fixture\05 Unicode folder" | Out-Null
function SizedFile([string]$path, [long]$bytes) {
    $file = [IO.File]::Open($path, [IO.FileMode]::Create, [IO.FileAccess]::Write, [IO.FileShare]::Read)
    try { $file.SetLength($bytes) } finally { $file.Dispose() }
}
SizedFile "$fixture\01 Tiny folder\one.bin" 1024
SizedFile "$fixture\02 Medium file.bin" 2048
SizedFile "$fixture\03 Nested folder\one.bin" 1048576
SizedFile "$fixture\03 Nested folder\inside\two.bin" 2097152
SizedFile "$fixture\04 Large file.bin" 5242880
SizedFile "$fixture\05 Unicode folder\café 日本語.bin" 4096
$fixture
