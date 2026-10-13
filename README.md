# Folder Size Lite

Folder sizes in Windows File Explorer's existing **Size** column, with files and
folders sorted together by size.

**About 203 KiB of installed runtime. No separate background app, tray icon,
service, startup task, Windhawk installation, or Everything index.** One native
DLL runs inside Explorer, with one low-priority scanning thread while enabled.

[Download v0.1.0 preview](https://github.com/cedgeremek/folder-size-lite/releases/tag/v0.1.0)
· [Verification](VERIFICATION.md) · [Source attribution](THIRD-PARTY.md)

This is an early **Windows x64 preview**, tested on Windows 11 25H2 build
26200.9550. It uses unsupported Explorer internals adapted from m417z's Windhawk
mod. Matching symbols are required, and other Windows builds remain unverified.
It is an independent derivative, not an official Windhawk release.

## Install

1. Download and extract `FolderSizeLite-v0.1.0-windows-x64.zip` from the release.
2. Open **64-bit PowerShell as administrator** in the extracted `FolderSizeLite`
   directory. Run these two commands in order; stop if preparation fails:

   ```powershell
   powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\Prepare.ps1
   powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\Install-Computer.ps1
   ```

   The execution-policy option applies only to those PowerShell processes.
   Preparation downloads a small exact-build text map from Windhawk's public
   symbol cache, not executable code or a full PDB. Installation copies the DLL
   and map into `C:\Program Files\FolderSizeLite` and registers a normal COM
   context-menu extension. It does not restart Explorer.

3. In Explorer, right-click empty space in a local folder, select **Show more
   options**, then **Folder Size Lite: show folder sizes**.
4. Switch to **Details** view, press **F5**, and wait for totals. Click **Size**
   to sort. Click it again after scanning if Explorer retains its previous order.

Your enable/pause preference is remembered. After signing in or restarting
Explorer, open **Show more options** once to load and resume the extension.
Use **Folder Size Lite: pause** to stop scanning, then refresh to clear old totals.

The tested installation needed computer-wide registration. An alternative
per-user `Install.ps1` is included, but its menu did not appear on the tested PC,
even after restarting Explorer. If a newly installed menu is missing, a manual
Explorer restart may be needed; this closes folder windows and briefly removes
the taskbar.

## What makes it small

- The tested DLL is 207,360 bytes; its symbol map is 234 bytes: **202.73 KiB combined**.
  Source, licenses, setup scripts, and a small installation ownership marker are additional.
- Sizes are calculated only when Explorer requests them. There is no whole-drive index.
- One background-priority worker sleeps on events when idle.
- Bounds: 512 cached results, 1 MiB of cached path text, 256 queued scans, and
  eight directory watches with 32 KiB buffers each.
- Directory-change notifications invalidate totals. Requested entries older than
  60 seconds are also rechecked.
- Metadata is read; file contents are not. A scan has a cooperative 30-second
  deadline and a maximum depth of 256.

A first scan of a large tree still takes time. Sorting may request many folder
sizes at once. The [small-fixture measurements](VERIFICATION.md) are not peak
memory or general disk-performance benchmarks.

## Supported folders and limits

- Local fixed, removable, and RAM drives. Network paths and virtual shell folders
  stay unchanged.
- Logical byte totals, not allocated disk space. Hidden files count; separate
  hard-link names count separately.
- Directory junctions and symbolic links are excluded to avoid cycles and double
  counting. Cloud/provider directory reparse points are unsupported.
- Access errors and incomplete scans leave the total unknown rather than showing
  a misleading partial value. Empty ordinary folders show zero.
- Activation is refused if the symbol map does not match the installed Windows
  binary, or Windhawk is detected in the same process.
- Each Explorer process has its own cache. A pause applies to the current process;
  another process picks up the saved preference when its menu opens.
- Other applications' Open/Save dialogs are not modified.

The release is unsigned. Windows/security software may ask about unsigned code.
No antivirus exclusion or security-policy change is part of installation.

## Windows updates and upgrades

After a Windows update, prepare a new map in the extracted package directory:

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\Prepare.ps1
```

For a computer-wide installation, run that in administrator PowerShell and copy
the prepared map into the installed directory:

```powershell
Copy-Item -LiteralPath .\symbols.ini -Destination "$env:ProgramFiles\FolderSizeLite\symbols.ini" -Force
```

Then use the enable command again. If no matching map exists, activation is
refused. A matching map alone does not establish compatibility with a new build.

To replace the DLL with a future release, pause and uninstall, restart Explorer
or sign out to unload it, finish removal if requested, and install the new release.
Do not overwrite a DLL while Explorer has it loaded.

## Uninstall

Pause from the menu, then run in administrator PowerShell from the extracted package:

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\Uninstall-Computer.ps1
```

This removes only this application's owned registration and runtime files. If
Explorer still holds the DLL, restart Explorer or sign out, then rerun the script
to finish. The uninstaller never closes Explorer automatically. Even after
pausing, the DLL stays mapped until Explorer exits so in-flight shell calls can
return safely. For a per-user installation, use `Uninstall.ps1` instead.

## Build and test

Requires Visual Studio 2022 C++ Build Tools with the Windows SDK, and 64-bit
PowerShell. No .NET, Python, or browser runtime is needed by the extension.

```powershell
.\Build.ps1
.\Prepare.ps1
.\Run-Tests.ps1
.\Package.ps1
```

Builds use C++17, static CRT linking, and bundled MinHook. Build outputs go into
`build/`, plus the runtime DLL in the repository root. Tests run in an isolated
native host, create fixtures under `build/`, and do not install or control Explorer.
The test DLL is pinned only inside the test process, which exits afterward.

`Package.ps1` creates a ZIP with the runtime, complete corresponding source,
licenses, scripts, and a per-file SHA-256 manifest. Download archives and build
products are kept out of Git. It also writes the ZIP's SHA-256 checksum beside it.

## Attribution and license

GPL-3.0. The Explorer hook signatures, copy-operation exclusion, property-cache
extraction, and comparison approach derive from
[Better file sizes in Explorer details](https://github.com/ramensoftware/windhawk-mods/blob/c89e4f4b2c154f31a7b6f5f80a164b2f24a288f6/mods/explorer-details-better-file-sizes.wh.cpp)
by **m417z (Michael Maltsev)**. Folder Size Lite adds its own bounded asynchronous
scanner, cache, change notifications, COM loading, and packaging.

See [THIRD-PARTY.md](THIRD-PARTY.md), [LICENSE.txt](LICENSE.txt), and the included
[MinHook license](vendor/minhook/LICENSE.txt).
