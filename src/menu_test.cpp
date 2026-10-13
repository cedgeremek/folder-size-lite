// GPL-3.0. In-process shell-registration test. Does not display a menu, invoke
// a command, or control an existing Explorer window.
#include "common.h"
#include "extension.h"
#include <shlobj.h>
#include <cstdio>

int wmain(int argc, wchar_t** argv) {
    if (argc < 2) return 2;
    OleInitialize(nullptr);
    PIDLIST_ABSOLUTE id = nullptr;
    HRESULT hr = SHParseDisplayName(argv[1], nullptr, &id, 0, nullptr);
    IShellFolder* folder = nullptr;
    if (SUCCEEDED(hr)) hr = SHBindToObject(nullptr, id, nullptr, IID_PPV_ARGS(&folder));
    CoTaskMemFree(id);
    printf("Bind folder %08lx\n", hr);
    IContextMenu* menu = nullptr;
    if (SUCCEEDED(hr)) hr = folder->CreateViewObject(nullptr, IID_PPV_ARGS(&menu));
    printf("Create background menu %08lx\n", hr);
    if (FAILED(hr)) return 2;
    HMENU handle = CreatePopupMenu();
    hr = menu->QueryContextMenu(handle, 0, 1, 0x6fff, CMF_NORMAL | CMF_EXPLORE);
    printf("Query native background menu %08lx\n", hr);
    bool found = false;
    for (int i = 0; i < GetMenuItemCount(handle); ++i) {
        wchar_t label[1024]{};
        MENUITEMINFOW item{}; item.cbSize = sizeof(item);
        item.fMask = MIIM_STRING | MIIM_ID | MIIM_STATE;
        item.dwTypeData = label; item.cch = ARRAYSIZE(label);
        GetMenuItemInfoW(handle, i, TRUE, &item);
        printf("ENTRY id=%u state=%u %ls\n", item.wID, item.fState, label);
        if (wcsstr(label, L"Folder Size Lite")) found = true;
    }
    printf("Registered handler discovered=%s\n", found ? "true" : "false");
    DestroyMenu(handle); menu->Release(); folder->Release();
    // Separately check a fresh COM activation, without preloading the DLL.
    IShellExtInit* extension = nullptr;
    hr = CoCreateInstance(CLSID_FolderSizeLite, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&extension));
    printf("Fresh COM activation %08lx\n", hr);
    if (extension) extension->Release();
    OleUninitialize(); return found ? 0 : 1;
}
