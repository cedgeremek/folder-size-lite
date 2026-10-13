# GPL-3.0. Remove only this application's owned machine registration and runtime.
[CmdletBinding(SupportsShouldProcess)]
param()
$ErrorActionPreference='Stop'
if (![Environment]::Is64BitProcess) { throw 'Use 64-bit PowerShell.' }
$principal=[Security.Principal.WindowsPrincipal]::new([Security.Principal.WindowsIdentity]::GetCurrent())
$clsid='{4AA0A230-6F44-4E64-813E-50132E6CD77F}'
$machine=[Microsoft.Win32.Registry]::LocalMachine
$classPath="Software\Classes\CLSID\$clsid"
$programFiles=[IO.Path]::GetFullPath($env:ProgramFiles).TrimEnd('\')
$destination=[IO.Path]::GetFullPath((Join-Path $programFiles 'FolderSizeLite'))
if ($destination -ine "$programFiles\FolderSizeLite") { throw 'Unexpected removal path.' }
$dll=Join-Path $destination 'FolderSizeLite.dll'
$key=$machine.OpenSubKey("$classPath\InprocServer32")
if($key) {
    try { if($key.GetValue('') -ine $dll) { throw 'A different copy is registered.' } }
    finally {$key.Dispose()}
}
if(Test-Path -LiteralPath $destination) {
    if ((Get-Item -LiteralPath $destination).Attributes -band [IO.FileAttributes]::ReparsePoint) { throw 'Removal target cannot be a reparse point.' }
    $marker=Join-Path $destination 'installation-id.txt'
    if(!(Test-Path -LiteralPath $marker) -or (Get-Content -LiteralPath $marker -Raw).Trim() -ne $clsid) { throw 'Ownership marker is missing.' }
}
if(!$PSCmdlet.ShouldProcess($destination,'Remove owned machine registration and runtime files')) { return }
if (!$principal.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)) { throw 'Use an administrator PowerShell window.' }
foreach($type in @('Directory\Background','Directory','Drive')) {
    $path="Software\Classes\$type\shellex\ContextMenuHandlers\FolderSizeLite"
    $key=$machine.OpenSubKey($path)
    if($key) {
        try {$owned=$key.GetValue('') -eq $clsid} finally {$key.Dispose()}
        if($owned) {$machine.DeleteSubKeyTree($path,$false)}
    }
}
$machine.DeleteSubKeyTree($classPath,$false)
[Microsoft.Win32.Registry]::CurrentUser.DeleteSubKeyTree('Software\FolderSizeLite',$false)
Add-Type 'public static class FslComputerUninstallNotify { [System.Runtime.InteropServices.DllImport("shell32.dll")] public static extern void SHChangeNotify(uint e,uint f,System.IntPtr a,System.IntPtr b); }'
[FslComputerUninstallNotify]::SHChangeNotify(0x08000000,0x1000,[IntPtr]::Zero,[IntPtr]::Zero)
if(Test-Path -LiteralPath $dll) {
    try { Remove-Item -LiteralPath $dll -ErrorAction Stop }
    catch { Write-Warning 'Registration removed. Explorer still has the DLL loaded; restart Explorer and rerun this script to remove its runtime files.'; return }
}
foreach($name in @('symbols.ini','installation-id.txt')) {
    $file=Join-Path $destination $name
    if(Test-Path -LiteralPath $file) { Remove-Item -LiteralPath $file }
}
if((Test-Path -LiteralPath $destination) -and @(Get-ChildItem -LiteralPath $destination -Force).Count -eq 0) { Remove-Item -LiteralPath $destination }
Write-Host 'Machine registration and runtime files removed. No Explorer window was closed.'
