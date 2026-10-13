Folder Size Lite is licensed under GPL-3.0. Source and build instructions are included.

Explorer hooks are adapted from **Better file sizes in Explorer details**, by
m417z (Michael Maltsev), GPL-3.0:
https://github.com/ramensoftware/windhawk-mods/blob/c89e4f4b2c154f31a7b6f5f80a164b2f24a288f6/mods/explorer-details-better-file-sizes.wh.cpp

The function signatures, copy-operation exclusion, property-cache extraction,
and comparison approach are reused. The bounded asynchronous filesystem scanner,
cache, change notifications, COM extension, and packaging are specific to this project.
This is an independent derivative, not an official Windhawk release.

Prepare.ps1 retrieves a small exact-build function-offset map from Windhawk's
public symbol-cache repository:
https://github.com/ramensoftware/windhawk-mod-symbol-cache
It does not install Windhawk or Everything. After preparation the program runs offline.

MinHook v1.3.4, Copyright (C) 2009-2017 Tsuda Kageyu, is included under its BSD-style
license. The x64 decoder includes additional copyrights as documented in
vendor/minhook/LICENSE.txt. Source:
https://github.com/TsudaKageyu/minhook/tree/v1.3.4
