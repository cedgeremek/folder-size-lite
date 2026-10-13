// Folder Size Lite - GPL-3.0. See LICENSE.txt and THIRD-PARTY.md.
#pragma once
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <string>
#include <vector>
#include <algorithm>

inline std::wstring ModuleDirectory(HMODULE module = nullptr) {
    std::wstring path(32768, L'\0');
    DWORD n = GetModuleFileNameW(module, path.data(), (DWORD)path.size());
    if (!n || n >= path.size()) return {};
    path.resize(n);
    return path.substr(0, path.find_last_of(L'\\'));
}
inline std::wstring ParentPath(const std::wstring& path) {
    auto pos = path.find_last_of(L'\\');
    if (pos == std::wstring::npos) return {};
    return path.substr(0, pos == 2 ? 3 : pos);
}
inline bool SamePath(const std::wstring& a, const std::wstring& b) {
    return CompareStringOrdinal(a.c_str(), (int)a.size(), b.c_str(), (int)b.size(), TRUE) == CSTR_EQUAL;
}
inline bool Within(const std::wstring& child, const std::wstring& parent) {
    if (child.size() < parent.size()) return false;
    if (CompareStringOrdinal(child.c_str(), (int)parent.size(), parent.c_str(), (int)parent.size(), TRUE) != CSTR_EQUAL) return false;
    return child.size() == parent.size() || parent.back() == L'\\' || child[parent.size()] == L'\\';
}
inline std::wstring LongPath(const std::wstring& path) {
    return path.rfind(L"\\\\?\\", 0) == 0 ? path : L"\\\\?\\" + path;
}
inline std::wstring PlainPath(std::wstring path) {
    if (path.rfind(L"\\\\?\\", 0) == 0) path.erase(0, 4);
    while (path.size() > 3 && path.back() == L'\\') path.pop_back();
    return path;
}
inline bool LocalPath(const std::wstring& path) {
    if (path.size() < 3 || path[1] != L':' || path[2] != L'\\') return false;
    wchar_t root[] = {path[0], L':', L'\\', 0};
    auto type = GetDriveTypeW(root);
    return type == DRIVE_FIXED || type == DRIVE_REMOVABLE || type == DRIVE_RAMDISK;
}

struct ImageIdentity {
    DWORD timestamp = 0, size = 0;
    std::wstring pdb;
};
inline ImageIdentity IdentifyImage(HMODULE mod) {
    ImageIdentity out;
    auto base = reinterpret_cast<const BYTE*>(mod);
    auto dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(base);
    if (!base || dos->e_magic != IMAGE_DOS_SIGNATURE) return out;
    auto nt = reinterpret_cast<const IMAGE_NT_HEADERS64*>(base + dos->e_lfanew);
    if (nt->Signature != IMAGE_NT_SIGNATURE || nt->FileHeader.Machine != IMAGE_FILE_MACHINE_AMD64) return out;
    out.timestamp = nt->FileHeader.TimeDateStamp;
    out.size = nt->OptionalHeader.SizeOfImage;
    auto dir = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_DEBUG];
    if (!dir.VirtualAddress || dir.VirtualAddress + dir.Size > out.size) return out;
    auto debug = reinterpret_cast<const IMAGE_DEBUG_DIRECTORY*>(base + dir.VirtualAddress);
    for (DWORD i = 0; i < dir.Size / sizeof(*debug); ++i) {
        if (debug[i].Type != IMAGE_DEBUG_TYPE_CODEVIEW || debug[i].SizeOfData < 24 ||
            debug[i].AddressOfRawData + debug[i].SizeOfData > out.size) continue;
        const BYTE* cv = base + debug[i].AddressOfRawData;
        if (memcmp(cv, "RSDS", 4)) continue;
        GUID guid; DWORD age;
        memcpy(&guid, cv + 4, sizeof(guid)); memcpy(&age, cv + 20, 4);
        wchar_t text[80];
        swprintf_s(text, L"%08X%04X%04X%02X%02X%02X%02X%02X%02X%02X%02X%X",
            guid.Data1, guid.Data2, guid.Data3, guid.Data4[0], guid.Data4[1], guid.Data4[2], guid.Data4[3],
            guid.Data4[4], guid.Data4[5], guid.Data4[6], guid.Data4[7], age);
        out.pdb = text; break;
    }
    return out;
}
inline bool ExecutableRva(HMODULE mod, DWORD rva) {
    auto base = reinterpret_cast<const BYTE*>(mod);
    auto nt = reinterpret_cast<const IMAGE_NT_HEADERS64*>(base + reinterpret_cast<const IMAGE_DOS_HEADER*>(base)->e_lfanew);
    auto sec = IMAGE_FIRST_SECTION(nt);
    for (unsigned i = 0; i < nt->FileHeader.NumberOfSections; ++i)
        if (rva >= sec[i].VirtualAddress && rva < sec[i].VirtualAddress + sec[i].Misc.VirtualSize)
            return (sec[i].Characteristics & IMAGE_SCN_MEM_EXECUTE) != 0;
    return false;
}

struct LiteStats {
    unsigned long long scans, hits, errors, invalidations, files;
    unsigned entries, queued, watches, active;
};
using LiteStart = DWORD(WINAPI*)(void*);
using LiteStop = DWORD(WINAPI*)(void*);
using LiteQuery = DWORD(WINAPI*)(const wchar_t*, unsigned long long*);
using LiteGetStats = void(WINAPI*)(LiteStats*);
