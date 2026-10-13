// Folder Size Lite - GPL-3.0. A normal, per-user COM context-menu extension.
#include "extension.h"
#include <shlobj.h>
#include <shellapi.h>
#include <strsafe.h>
#include <atomic>
#include <new>

static std::atomic<long> objects{0}, locks{0};
static constexpr wchar_t SettingsKey[] = L"Software\\FolderSizeLite";

#ifdef FSL_DIAGNOSTICS
static void Trace(const wchar_t* event, DWORD value = 0, REFIID iid = GUID_NULL) {
    wchar_t path[32768]; DWORD bytes = sizeof(path);
    if (RegGetValueW(HKEY_CURRENT_USER, SettingsKey, L"DiagnosticLog", RRF_RT_REG_SZ,
                    nullptr, path, &bytes) != ERROR_SUCCESS) return;
    wchar_t guid[40], line[256]; StringFromGUID2(iid, guid, ARRAYSIZE(guid));
    StringCchPrintfW(line, ARRAYSIZE(line), L"pid=%lu %s value=%08lx iid=%s\r\n", GetCurrentProcessId(), event, value, guid);
    HANDLE file = CreateFileW(path, FILE_APPEND_DATA, FILE_SHARE_READ | FILE_SHARE_WRITE,
                              nullptr, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) return;
    DWORD written; WriteFile(file, line, static_cast<DWORD>(wcslen(line) * sizeof(wchar_t)), &written, nullptr);
    CloseHandle(file);
}
#else
static void Trace(const wchar_t*, DWORD = 0, REFIID = GUID_NULL) {}
#endif

static bool IsExplorer() {
    wchar_t process[32768], windows[32768];
    DWORD n = GetModuleFileNameW(nullptr, process, ARRAYSIZE(process));
    UINT w = GetWindowsDirectoryW(windows, ARRAYSIZE(windows));
    if (!n || n >= ARRAYSIZE(process) || !w || w >= ARRAYSIZE(windows)) return false;
    return SamePath(process, std::wstring(windows) + L"\\explorer.exe");
}
static bool SavedEnabled() {
    DWORD value = 0, bytes = sizeof(value);
    return RegGetValueW(HKEY_CURRENT_USER, SettingsKey, L"Enabled", RRF_RT_REG_DWORD,
                       nullptr, &value, &bytes) == ERROR_SUCCESS && value == 1;
}
static LSTATUS SaveEnabled(bool enabled) {
    DWORD value = enabled ? 1 : 0;
    return RegSetKeyValueW(HKEY_CURRENT_USER, SettingsKey, L"Enabled", REG_DWORD, &value, sizeof(value));
}

class Menu final : public IShellExtInit, public IContextMenu {
    std::atomic<ULONG> refs{1};
    bool enableAction = true;
    bool explorer = false;
public:
    Menu() { ++objects; }
    ~Menu() { --objects; }
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid, void** out) override {
        Trace(L"Menu.QueryInterface", 0, iid);
        if (!out) return E_POINTER;
        *out = nullptr;
        if (iid == IID_IUnknown || iid == IID_IShellExtInit) *out = static_cast<IShellExtInit*>(this);
        else if (iid == IID_IContextMenu) *out = static_cast<IContextMenu*>(this);
        else return E_NOINTERFACE;
        AddRef(); return S_OK;
    }
    ULONG STDMETHODCALLTYPE AddRef() override { return ++refs; }
    ULONG STDMETHODCALLTYPE Release() override { ULONG n = --refs; if (!n) delete this; return n; }
    HRESULT STDMETHODCALLTYPE Initialize(PCIDLIST_ABSOLUTE, IDataObject*, HKEY) override {
        Trace(L"Initialize");
        try {
            explorer = IsExplorer();
            if (explorer) {
                // Remember the user's choice, activating only when Explorer loads
                // the menu. No service, startup executable, or remote injection.
                bool saved = SavedEnabled();
                if (saved && !FSLRunning()) FSLStart(nullptr);
                else if (!saved && FSLRunning()) FSLStop(nullptr);
            }
            enableAction = !FSLRunning();
            return S_OK;
        } catch (...) { return E_OUTOFMEMORY; }
    }
    HRESULT STDMETHODCALLTYPE QueryContextMenu(HMENU menu, UINT index, UINT first, UINT last, UINT flags) override {
        Trace(L"QueryContextMenu", flags);
        if ((flags & CMF_DEFAULTONLY) || first > last) return MAKE_HRESULT(SEVERITY_SUCCESS, 0, 0);
        const wchar_t* label = !explorer ? L"Folder Size Lite (File Explorer only)" :
            enableAction ? L"Folder Size Lite: show folder sizes" : L"Folder Size Lite: pause";
        if (!InsertMenuW(menu, index, MF_BYPOSITION | MF_STRING | (explorer ? MF_ENABLED : MF_GRAYED), first, label))
            return HRESULT_FROM_WIN32(GetLastError());
        return MAKE_HRESULT(SEVERITY_SUCCESS, 0, 1);
    }
    HRESULT STDMETHODCALLTYPE GetCommandString(UINT_PTR id, UINT type, UINT*, LPSTR buffer, UINT count) override {
        if (id != 0) return E_INVALIDARG;
        const wchar_t* verbW = enableAction ? L"foldersizelite.enable" : L"foldersizelite.pause";
        const char* verbA = enableAction ? "foldersizelite.enable" : "foldersizelite.pause";
        if (type == GCS_VALIDATEA || type == GCS_VALIDATEW) return S_OK;
        if (type == GCS_VERBW) return StringCchCopyW(reinterpret_cast<wchar_t*>(buffer), count, verbW);
        if (type == GCS_VERBA) return StringCchCopyA(buffer, count, verbA);
        if (type == GCS_HELPTEXTW) return StringCchCopyW(reinterpret_cast<wchar_t*>(buffer), count, L"Show or pause folder totals in Explorer's Size column.");
        if (type == GCS_HELPTEXTA) return StringCchCopyA(buffer, count, "Show or pause folder totals in Explorer's Size column.");
        return E_INVALIDARG;
    }
    HRESULT STDMETHODCALLTYPE InvokeCommand(CMINVOKECOMMANDINFO* info) override {
        if (!info || info->cbSize < sizeof(CMINVOKECOMMANDINFO)) return E_INVALIDARG;
        if (!explorer) return E_ACCESSDENIED; // Never patch another application's file dialog.
        bool enable = enableAction;
        auto ex = reinterpret_cast<CMINVOKECOMMANDINFOEX*>(info);
        bool unicode = info->cbSize >= sizeof(CMINVOKECOMMANDINFOEX) && (info->fMask & CMIC_MASK_UNICODE);
        if (unicode && !IS_INTRESOURCE(ex->lpVerbW)) {
            if (!_wcsicmp(ex->lpVerbW, L"foldersizelite.enable")) enable = true;
            else if (!_wcsicmp(ex->lpVerbW, L"foldersizelite.pause")) enable = false;
            else return E_INVALIDARG;
        } else if (IS_INTRESOURCE(info->lpVerb)) {
            if (LOWORD(info->lpVerb) != 0) return E_INVALIDARG;
        } else {
            if (!_stricmp(info->lpVerb, "foldersizelite.enable")) enable = true;
            else if (!_stricmp(info->lpVerb, "foldersizelite.pause")) enable = false;
            else return E_INVALIDARG;
        }
        DWORD error = enable ? FSLStart(nullptr) : FSLStop(nullptr);
        if (!error) {
            error = SaveEnabled(enable);
            if (error && enable) FSLStop(nullptr);
        }
        if (error) {
            const wchar_t* message = error == ERROR_REVISION_MISMATCH ?
                L"This Windows version needs a matching symbol map. Run Prepare.ps1, then try again." :
                error == ERROR_ALREADY_EXISTS ? L"Windhawk is already loaded. Use one folder-size implementation at a time." :
                L"Folder sizes could not be enabled. Check the included README for compatibility and setup.";
            if (!(info->fMask & CMIC_MASK_FLAG_NO_UI)) MessageBoxW(info->hwnd, message, L"Folder Size Lite", MB_OK | MB_ICONINFORMATION);
            return HRESULT_FROM_WIN32(error);
        }
        SHChangeNotify(SHCNE_ASSOCCHANGED, SHCNF_IDLIST | SHCNF_FLUSHNOWAIT, nullptr, nullptr);
        return S_OK;
    }
};

class Factory final : public IClassFactory {
    std::atomic<ULONG> refs{1};
public:
    Factory() { ++objects; }
    ~Factory() { --objects; }
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid, void** out) override {
        if (!out) return E_POINTER;
        *out = nullptr;
        if (iid != IID_IUnknown && iid != IID_IClassFactory) return E_NOINTERFACE;
        *out = static_cast<IClassFactory*>(this); AddRef(); return S_OK;
    }
    ULONG STDMETHODCALLTYPE AddRef() override { return ++refs; }
    ULONG STDMETHODCALLTYPE Release() override { ULONG n = --refs; if (!n) delete this; return n; }
    HRESULT STDMETHODCALLTYPE CreateInstance(IUnknown* outer, REFIID iid, void** out) override {
        if (!out) return E_POINTER;
        *out = nullptr;
        if (outer) return CLASS_E_NOAGGREGATION;
        auto object = new(std::nothrow) Menu();
        if (!object) return E_OUTOFMEMORY;
        HRESULT hr = object->QueryInterface(iid, out); object->Release(); return hr;
    }
    HRESULT STDMETHODCALLTYPE LockServer(BOOL lock) override { if (lock) ++locks; else --locks; return S_OK; }
};
STDAPI DllGetClassObject(REFCLSID clsid, REFIID iid, void** out) {
    Trace(L"DllGetClassObject", 0, iid);
    if (!out) return E_POINTER;
    *out = nullptr;
    if (clsid != CLSID_FolderSizeLite) return CLASS_E_CLASSNOTAVAILABLE;
    auto factory = new(std::nothrow) Factory();
    if (!factory) return E_OUTOFMEMORY;
    HRESULT hr = factory->QueryInterface(iid, out); factory->Release(); return hr;
}
STDAPI DllCanUnloadNow() {
    return objects == 0 && locks == 0 && !FSLPinned() ? S_OK : S_FALSE;
}
