// Folder Size Lite - GPL-3.0.
// Explorer hook signatures, copy-operation exclusion, property extraction and
// size comparison adapted from m417z's Better file sizes in Explorer details.
// Upstream c89e4f4b2c154f31a7b6f5f80a164b2f24a288f6. See THIRD-PARTY.md.
#include "scanner.h"
#include "../vendor/minhook/include/MinHook.h"
#include <propkey.h>
#include <propsys.h>
#include <propvarutil.h>
#include <tlhelp32.h>

static HMODULE self;
static SizeCache* cache = nullptr; // Intentionally retained until process exit; no loader-lock joins.
static std::mutex lifecycle;
static std::atomic<bool> running{false};
static bool created = false;
static std::atomic<bool> pinned{false};
static thread_local unsigned operationDepth = 0;
static thread_local bool inSize = false;
static thread_local bool resolving = false;

using GetSizeFn = HRESULT(WINAPI*)(void*, const ITEMID_CHILD*, const void*, PROPVARIANT*);
using OperationFn = HRESULT(WINAPI*)(void*);
using ItemValueFn = HRESULT(WINAPI*)(void*, const void*, int, const PROPERTYKEY*, PROPVARIANT*, int*);
using ItemPropertyFn = HRESULT(WINAPI*)(void*, void*, const void*, int, const PROPERTYKEY*, int*, PROPVARIANT*);
using MapColumnFn = HRESULT(WINAPI*)(void*, UINT, PROPERTYKEY*);
using DetailsFn = HRESULT(WINAPI*)(void*, const ITEMID_CHILD*, const PROPERTYKEY*, VARIANT*);
using CompareFn = HRESULT(WINAPI*)(void*, LPARAM, const ITEMIDLIST_RELATIVE*, const ITEMIDLIST_RELATIVE*);
static GetSizeFn oldSize;
static OperationFn oldPrepare, oldDo;
static ItemValueFn oldValue;
static ItemPropertyFn oldProperty;
static MapColumnFn mapColumn;
static DetailsFn details;
static CompareFn oldCompare;

static std::wstring ItemPath(void* object, const ITEMID_CHILD* child) {
    IShellFolder* folder = nullptr;
    if (FAILED(static_cast<IUnknown*>(object)->QueryInterface(IID_PPV_ARGS(&folder)))) return {};
    IShellItem* item = nullptr;
    HRESULT hr = SHCreateItemWithParent(nullptr, folder, child, IID_PPV_ARGS(&item));
    folder->Release();
    if (FAILED(hr)) return {};
    PWSTR path = nullptr;
    hr = item->GetDisplayName(SIGDN_FILESYSPATH, &path);
    item->Release();
    if (FAILED(hr) || !path) return {};
    std::wstring result(path); CoTaskMemFree(path); return result;
}
static HRESULT WINAPI SizeHook(void* object, const ITEMID_CHILD* child, const void* id, PROPVARIANT* value) {
    HRESULT hr = oldSize(object, child, id, value);
    if (!running || operationDepth || resolving || hr != S_OK || value->vt != VT_EMPTY) return hr;
    try {
        resolving = true;
        auto path = ItemPath(object, child);
        resolving = false;
        if (!path.empty()) {
            auto size = cache->Query(path);
            if (size) { value->vt = VT_UI8; value->uhVal.QuadPart = *size; }
        }
    } catch (...) { resolving = false; }
    return hr;
}
static HRESULT WINAPI PrepareHook(void* object) {
    ++operationDepth; HRESULT hr = oldPrepare(object); --operationDepth; return hr;
}
static HRESULT WINAPI DoHook(void* object) {
    ++operationDepth; HRESULT hr = oldDo(object); --operationDepth; return hr;
}
static HRESULT WINAPI ValueHook(void* object, const void* item, int mode, const PROPERTYKEY* key, PROPVARIANT* value, int* state) {
    bool previous = inSize;
    inSize = running && IsEqualPropertyKey(*key, PKEY_Size);
    HRESULT hr = oldValue(object, item, mode, key, value, state);
    inSize = previous; return hr;
}
static HRESULT WINAPI PropertyHook(void* object, void* store, const void* item, int mode,
                                  const PROPERTYKEY* key, int* dirty, PROPVARIANT* value) {
    HRESULT hr = oldProperty(object, store, item, mode, key, dirty, value);
    if (!running || !inSize || mode == 0 || !IsEqualPropertyKey(*key, PKEY_Size) ||
        (SUCCEEDED(hr) && value->vt != VT_EMPTY && !(dirty && *dirty))) return hr;
    PropVariantClear(value);
    hr = oldProperty(object, store, item, 0, key, nullptr, value);
    if (SUCCEEDED(hr) && dirty) *dirty = 0;
    return hr;
}
static HRESULT WINAPI CompareHook(void* object, LPARAM column, const ITEMIDLIST_RELATIVE* a, const ITEMIDLIST_RELATIVE* b) {
    auto original = [&]() { return oldCompare(object, column, a, b); };
    if (!running || operationDepth || !a || !b || (column & ~static_cast<LPARAM>(SHCIDS_COLUMNMASK))) return original();
    PROPERTYKEY key;
    if (FAILED(mapColumn(object, (UINT)column, &key)) || !IsEqualPropertyKey(key, PKEY_Size)) return original();
    VARIANT va{}, vb{};
    HRESULT ha = details(object, reinterpret_cast<const ITEMID_CHILD*>(a), &key, &va);
    HRESULT hb = details(object, reinterpret_cast<const ITEMID_CHILD*>(b), &key, &vb);
    bool ka = SUCCEEDED(ha) && va.vt == VT_UI8;
    bool kb = SUCCEEDED(hb) && vb.vt == VT_UI8;
    int result = 0;
    if (ka && kb) result = va.ullVal < vb.ullVal ? -1 : va.ullVal > vb.ullVal ? 1 : 0;
    else if (ka != kb) result = ka ? 1 : -1; // Unknown is a category, not a fabricated zero-byte total.
    VariantClear(&va); VariantClear(&vb);
    if (!result) return oldCompare(object, 0, a, b); // Stable native name order for ties.
    return static_cast<HRESULT>(static_cast<USHORT>(static_cast<SHORT>(result)));
}

static DWORD CreateHooks() {
    // Do not overlap a Windhawk engine's hooks in the same process.
    HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPMODULE, GetCurrentProcessId());
    if (snapshot == INVALID_HANDLE_VALUE) return GetLastError();
    MODULEENTRY32W module{}; module.dwSize = sizeof(module);
    bool conflict = false;
    if (Module32FirstW(snapshot, &module)) do {
        std::wstring name(module.szModule);
        std::transform(name.begin(), name.end(), name.begin(), [](wchar_t c) { return (wchar_t)towlower(c); });
        if (name.find(L"windhawk") != std::wstring::npos) { conflict = true; break; }
    } while (Module32NextW(snapshot, &module));
    CloseHandle(snapshot);
    if (conflict) return ERROR_ALREADY_EXISTS;
    HMODULE storage = LoadLibraryExW(L"windows.storage.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
    if (!storage) return GetLastError();
    auto identity = IdentifyImage(storage);
    std::wstring ini = ModuleDirectory(self) + L"\\symbols.ini";
    wchar_t text[100];
    GetPrivateProfileStringW(L"image", L"pdb", L"", text, 100, ini.c_str());
    if (identity.pdb != text || identity.timestamp != GetPrivateProfileIntW(L"image", L"timestamp", 0, ini.c_str()) ||
        identity.size != GetPrivateProfileIntW(L"image", L"size", 0, ini.c_str())) return ERROR_REVISION_MISMATCH;
    struct Hook { const wchar_t* key; void* replacement; void** original; };
    Hook hooks[] = {
        {L"GetSize", (void*)SizeHook, (void**)&oldSize},
        {L"Prepare", (void*)PrepareHook, (void**)&oldPrepare},
        {L"Do", (void*)DoHook, (void**)&oldDo},
        {L"ItemValue", (void*)ValueHook, (void**)&oldValue},
        {L"ItemProperty", (void*)PropertyHook, (void**)&oldProperty},
        {L"MapColumn", nullptr, (void**)&mapColumn},
        {L"Details", nullptr, (void**)&details},
        {L"Compare", (void*)CompareHook, (void**)&oldCompare},
    };
    std::vector<void*> targets;
    for (auto& hook : hooks) {
        DWORD rva = GetPrivateProfileIntW(L"symbols", hook.key, 0, ini.c_str());
        if (!rva || !ExecutableRva(storage, rva)) return ERROR_INVALID_ADDRESS;
        targets.push_back(reinterpret_cast<BYTE*>(storage) + rva);
    }
    if (MH_Initialize() != MH_OK) return ERROR_INVALID_FUNCTION;
    for (size_t i = 0; i < std::size(hooks); ++i) {
        auto& hook = hooks[i];
        if (!hook.replacement) { *hook.original = targets[i]; continue; }
        if (MH_CreateHook(targets[i], hook.replacement, hook.original) != MH_OK) {
            MH_Uninitialize(); return ERROR_INVALID_FUNCTION;
        }
    }
    created = true; return ERROR_SUCCESS;
}

extern "C" __declspec(dllexport) DWORD WINAPI FSLStart(void*) {
    std::lock_guard<std::mutex> lock(lifecycle);
    if (running) return ERROR_SUCCESS;
    try {
        if (!created) { DWORD error = CreateHooks(); if (error) return error; }
        if (!pinned) {
            HMODULE retained;
            if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_PIN,
                                   reinterpret_cast<LPCWSTR>(self), &retained)) return GetLastError();
            pinned = true; // COM must never unload executable hook return addresses.
        }
        if (!cache) cache = new SizeCache();
        if (!cache->Start()) return ERROR_NOT_ENOUGH_MEMORY;
        running = true;
        if (MH_EnableHook(MH_ALL_HOOKS) != MH_OK) { running = false; cache->Stop(); return ERROR_INVALID_FUNCTION; }
        return ERROR_SUCCESS;
    } catch (...) { return ERROR_NOT_ENOUGH_MEMORY; }
}
extern "C" __declspec(dllexport) DWORD WINAPI FSLStop(void*) {
    std::lock_guard<std::mutex> lock(lifecycle);
    running = false;
    if (created) MH_DisableHook(MH_ALL_HOOKS);
    if (cache) cache->Stop();
    // Trampolines and this DLL stay mapped until Explorer exits. An in-flight hook
    // may still be returning through them; freeing executable code here is unsafe.
    return ERROR_SUCCESS;
}
extern "C" __declspec(dllexport) DWORD WINAPI FSLQuery(const wchar_t* path, unsigned long long* value) {
    if (!running || !cache) return ERROR_SERVICE_NOT_ACTIVE;
    try { auto n = cache->Query(path); if (!n) return ERROR_IO_PENDING; *value = *n; return 0; }
    catch (...) { return ERROR_NOT_ENOUGH_MEMORY; }
}
extern "C" __declspec(dllexport) void WINAPI FSLStats(LiteStats* stats) {
    if (cache) cache->Stats(stats); else *stats = {};
}
extern "C" __declspec(dllexport) BOOL WINAPI FSLRunning() { return running; }
extern "C" __declspec(dllexport) BOOL WINAPI FSLPinned() { return pinned; }
BOOL WINAPI DllMain(HINSTANCE module, DWORD reason, void*) {
    if (reason == DLL_PROCESS_ATTACH) self = module;
    return TRUE;
}
