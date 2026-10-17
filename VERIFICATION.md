# Verification

## v0.1.1 updater repair — 16 October 2026

On Windows 11 25H2 **26200.9711**, the package map had been refreshed, but the
registered Program Files copy still contained the previous Windows image's map.
The installed DLL rejected activation with `ERROR_REVISION_MISMATCH` (1306),
reproducing the user's error.

The repaired preparation script discovered the actual COM installation, obtained
administrator approval, atomically updated its map, and verified the installed
SHA-256 against the prepared map. The **same installed DLL** then passed all
24 native regression checks through its registered COM class. The release DLL
also passed 24 native checks, five filesystem edges, and mismatch rejection on
this Windows build. Runtime DLLs were not replaced; no Explorer restart or UI
automation was used for this repair.

Preparation regressions passed under Windows PowerShell 5.1: a new package path
containing spaces, stale local map replacement, unchanged-map idempotency,
`-LocalOnly` preserving the installed map, and temporary-file cleanup. Repeating
normal preparation against the repaired machine installation succeeded without
elevation or rewriting its already matching map.

The `.cmd` launcher was also exercised through `cmd.exe` under Windows
PowerShell 5.1, including installed-map verification and a successful exit code.

The current Windows storage image has timestamp **3070770283**, size **9101312**,
and PDB identity **3E661D2422215F458D3B0C58AAF93AE31**. The prepared/installed
map SHA-256 is `C7602B3033D3A526F6868AC226CFE808F24E9793D403C529F89382EAD9A0C623`.
Its fixed file version is **10.0.26100.9711**; the file's descriptive version
string still reports 10.0.26100.9278, so the PE/PDB identity is the relevant gate.

See `verification/prepare-results.txt` and `verification/installed-map-repair-results.txt`.
Actual Explorer activation after the repair remains for the user to confirm.

## v0.1.0 preview — 12 October 2026

Tested on **Windows 11 25H2 build 26200.9550, x64**, with
`windows.storage.dll` version **10.0.26100.9278** and MSVC **14.39.33519**.
See the newer build's native-test coverage above. Other Windows versions have not
been validated.

The native runtime source is the same implementation that was installed and
confirmed working by its first user. The public package excludes personal paths,
desktop captures, registry diagnostics, and installation-session records.

## Native regression tests

The isolated host passed **24 checks**, including:

- Blank native folder Size properties before activation; exact totals afterward.
- Empty, nested, and Unicode folders; ordinary file sizes preserved.
- Numeric file/folder interleaving through native `CompareIDs`, in both argument
  orders, with non-size sorting unchanged.
- Cache invalidation after file creation, deletion, nested growth, and shrink.
- Unknown missing/network paths; cooperative scan cancellation.
- A real `IFileOperation` copy with correct nested contents while hooks are active.
- Pause/resume, COM lifetime, non-Explorer refusal, and safe DLL pinning.

**Five filesystem edge checks** passed: a junction cycle, Unicode paths beyond
MAX_PATH, a sparse logical file of 5 GiB + 7 bytes, hidden files, and access denied.
The temporary deny ACL was restored afterward. A mismatched map was rejected with
`ERROR_REVISION_MISMATCH` (1306).

The public source was rebuilt and these tests rerun for release preparation.
Text results are in [verification/](verification/). Bounds were inspected and
checked in small runs, not stress-tested to saturation.

## Actual Explorer

Computer-wide COM registration loaded the DLL from Program Files and made its
command visible in Explorer's actual classic context menu. The user then confirmed
the extension was working. Per-user registration had not exposed the command on
this PC; the precise Windows reason remains unknown.

Automated live-GUI checks of populated totals, both sort directions, and
pause/resume were not completed. Those behaviors were tested through the native
shell interfaces in the isolated host. User confirmation is separate evidence.
No claim of clean-machine installation or broad Windows compatibility is made.

## Footprint

| Runtime component | Bytes |
| --- | ---: |
| FolderSizeLite.dll | 207,360 |
| Exact-build symbols.ini | 234 |
| Total | 207,594 (202.73 KiB) |

The installer also writes a small ownership marker. No separate app, service,
startup task, or index is installed. One low-priority worker runs per enabled
Explorer process and waits on events while idle.

Earlier small-fixture measurements observed readiness in 47–63 ms with 40 ms
polling; private-memory increases of 450,560–475,136 bytes; working-set increases
of 1,847,296–1,855,488 bytes. Two-second idle samples measured 0.000 ms process CPU
with scan count unchanged. These were small, warm fixtures, not peak-memory,
cold-disk, large-tree, or whole-Explorer benchmarks.

## Compatibility boundary

The COM loading mechanism is documented; the size, property, and sorting hooks
use unsupported Windows internals. Activation verifies the PE/PDB identity and
function addresses against the running Windows binary. Preparation uses a small
HTTPS text map from Windhawk's public symbol cache. Exact matching is necessary,
but additional Windows builds still need their own behavior tests.

Runtime source and vendor code are included. The DLL is unsigned. An earlier
experimental remote-injection launcher was flagged by Defender and retired;
neither that executable nor that loading mechanism is included. The shipped
version uses normal COM registration without a helper executable. A prior local
scan of this replacement reported no new matching detection; this does not
guarantee future security-engine results.

## Release binary identity

The public release was rebuilt from the included source and tested in the isolated
host. Its DLL SHA-256 is:

`D83D8DCF4E9A70F0ABE58E121A1A0CAC5E4DF7F994DA939EC6672A3204CF02D5`

This rebuild did not replace or reconfigure the first user's installed copy.
