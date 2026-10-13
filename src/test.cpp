// Focused regression and consumer-interface test. Not installed into Explorer.
#include "scanner.h"
#include "extension.h"
#include <propkey.h>
#include <propsys.h>
#include <shlwapi.h>
#include <shellapi.h>
#include <cstdio>
#include <functional>
#include <psapi.h>

static int failures = 0;
static void Check(bool ok, const char* label) {
    printf("%s %s\n", ok ? "PASS" : "FAIL", label); fflush(stdout);
    if (!ok) ++failures;
}
static std::optional<unsigned long long> Property(IShellFolder2* folder, PCUITEMID_CHILD child) {
    VARIANT value{};
    HRESULT hr = folder->GetDetailsEx(child, &PKEY_Size, &value);
    std::optional<unsigned long long> result;
    if (SUCCEEDED(hr) && value.vt == VT_UI8) result = value.ullVal;
    VariantClear(&value); return result;
}
static bool Eventually(const std::function<bool()>& predicate, DWORD timeout = 10000) {
    ULONGLONG end = GetTickCount64() + timeout;
    while (GetTickCount64() < end) { if (predicate()) return true; Sleep(40); }
    return false;
}
static void MakeFile(const std::wstring& path, unsigned bytes) {
    HANDLE file = CreateFileW(LongPath(path).c_str(), GENERIC_WRITE, FILE_SHARE_READ, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    LARGE_INTEGER pos{}; pos.QuadPart = bytes;
    if (file != INVALID_HANDLE_VALUE) { SetFilePointerEx(file, pos, nullptr, FILE_BEGIN); SetEndOfFile(file); CloseHandle(file); }
}
int wmain(int argc, wchar_t** argv) {
    if (argc < 3) { puts("Usage: test DLL FIXTURE [--mismatch]"); return 2; }
    if (!wcscmp(argv[1],L"--edges")) {
        std::wstring edge=argv[2];
        auto r=ScanFolder(edge+L"\\links",nullptr);
        Check(!r.error && r.bytes==1234 && r.links==1,"directory junction cycle is skipped without double counting");
        r=ScanFolder(edge+L"\\long",nullptr);
        Check(!r.error && r.bytes==4321,"Unicode paths longer than MAX_PATH");
        r=ScanFolder(edge+L"\\sparse",nullptr);
        Check(!r.error && r.bytes==5368709127ULL,"64-bit logical file sizes including a sparse file over 4 GiB");
        r=ScanFolder(edge+L"\\hidden",nullptr);
        Check(!r.error && r.bytes==987,"hidden files are included");
        r=ScanFolder(edge+L"\\denied",nullptr);
        Check(r.error==ERROR_ACCESS_DENIED,"access denied produces unknown instead of a partial total");
        return failures?1:0;
    }
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    std::wstring root = argv[2];
    if (argc > 3 && !wcscmp(argv[3], L"--measure")) {
        // A small, repeatable in-process measurement, not Explorer's total RAM.
        LoadLibraryExW(L"windows.storage.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
        PROCESS_MEMORY_COUNTERS_EX before{}; before.cb = sizeof(before);
        GetProcessMemoryInfo(GetCurrentProcess(), reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&before), sizeof(before));
        HMODULE measuredDll = LoadLibraryW(argv[1]);
        if (!measuredDll) return 2;
        auto measuredStart = reinterpret_cast<LiteStart>(GetProcAddress(measuredDll, "FSLStart"));
        auto measuredStop = reinterpret_cast<LiteStop>(GetProcAddress(measuredDll, "FSLStop"));
        auto measuredQuery = reinterpret_cast<LiteQuery>(GetProcAddress(measuredDll, "FSLQuery"));
        auto measuredStats = reinterpret_cast<LiteGetStats>(GetProcAddress(measuredDll, "FSLStats"));
        DWORD result = measuredStart(nullptr); if (result) return 2;
        ULONGLONG began = GetTickCount64(); unsigned long long bytes = 0;
        if (!Eventually([&] { return !measuredQuery(root.c_str(), &bytes); })) { measuredStop(nullptr); return 1; }
        ULONGLONG elapsed = GetTickCount64() - began;
        Sleep(600); // Let the final shell-change notification settle.
        PROCESS_MEMORY_COUNTERS_EX after{}; after.cb = sizeof(after);
        GetProcessMemoryInfo(GetCurrentProcess(), reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&after), sizeof(after));
        auto cpu = []() {
            FILETIME c,e,k,u; GetProcessTimes(GetCurrentProcess(), &c, &e, &k, &u);
            return ((static_cast<unsigned long long>(k.dwHighDateTime)<<32)|k.dwLowDateTime) +
                   ((static_cast<unsigned long long>(u.dwHighDateTime)<<32)|u.dwLowDateTime);
        };
        LiteStats first{}, last{}; measuredStats(&first);
        auto cpuBefore = cpu(); Sleep(2000); auto cpuAfter = cpu(); measuredStats(&last);
        printf("MEASURE fixture_bytes=%llu scan_observed_ms=%llu private_delta_bytes=%lld working_set_delta_bytes=%lld idle_cpu_ms=%.3f idle_seconds=2 scans_before=%llu scans_after=%llu entries=%u watches=%u\n",
            bytes,elapsed,static_cast<long long>(after.PrivateUsage)-static_cast<long long>(before.PrivateUsage),
            static_cast<long long>(after.WorkingSetSize)-static_cast<long long>(before.WorkingSetSize),
            (cpuAfter-cpuBefore)/10000.0,first.scans,last.scans,last.entries,last.watches);
        measuredStop(nullptr); CoUninitialize(); return 0;
    }
    PIDLIST_ABSOLUTE pidl = nullptr;
    HRESULT hr = SHParseDisplayName(root.c_str(), nullptr, &pidl, 0, nullptr);
    IShellFolder2* folder = nullptr;
    if (SUCCEEDED(hr)) hr = SHBindToObject(nullptr, pidl, nullptr, IID_PPV_ARGS(&folder));
    CoTaskMemFree(pidl);
    if (FAILED(hr) || !folder) { printf("Could not bind test folder: %lx\n", hr); return 2; }
    struct Row { std::wstring name; PITEMID_CHILD id; bool folder; std::optional<unsigned long long> before; };
    std::vector<Row> rows;
    IEnumIDList* enumeration = nullptr;
    folder->EnumObjects(nullptr, SHCONTF_FOLDERS | SHCONTF_NONFOLDERS | SHCONTF_INCLUDEHIDDEN, &enumeration);
    PITEMID_CHILD child;
    while (enumeration && enumeration->Next(1, &child, nullptr) == S_OK) {
        STRRET name{}; folder->GetDisplayNameOf(child, SHGDN_INFOLDER, &name);
        PWSTR text = nullptr; StrRetToStrW(&name, child, &text);
        SFGAOF attr = SFGAO_FOLDER; PCUITEMID_CHILD item = child; folder->GetAttributesOf(1, &item, &attr);
        rows.push_back({text ? text : L"", child, (attr & SFGAO_FOLDER) != 0, Property(folder, child)});
        CoTaskMemFree(text);
    }
    if (enumeration) enumeration->Release();
    Check(rows.size() == 6, "six fixture rows enumerated through the native shell");
    bool blanks = true;
    for (auto& row : rows) if (row.folder && row.before) blanks = false;
    Check(blanks, "baseline: native folder Size properties are blank");
    std::vector<short> beforeNames;
    for (auto& a : rows) for (auto& b : rows) beforeNames.push_back((short)HRESULT_CODE(folder->CompareIDs(0, a.id, b.id)));
    HMODULE dll = LoadLibraryW(argv[1]);
    if (!dll) { printf("LoadLibrary failed %lu\n", GetLastError()); return 2; }
    auto start = reinterpret_cast<LiteStart>(GetProcAddress(dll, "FSLStart"));
    auto stop = reinterpret_cast<LiteStop>(GetProcAddress(dll, "FSLStop"));
    auto query = reinterpret_cast<LiteQuery>(GetProcAddress(dll, "FSLQuery"));
    auto stats = reinterpret_cast<LiteGetStats>(GetProcAddress(dll, "FSLStats"));
    // Exercise the public COM contract without modifying another process or
    // automating a desktop menu. An ordinary file-dialog host must stay untouched.
    using GetFactory = HRESULT(WINAPI*)(REFCLSID, REFIID, void**);
    using CanUnload = HRESULT(WINAPI*)();
    auto getFactory = reinterpret_cast<GetFactory>(GetProcAddress(dll, "DllGetClassObject"));
    auto canUnload = reinterpret_cast<CanUnload>(GetProcAddress(dll, "DllCanUnloadNow"));
    Check(getFactory && canUnload && canUnload() == S_OK, "normal COM extension exports and initial unload state");
    IClassFactory* factory = nullptr;
    if (argc > 3 && !wcscmp(argv[3], L"--registered"))
        hr = CoGetClassObject(CLSID_FolderSizeLite, CLSCTX_INPROC_SERVER, nullptr, IID_PPV_ARGS(&factory));
    else hr = getFactory(CLSID_FolderSizeLite, IID_PPV_ARGS(&factory));
    IShellExtInit* extension = nullptr;
    if (SUCCEEDED(hr)) hr = factory->CreateInstance(nullptr, IID_PPV_ARGS(&extension));
    Check(SUCCEEDED(hr) && extension && canUnload() == S_FALSE, "COM class factory creates an extension and retains its lifetime");
    IContextMenu* menu = nullptr;
    if (extension) { extension->Initialize(nullptr, nullptr, nullptr); extension->QueryInterface(IID_PPV_ARGS(&menu)); }
    HMENU handle = CreatePopupMenu();
    hr = menu ? menu->QueryContextMenu(handle, 0, 100, 110, CMF_NORMAL) : E_FAIL;
    wchar_t verb[80]{};
    if (menu) menu->GetCommandString(0, GCS_VERBW, nullptr, reinterpret_cast<LPSTR>(verb), ARRAYSIZE(verb));
    Check(SUCCEEDED(hr) && HRESULT_CODE(hr) == 1 && !wcscmp(verb, L"foldersizelite.enable"), "menu exposes one canonical enable command");
    CMINVOKECOMMANDINFO invoke{}; invoke.cbSize = sizeof(invoke); invoke.fMask = CMIC_MASK_FLAG_NO_UI;
    hr = menu ? menu->InvokeCommand(&invoke) : E_FAIL;
    Check(hr == E_ACCESSDENIED && !reinterpret_cast<BOOL(WINAPI*)()>(GetProcAddress(dll, "FSLRunning"))(),
          "menu refuses to patch non-Explorer applications");
    DestroyMenu(handle);
    if (menu) menu->Release(); if (extension) extension->Release(); if (factory) factory->Release();
    Check(canUnload() == S_OK, "unactivated COM object releases cleanly");
    DWORD code = start(nullptr); printf("Start returned %lu\n", code);
    if (argc > 3 && !wcscmp(argv[3], L"--mismatch")) {
        Check(code == ERROR_REVISION_MISMATCH, "mismatched Windows symbol map refuses activation");
        folder->Release(); return failures ? 1 : 0;
    }
    Check(code == 0, "exact-build hooks enabled inside isolated host");
    if (code) return 1;
    std::map<std::wstring, unsigned long long> expected{
        {L"00 Empty folder",0}, {L"01 Tiny folder",1024}, {L"02 Medium file.bin",2048},
        {L"03 Nested folder",3145728}, {L"04 Large file.bin",5242880}, {L"05 Unicode folder",4096}
    };
    Check(Eventually([&] { for (auto& row : rows) if (Property(folder, row.id) != expected[row.name]) return false; return true; }),
          "native Size properties: empty, nested, Unicode, files and folders have exact totals");
    bool preserved = true; size_t index = 0;
    for (auto& a : rows) for (auto& b : rows)
        if ((short)HRESULT_CODE(folder->CompareIDs(0, a.id, b.id)) != beforeNames[index++]) preserved = false;
    Check(preserved, "non-size native sorting remains unchanged");
    int sizeColumn = -1;
    for (UINT i = 0; i < 64; ++i) { PROPERTYKEY key{}; if (SUCCEEDED(folder->MapColumnToSCID(i, &key)) && IsEqualPropertyKey(key, PKEY_Size)) { sizeColumn = i; break; } }
    bool order = sizeColumn >= 0;
    for (auto& a : rows) for (auto& b : rows) {
        short cmp = (short)HRESULT_CODE(folder->CompareIDs(sizeColumn, a.id, b.id));
        if (expected[a.name] < expected[b.name] && cmp >= 0) order = false;
        if (expected[a.name] > expected[b.name] && cmp <= 0) order = false;
    }
    Check(order, "native CompareIDs interleaves files and folders by numeric size in both directions");
    for (auto& row : rows) printf("ROW %ls %llu\n", row.name.c_str(), Property(folder, row.id).value_or(ULLONG_MAX));
    auto changed = root + L"\\01 Tiny folder\\update.bin";
    MakeFile(changed, 777);
    Check(Eventually([&] { unsigned long long n=0; return !query((root+L"\\01 Tiny folder").c_str(), &n) && n==1801; }), "filesystem notification invalidates a cached total after adding a file");
    DeleteFileW(LongPath(changed).c_str());
    Check(Eventually([&] { unsigned long long n=0; return !query((root+L"\\01 Tiny folder").c_str(), &n) && n==1024; }), "deleting a file restores the correct cached total");
    std::wstring nestedFile = root + L"\\03 Nested folder\\inside\\two.bin";
    MakeFile(nestedFile, 2097165);
    Check(Eventually([&] { unsigned long long n=0; return !query((root+L"\\03 Nested folder").c_str(), &n) && n==3145741; }), "nested file growth invalidates the ancestor folder");
    MakeFile(nestedFile,2097152);
    Check(Eventually([&] { unsigned long long n=0; return !query((root+L"\\03 Nested folder").c_str(), &n) && n==3145728; }), "nested shrink updates the ancestor folder");
    auto error = ScanFolder(root + L"\\missing", nullptr);
    Check(error.error != 0, "missing directory is unknown rather than zero");
    error = ScanFolder(L"\\\\unavailable-server\\share", nullptr);
    Check(error.error == ERROR_NOT_SUPPORTED, "network paths are rejected without connecting");
    HANDLE cancel = CreateEventW(nullptr, TRUE, TRUE, nullptr);
    error = ScanFolder(root, cancel); CloseHandle(cancel);
    Check(error.error == ERROR_CANCELLED, "scan cancellation is checked during enumeration");
    auto copiedTo = ParentPath(root) + L"\\Copy result";
    CreateDirectoryW(copiedTo.c_str(), nullptr);
    IShellItem *sourceItem=nullptr, *targetItem=nullptr;
    IFileOperation* operation=nullptr;
    hr = CoCreateInstance(CLSID_FileOperation, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&operation));
    if (SUCCEEDED(hr)) hr = SHCreateItemFromParsingName((root+L"\\03 Nested folder").c_str(), nullptr, IID_PPV_ARGS(&sourceItem));
    if (SUCCEEDED(hr)) hr = SHCreateItemFromParsingName(copiedTo.c_str(), nullptr, IID_PPV_ARGS(&targetItem));
    if (SUCCEEDED(hr)) hr = operation->SetOperationFlags(FOF_NOCONFIRMATION | FOF_NOERRORUI | FOF_SILENT);
    if (SUCCEEDED(hr)) hr = operation->CopyItem(sourceItem,targetItem,L"copied",nullptr);
    if (SUCCEEDED(hr)) hr = operation->PerformOperations();
    auto copied = ScanFolder(copiedTo+L"\\copied",nullptr);
    Check(SUCCEEDED(hr) && copied.error==0 && copied.bytes==3145728 && copied.files==2, "real native IFileOperation copy preserves nested contents with hooks active");
    if(operation) operation->Release(); if(sourceItem)sourceItem->Release(); if(targetItem)targetItem->Release();
    LiteStats measured{}; stats(&measured);
    printf("STATS scans=%llu hits=%llu errors=%llu invalidations=%llu entries=%u watches=%u\n",
        measured.scans,measured.hits,measured.errors,measured.invalidations,measured.entries,measured.watches);
    Check(measured.scans < 40 && measured.entries <= 512 && measured.watches<=8,"cache reuse and resource bounds");
    stop(nullptr);
    Check(canUnload() == S_FALSE, "activated hook DLL stays pinned even after pausing");
    blanks = true;
    for (auto& row : rows) { auto n=Property(folder,row.id); if (row.folder ? n.has_value() : n != row.before) blanks=false; }
    Check(blanks,"disable restores native blank folder sizes and preserves file sizes");
    Check(start(nullptr)==0,"same loaded DLL can be re-enabled");
    Check(Eventually([&] { unsigned long long n=0; return !query((root+L"\\01 Tiny folder").c_str(),&n)&&n==1024; }),"cache restarts after disable");
    stop(nullptr);
    for(auto& row:rows) CoTaskMemFree(row.id);
    folder->Release(); CoUninitialize();
    printf("RESULT %d failures\n",failures); return failures ? 1:0;
}
