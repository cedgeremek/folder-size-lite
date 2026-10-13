# GPL-3.0. Optional machine registration, requiring a Windows administrator.
# Copies only the native DLL and exact-build symbol map into protected Program Files.
# No service, task, Run entry, remote injection or security-policy change.
[CmdletBinding(SupportsShouldProcess)]
param()
$ErrorActionPreference = 'Stop'
if (![Environment]::Is64BitProcess) { throw 'Use 64-bit PowerShell.' }
$identity = [Security.Principal.WindowsIdentity]::GetCurrent()
$principal = [Security.Principal.WindowsPrincipal]::new($identity)
$clsid = '{4AA0A230-6F44-4E64-813E-50132E6CD77F}'
$classPath = "Software\Classes\CLSID\$clsid"
$machine = [Microsoft.Win32.Registry]::LocalMachine
$user = [Microsoft.Win32.Registry]::CurrentUser
$programFiles = [IO.Path]::GetFullPath($env:ProgramFiles).TrimEnd('\')
$destination = [IO.Path]::GetFullPath((Join-Path $programFiles 'FolderSizeLite'))
if ($destination -ine "$programFiles\FolderSizeLite") { throw 'Unexpected installation path.' }
$installedDll = Join-Path $destination 'FolderSizeLite.dll'
$sourceDll = Join-Path $PSScriptRoot 'FolderSizeLite.dll'
$sourceMap = Join-Path $PSScriptRoot 'symbols.ini'
foreach ($file in @($sourceDll,$sourceMap)) {
    if (!(Test-Path -LiteralPath $file -PathType Leaf)) { throw "Missing runtime file: $file" }
}
foreach ($root in @($machine,$user)) {
    $key = $root.OpenSubKey("$classPath\InprocServer32")
    if ($key) {
        try {
            if ($key.GetValue('') -notin @($sourceDll,$installedDll)) { throw 'Another copy is registered; uninstall it first.' }
        } finally { $key.Dispose() }
    }
    foreach($type in @('Directory\Background','Directory','Drive')) {
        $key=$root.OpenSubKey("Software\Classes\$type\shellex\ContextMenuHandlers\FolderSizeLite")
        if($key) {
            try { if($key.GetValue('') -ne $clsid) { throw 'The menu registration name belongs to another extension.' } }
            finally {$key.Dispose()}
        }
    }
}
if (Test-Path -LiteralPath $destination) {
    $marker = Join-Path $destination 'installation-id.txt'
    if (!(Test-Path -LiteralPath $marker) -or (Get-Content -LiteralPath $marker -Raw).Trim() -ne $clsid) {
        throw 'The target folder already exists without this installer ownership marker.'
    }
    if ((Get-Item -LiteralPath $destination).Attributes -band [IO.FileAttributes]::ReparsePoint) { throw 'Installation target cannot be a reparse point.' }
}
if (!$PSCmdlet.ShouldProcess($destination,'Install two runtime files and machine COM/context-menu registration')) { return }
if (!$principal.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)) {
    throw 'Computer registration requires an administrator PowerShell window. Windows UAC approval must come from you.'
}
New-Item -ItemType Directory -Path $destination -Force | Out-Null
Set-Content -LiteralPath (Join-Path $destination 'installation-id.txt') -Value $clsid -Encoding ascii
Copy-Item -LiteralPath $sourceDll -Destination $installedDll -Force
Copy-Item -LiteralPath $sourceMap -Destination (Join-Path $destination 'symbols.ini') -Force
foreach ($name in @('FolderSizeLite.dll','symbols.ini')) {
    if ((Get-FileHash -LiteralPath (Join-Path $PSScriptRoot $name)).Hash -ne (Get-FileHash -LiteralPath (Join-Path $destination $name)).Hash) { throw 'Installed runtime hash mismatch.' }
}
function Write-MachineKey([string]$Path,[hashtable]$Values) {
    $key=$machine.CreateSubKey($Path)
    try { foreach($name in $Values.Keys) { $key.SetValue($name,$Values[$name]) } }
    finally { $key.Dispose() }
}
Write-MachineKey $classPath @{ ''='Folder Size Lite' }
Write-MachineKey "$classPath\InprocServer32" @{ ''=$installedDll; ThreadingModel='Apartment' }
foreach($type in @('Directory\Background','Directory','Drive')) {
    Write-MachineKey "Software\Classes\$type\shellex\ContextMenuHandlers\FolderSizeLite" @{ ''=$clsid }
}
# Only remove this copy's obsolete per-user registration after protected registration exists.
foreach($type in @('Directory\Background','Directory','Drive')) {
    $path="Software\Classes\$type\shellex\ContextMenuHandlers\FolderSizeLite"
    $key=$user.OpenSubKey($path)
    if($key) {
        try {$owned=$key.GetValue('') -eq $clsid} finally {$key.Dispose()}
        if($owned) {$user.DeleteSubKeyTree($path,$false)}
    }
}
$user.DeleteSubKeyTree($classPath,$false)
Add-Type 'public static class FslComputerInstallNotify { [System.Runtime.InteropServices.DllImport("shell32.dll")] public static extern void SHChangeNotify(uint e,uint f,System.IntPtr a,System.IntPtr b); }'
[FslComputerInstallNotify]::SHChangeNotify(0x08000000,0x1000,[IntPtr]::Zero,[IntPtr]::Zero)
Write-Host "Registered the Explorer menu for this computer. Runtime: $destination"
Write-Host 'Folder scanning is still off until enabled from the menu. No Explorer restart was performed.'
