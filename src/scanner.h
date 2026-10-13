// Folder Size Lite - GPL-3.0.
#pragma once
#include "common.h"
#include <atomic>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <deque>
#include <set>
#include <shlobj.h>

struct ScanResult { unsigned long long bytes = 0, files = 0; DWORD error = 0; unsigned links = 0; };
inline ScanResult ScanFolder(const std::wstring& plain, HANDLE stop, DWORD budgetMs = 30000) {
    ScanResult out;
    if (!LocalPath(plain)) { out.error = ERROR_NOT_SUPPORTED; return out; }
    std::wstring root = LongPath(plain);
    DWORD attr = GetFileAttributesW(root.c_str());
    if (attr == INVALID_FILE_ATTRIBUTES) { out.error = GetLastError(); return out; }
    if (!(attr & FILE_ATTRIBUTE_DIRECTORY) || (attr & (FILE_ATTRIBUTE_REPARSE_POINT | FILE_ATTRIBUTE_OFFLINE))) {
        out.error = ERROR_NOT_SUPPORTED; return out;
    }
    struct Frame { std::wstring path; HANDLE find; WIN32_FIND_DATAW data; bool first; };
    std::vector<Frame> stack;
    auto push = [&](const std::wstring& path) {
        Frame frame{path, INVALID_HANDLE_VALUE, {}, true};
        frame.find = FindFirstFileExW((path + (path.back() == L'\\' ? L"*" : L"\\*")).c_str(), FindExInfoBasic, &frame.data,
                                     FindExSearchNameMatch, nullptr, FIND_FIRST_EX_LARGE_FETCH);
        if (frame.find == INVALID_HANDLE_VALUE) {
            DWORD err = GetLastError();
            if (err != ERROR_FILE_NOT_FOUND) out.error = err;
            return false;
        }
        stack.push_back(std::move(frame)); return true;
    };
    ULONGLONG until = GetTickCount64() + budgetMs;
    push(root);
    while (!stack.empty() && !out.error) {
        if (stop && WaitForSingleObject(stop, 0) == WAIT_OBJECT_0) { out.error = ERROR_CANCELLED; break; }
        if (GetTickCount64() > until) { out.error = ERROR_TIMEOUT; break; }
        auto& frame = stack.back();
        if (!frame.first && !FindNextFileW(frame.find, &frame.data)) {
            DWORD err = GetLastError();
            if (err != ERROR_NO_MORE_FILES) out.error = err;
            FindClose(frame.find); stack.pop_back(); continue;
        }
        frame.first = false;
        const auto data = frame.data;
        if (!wcscmp(data.cFileName, L".") || !wcscmp(data.cFileName, L"..")) continue;
        if (data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
            if (data.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) {
                if (data.dwReserved0 == IO_REPARSE_TAG_MOUNT_POINT || data.dwReserved0 == IO_REPARSE_TAG_SYMLINK) {
                    ++out.links; continue; // Never follow a directory link, including cycles.
                }
                out.error = ERROR_NOT_SUPPORTED; break; // Cloud/provider directory: unknown, not zero.
            }
            if (data.dwFileAttributes & FILE_ATTRIBUTE_OFFLINE) { out.error = ERROR_NOT_SUPPORTED; break; }
            if (stack.size() >= 256) { out.error = ERROR_DIRECTORY; break; }
            std::wstring sub = frame.path + (frame.path.back() == L'\\' ? L"" : L"\\") + data.cFileName;
            push(sub);
        } else {
            unsigned long long n = (static_cast<unsigned long long>(data.nFileSizeHigh) << 32) | data.nFileSizeLow;
            if (ULLONG_MAX - out.bytes < n) { out.error = ERROR_ARITHMETIC_OVERFLOW; break; }
            out.bytes += n; ++out.files;
        }
    }
    for (auto& frame : stack) FindClose(frame.find);
    return out;
}

class SizeCache {
    static constexpr size_t MaxEntries = 512, MaxQueue = 256, MaxWatches = 8, MaxKeyBytes = 1024 * 1024;
    struct Entry {
        std::wstring path;
        ScanResult result;
        ULONGLONG accessed = 0, finished = 0, generation = 0;
        bool queued = false, ready = false;
    };
    struct Watch {
        std::wstring path;
        HANDLE dir = INVALID_HANDLE_VALUE;
        OVERLAPPED ov{};
        alignas(DWORD) BYTE buffer[32768];
        ULONGLONG used = 0;
        ~Watch() {
            if (dir != INVALID_HANDLE_VALUE) {
                CancelIoEx(dir, &ov);
                DWORD n; GetOverlappedResult(dir, &ov, &n, TRUE);
                CloseHandle(dir);
            }
            if (ov.hEvent) CloseHandle(ov.hEvent);
        }
        bool Arm() {
            ResetEvent(ov.hEvent);
            return ReadDirectoryChangesW(dir, buffer, sizeof(buffer), TRUE,
                FILE_NOTIFY_CHANGE_FILE_NAME | FILE_NOTIFY_CHANGE_DIR_NAME | FILE_NOTIFY_CHANGE_SIZE | FILE_NOTIFY_CHANGE_LAST_WRITE,
                nullptr, &ov, nullptr) != FALSE;
        }
    };
    std::mutex mutex;
    std::vector<std::shared_ptr<Entry>> entries;
    std::deque<std::shared_ptr<Entry>> queue;
    std::vector<std::unique_ptr<Watch>> watches; // Owned solely by the worker.
    std::set<std::wstring> notifyPaths;
    size_t keyBytes = 0;
    HANDLE stop = nullptr, wake = nullptr, worker = nullptr;
    std::atomic<unsigned long long> scanCount{0}, hits{0}, errors{0}, invalidations{0}, files{0};
    std::atomic<unsigned> watchCount{0}, active{0};
    std::atomic<bool> workerFailed{false};
    bool enabled = false;
    ULONGLONG lastFlush = 0;

    bool HasWatch(const std::wstring& path) {
        for (auto& w : watches) if (Within(path, w->path)) { w->used = GetTickCount64(); return true; }
        return false;
    }
    void EnsureWatch(const std::wstring& path) {
        std::wstring parent = ParentPath(path);
        if (parent.empty() || HasWatch(parent)) return;
        if (watches.size() == MaxWatches) {
            auto it = std::min_element(watches.begin(), watches.end(), [](auto& a, auto& b) { return a->used < b->used; });
            watches.erase(it);
        }
        auto w = std::make_unique<Watch>(); w->path = parent; w->used = GetTickCount64();
        w->ov.hEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
        w->dir = CreateFileW(LongPath(parent).c_str(), FILE_LIST_DIRECTORY,
            FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING,
            FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OVERLAPPED, nullptr);
        if (w->dir != INVALID_HANDLE_VALUE && w->ov.hEvent && w->Arm()) watches.push_back(std::move(w));
        watchCount = (unsigned)watches.size();
    }
    void Invalidate(const std::wstring& path) {
        std::lock_guard<std::mutex> lock(mutex);
        for (auto& entry : entries) {
            if (Within(path, entry->path) || Within(entry->path, path)) {
                ++entry->generation; entry->ready = false; entry->finished = 0;
                notifyPaths.insert(entry->path); ++invalidations;
            }
        }
    }
    void ReadChanges() {
        for (size_t i = 0; i < watches.size();) {
            auto& w = watches[i];
            if (WaitForSingleObject(w->ov.hEvent, 0) != WAIT_OBJECT_0) { ++i; continue; }
            DWORD n = 0;
            bool ok = GetOverlappedResult(w->dir, &w->ov, &n, FALSE) != FALSE;
            if (!ok || !n) Invalidate(w->path); // Buffer overflow also invalidates, never silently loses a change.
            else {
                DWORD offset = 0;
                while (offset + FIELD_OFFSET(FILE_NOTIFY_INFORMATION, FileName) <= n) {
                    auto info = reinterpret_cast<FILE_NOTIFY_INFORMATION*>(w->buffer + offset);
                    if (offset + FIELD_OFFSET(FILE_NOTIFY_INFORMATION, FileName) + info->FileNameLength > n) { Invalidate(w->path); break; }
                    std::wstring changed = w->path + (w->path.back() == L'\\' ? L"" : L"\\") +
                        std::wstring(info->FileName, info->FileNameLength / sizeof(wchar_t));
                    Invalidate(changed);
                    if (!info->NextEntryOffset) break;
                    offset += info->NextEntryOffset;
                }
            }
            if (!ok || !w->Arm()) { watches.erase(watches.begin() + i); watchCount = (unsigned)watches.size(); }
            else ++i;
        }
    }
    void Flush(bool force) {
        if (notifyPaths.empty() || (!force && GetTickCount64() - lastFlush < 500)) return;
        std::set<std::wstring> parents;
        for (const auto& path : notifyPaths) {
            SHChangeNotify(SHCNE_UPDATEITEM, SHCNF_PATHW | SHCNF_FLUSHNOWAIT, path.c_str(), nullptr);
            parents.insert(ParentPath(path));
        }
        for (const auto& path : parents) if (!path.empty())
            SHChangeNotify(SHCNE_UPDATEDIR, SHCNF_PATHW | SHCNF_FLUSHNOWAIT, path.c_str(), nullptr);
        notifyPaths.clear(); lastFlush = GetTickCount64();
    }
    static DWORD WINAPI ThreadMain(void* arg) {
        auto owner = static_cast<SizeCache*>(arg);
        try { owner->Run(); }
        catch (...) { owner->workerFailed = true; owner->active = 0; ++owner->errors; }
        // A resource failure must never unwind out of a worker into Explorer.
        // Totals stay unknown until the user pauses and resumes the helper.
        return 0;
    }
    void Run() {
        CoInitializeEx(nullptr, COINIT_MULTITHREADED);
        SetThreadPriority(GetCurrentThread(), THREAD_MODE_BACKGROUND_BEGIN);
        for (;;) {
            if (WaitForSingleObject(stop, 0) == WAIT_OBJECT_0) break;
            ReadChanges();
            std::shared_ptr<Entry> item;
            ULONGLONG generation = 0;
            {
                std::lock_guard<std::mutex> lock(mutex);
                if (!queue.empty()) { item = queue.front(); queue.pop_front(); generation = item->generation; }
            }
            if (item) {
                EnsureWatch(item->path); active = 1;
                auto result = ScanFolder(item->path, stop);
                active = 0; ++scanCount; files += result.files;
                if (result.error) ++errors;
                ReadChanges(); // A write during enumeration makes that total stale.
                {
                    std::lock_guard<std::mutex> lock(mutex);
                    item->queued = false;
                    if (item->generation == generation) {
                        item->result = result; item->ready = true; item->finished = GetTickCount64();
                    }
                }
                notifyPaths.insert(item->path); Flush(false); continue;
            }
            Flush(true);
            HANDLE waiters[MAXIMUM_WAIT_OBJECTS] = {stop, wake};
            DWORD count = 2;
            for (auto& w : watches) waiters[count++] = w->ov.hEvent;
            WaitForMultipleObjects(count, waiters, FALSE, INFINITE);
        }
        watches.clear(); watchCount = 0; notifyPaths.clear();
        SetThreadPriority(GetCurrentThread(), THREAD_MODE_BACKGROUND_END);
        CoUninitialize();
    }
public:
    bool Start() {
        std::lock_guard<std::mutex> lock(mutex);
        if (enabled) return true;
        workerFailed = false;
        stop = CreateEventW(nullptr, TRUE, FALSE, nullptr);
        wake = CreateEventW(nullptr, FALSE, FALSE, nullptr);
        if (!stop || !wake) { if (stop) CloseHandle(stop); if (wake) CloseHandle(wake); stop = wake = nullptr; return false; }
        enabled = true;
        worker = CreateThread(nullptr, 0, ThreadMain, this, 0, nullptr);
        if (!worker) { enabled = false; CloseHandle(stop); CloseHandle(wake); stop = wake = nullptr; return false; }
        return true;
    }
    void Stop() {
        {
            std::lock_guard<std::mutex> lock(mutex);
            if (!enabled) return;
            enabled = false; SetEvent(stop);
        }
        WaitForSingleObject(worker, INFINITE);
        std::lock_guard<std::mutex> lock(mutex);
        CloseHandle(worker); CloseHandle(stop); CloseHandle(wake); worker = stop = wake = nullptr;
        entries.clear(); queue.clear(); keyBytes = 0;
    }
    std::optional<unsigned long long> Query(const std::wstring& raw) {
        auto path = PlainPath(raw);
        if (!LocalPath(path)) return {};
        std::lock_guard<std::mutex> lock(mutex);
        if (!enabled || workerFailed) return {};
        ULONGLONG now = GetTickCount64();
        std::shared_ptr<Entry> item;
        for (auto& e : entries) if (SamePath(e->path, path)) { item = e; break; }
        if (!item) {
            size_t bytes = (path.size() + 1) * sizeof(wchar_t);
            while (entries.size() >= MaxEntries || keyBytes + bytes > MaxKeyBytes) {
                auto it = entries.end();
                for (auto i = entries.begin(); i != entries.end(); ++i)
                    if (!(*i)->queued && (it == entries.end() || (*i)->accessed < (*it)->accessed)) it = i;
                if (it == entries.end()) return {};
                keyBytes -= ((*it)->path.size() + 1) * sizeof(wchar_t); entries.erase(it);
            }
            item = std::make_shared<Entry>(); item->path = path; entries.push_back(item); keyBytes += bytes;
        }
        // A cache revisit after a minute always revalidates, including evicted/failed watchers.
        // Continuous views are invalidated immediately by filesystem notifications.
        if (now - item->finished > 60000 && item->ready) item->ready = false;
        item->accessed = now;
        if (item->ready) {
            if (!item->result.error) { ++hits; return item->result.bytes; }
            if (now - item->finished < 60000) return {};
            item->ready = false;
        }
        if (!item->queued && queue.size() < MaxQueue) { item->queued = true; queue.push_back(item); SetEvent(wake); }
        return {};
    }
    void Stats(LiteStats* out) {
        std::lock_guard<std::mutex> lock(mutex);
        *out = {scanCount.load(), hits.load(), errors.load(), invalidations.load(), files.load(),
            (unsigned)entries.size(), (unsigned)queue.size(), watchCount.load(), active.load()};
    }
};
