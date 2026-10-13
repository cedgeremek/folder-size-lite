param([Parameter(Mandatory=$true)][string]$Edge, [Parameter(Mandatory=$true)][string]$TestExe)
$ErrorActionPreference = 'Stop'

New-Item -ItemType Directory -Force -Path "$edge\links", "$edge\long", "$edge\sparse", "$edge\hidden", "$edge\denied\locked" | Out-Null
function Sized([string]$path,[long]$bytes) {
    if ([IO.File]::Exists($path)) { [IO.File]::SetAttributes($path,[IO.FileAttributes]::Normal) }
    $f=[IO.File]::Open($path,[IO.FileMode]::Create,[IO.FileAccess]::Write,[IO.FileShare]::Read)
    try { $f.SetLength($bytes) } finally { $f.Dispose() }
}
Sized "$edge\links\data.bin" 1234
if (!(Test-Path -LiteralPath "$edge\links\cycle")) { New-Item -ItemType Junction -Path "$edge\links\cycle" -Target "$edge\links" | Out-Null }
$longPath="$edge\long"
for($i=0;$i -lt 6;$i++) { $longPath=Join-Path $longPath ('long-unicode-日本語-' + ('a'*36) + $i); [IO.Directory]::CreateDirectory($longPath) | Out-Null }
Sized (Join-Path $longPath 'data.bin') 4321
Sized "$edge\hidden\hidden.bin" 987
[IO.File]::SetAttributes("$edge\hidden\hidden.bin",[IO.FileAttributes]::Hidden)
Add-Type -TypeDefinition @'
using System; using System.Runtime.InteropServices; using Microsoft.Win32.SafeHandles;
public static class SparseFixture {
 [DllImport("kernel32.dll",SetLastError=true)] public static extern bool DeviceIoControl(SafeFileHandle file,uint code,IntPtr input,int inSize,IntPtr output,int outSize,out uint count,IntPtr overlapped);
}
'@
$f=[IO.File]::Open("$edge\sparse\large.bin",[IO.FileMode]::Create,[IO.FileAccess]::Write,[IO.FileShare]::Read)
try {
    [uint32]$count=0
    if(![SparseFixture]::DeviceIoControl($f.SafeFileHandle,0x900c4,[IntPtr]::Zero,0,[IntPtr]::Zero,0,[ref]$count,[IntPtr]::Zero)) { throw 'Could not mark fixture sparse; refusing to allocate 5 GiB.' }
    $f.SetLength(5368709127)
} finally { $f.Dispose() }
Sized "$edge\denied\locked\unreadable.bin" 111
$locked="$edge\denied\locked"
$original=Get-Acl -LiteralPath $locked
$changed=Get-Acl -LiteralPath $locked
$sid=[Security.Principal.WindowsIdentity]::GetCurrent().User
$rule=New-Object Security.AccessControl.FileSystemAccessRule($sid,[Security.AccessControl.FileSystemRights]::ListDirectory,[Security.AccessControl.AccessControlType]::Deny)
$changed.AddAccessRule($rule)
try {
    Set-Acl -LiteralPath $locked -AclObject $changed
    & $TestExe --edges $edge
    if($LASTEXITCODE) { throw 'Edge regression failed.' }
} finally { Set-Acl -LiteralPath $locked -AclObject $original }
