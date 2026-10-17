# GPL-3.0. Register only this user's normal 64-bit Explorer context-menu handler.
param([switch]$RestartExplorer)
$ErrorActionPreference = 'Stop'
if (![Environment]::Is64BitProcess) { throw 'Run this script in 64-bit PowerShell.' }
$dll = Join-Path $PSScriptRoot 'FolderSizeLite.dll'
if (!(Test-Path -LiteralPath $dll)) { throw 'FolderSizeLite.dll is missing. Build it or restore the complete package.' }
& (Join-Path $PSScriptRoot 'Prepare.ps1') -LocalOnly
$clsid = '{4AA0A230-6F44-4E64-813E-50132E6CD77F}'
$user = [Microsoft.Win32.Registry]::CurrentUser
$classPath = "Software\Classes\CLSID\$clsid"
$computer = [Microsoft.Win32.Registry]::LocalMachine.OpenSubKey("$classPath\InprocServer32")
if ($computer) { $computer.Dispose(); throw 'A computer-wide copy is registered. Use Install-Computer.ps1 or uninstall that copy first.' }
$existing = $user.OpenSubKey("$classPath\InprocServer32")
if ($existing) {
    try {
        if ($existing.GetValue('') -ne $dll) { throw 'A different copy is registered. Uninstall that copy first.' }
    } finally { $existing.Dispose() }
}
function Write-Key([string]$Path, [hashtable]$Values) {
    $key = $user.CreateSubKey($Path)
    try { foreach ($name in $Values.Keys) { $key.SetValue($name, $Values[$name]) } }
    finally { $key.Dispose() }
}
Write-Key $classPath @{ '' = 'Folder Size Lite' }
Write-Key "$classPath\InprocServer32" @{ '' = $dll; 'ThreadingModel' = 'Apartment' }
foreach ($type in @('Directory\Background', 'Directory', 'Drive')) {
    Write-Key "Software\Classes\$type\shellex\ContextMenuHandlers\FolderSizeLite" @{ '' = $clsid }
}
$settings = $user.CreateSubKey('Software\FolderSizeLite')
try { if ($null -eq $settings.GetValue('Enabled')) { $settings.SetValue('Enabled', 0, [Microsoft.Win32.RegistryValueKind]::DWord) } }
finally { $settings.Dispose() }
if (-not ('FolderSizeLiteSetup.Notify' -as [type])) {
    Add-Type 'namespace FolderSizeLiteSetup { public static class Notify { [System.Runtime.InteropServices.DllImport("shell32.dll")] public static extern void SHChangeNotify(uint e, uint f, System.IntPtr a, System.IntPtr b); } }'
}
[FolderSizeLiteSetup.Notify]::SHChangeNotify(0x08000000, 0, [IntPtr]::Zero, [IntPtr]::Zero)
Write-Host 'Registered for your Windows account. No separate app or startup task was installed.'
if ($RestartExplorer) {
    # Registration and association notifications must precede a fresh Explorer.
    # This opt-in switch closes folder windows and briefly removes the taskbar.
    $currentSession = (Get-Process -Id $PID).SessionId
    $explorerPath = Join-Path $env:WINDIR 'explorer.exe'
    $shells = @(Get-Process explorer -ErrorAction SilentlyContinue | Where-Object SessionId -eq $currentSession)
    foreach ($shell in $shells) {
        if ($shell.Path -ine $explorerPath) { throw 'Unexpected Explorer executable path; restart aborted.' }
    }
    foreach ($shell in $shells) {
        Stop-Process -Id $shell.Id -Force
        if (!$shell.WaitForExit(5000)) { throw 'Explorer did not exit; restart it manually before testing the menu.' }
    }
    Start-Sleep -Milliseconds 1500
    $fresh = @(Get-Process explorer -ErrorAction SilentlyContinue | Where-Object SessionId -eq $currentSession)
    if (!$fresh.Count) { Start-Process -FilePath $explorerPath -WindowStyle Hidden }
    Write-Host 'Explorer restarted after registration.'
} else {
    Write-Host 'If the menu entry is missing, restart Windows Explorer after registration.'
    Write-Host 'Optional: .\Install.ps1 -RestartExplorer (closes folder windows; taskbar briefly disappears).'
}
Write-Host 'In File Explorer, right-click empty space > Show more options > Folder Size Lite: show folder sizes.'
Write-Host 'Then use Details view, press F5, and click the Size column.'
