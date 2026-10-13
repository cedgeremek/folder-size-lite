# GPL-3.0. Remove this copy's per-user COM registration; never terminate Explorer.
$ErrorActionPreference = 'Stop'
if (![Environment]::Is64BitProcess) { throw 'Run this script in 64-bit PowerShell.' }
$clsid = '{4AA0A230-6F44-4E64-813E-50132E6CD77F}'
$dll = Join-Path $PSScriptRoot 'FolderSizeLite.dll'
$user = [Microsoft.Win32.Registry]::CurrentUser
$classPath = "Software\Classes\CLSID\$clsid"
$existing = $user.OpenSubKey("$classPath\InprocServer32")
if ($existing) {
    try { if ($existing.GetValue('') -ne $dll) { throw 'A different copy is registered. Run its Uninstall.ps1 instead.' } }
    finally { $existing.Dispose() }
}
foreach ($type in @('Directory\Background', 'Directory', 'Drive')) {
    $verb = "Software\Classes\$type\shell\FolderSizeLite"
    $key = $user.OpenSubKey($verb)
    if ($key) {
        try { $owned = $key.GetValue('ExplorerCommandHandler') -eq $clsid } finally { $key.Dispose() }
        if ($owned) { $user.DeleteSubKeyTree($verb, $false) }
    }
    $path = "Software\Classes\$type\shellex\ContextMenuHandlers\FolderSizeLite"
    $key = $user.OpenSubKey($path)
    if ($key) {
        try { $owned = $key.GetValue('') -eq $clsid } finally { $key.Dispose() }
        if ($owned) { $user.DeleteSubKeyTree($path, $false) }
    }
}
$user.DeleteSubKeyTree($classPath, $false)
$user.DeleteSubKeyTree('Software\FolderSizeLite', $false)
if (-not ('FolderSizeLiteSetup.Notify' -as [type])) {
    Add-Type 'namespace FolderSizeLiteSetup { public static class Notify { [System.Runtime.InteropServices.DllImport("shell32.dll")] public static extern void SHChangeNotify(uint e, uint f, System.IntPtr a, System.IntPtr b); } }'
}
[FolderSizeLiteSetup.Notify]::SHChangeNotify(0x08000000, 0, [IntPtr]::Zero, [IntPtr]::Zero)
Write-Host 'Registration removed. If previously enabled, it remains active in the current Explorer process until Explorer exits.'
Write-Host 'Sign out and back in, or restart Windows Explorer, to finish unloading. You can then delete this package.'
