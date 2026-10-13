// Folder Size Lite - GPL-3.0.
#pragma once
#include "common.h"
inline constexpr CLSID CLSID_FolderSizeLite =
    {0x4aa0a230, 0x6f44, 0x4e64, {0x81, 0x3e, 0x50, 0x13, 0x2e, 0x6c, 0xd7, 0x7f}};
extern "C" DWORD WINAPI FSLStart(void*);
extern "C" DWORD WINAPI FSLStop(void*);
extern "C" BOOL WINAPI FSLRunning();
extern "C" BOOL WINAPI FSLPinned();
