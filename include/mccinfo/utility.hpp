#pragma once

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <krabs/krabs.hpp> //#include <windows.h>
#include <tlhelp32.h>
#undef NOMINMAX

#include <psapi.h>
#include <gdiplus.h>

#include <cstdint>
#pragma warning(push)
#pragma warning(disable : 4068)
#pragma warning(disable : 4996)
#include <cometa.hpp>
#include <cident.h>
#pragma warning(pop)
#include <string>
#include <vector>
#include <fstream>
#include <optional>
#include <filesystem>
#include <span>
#include <variant>
#include <tuple>
#include <stdexcept>
#include <functional>
#include <any>
#include <type_traits>
#include <memory>
#include <stdexcept>
#include <ctime>
#include <iostream>
#include <ostream>

namespace mccinfo {
namespace utility {
class atomic_mutex {
  public:
    void lock() {
        while (flag.exchange(true, std::memory_order_relaxed))
            ;
        std::atomic_thread_fence(std::memory_order_acquire);
    }

    void unlock() {
        std::atomic_thread_fence(std::memory_order_release);
        flag.store(false, std::memory_order_relaxed);
    }

  private:
    std::atomic<bool> flag{false};
};

class atomic_guard {
  public:
    atomic_guard(atomic_mutex &mutex) : m_Mutex(mutex) {
        m_Mutex.lock();
    }
    ~atomic_guard() {
        m_Mutex.unlock();
    }

  private:
    atomic_mutex &m_Mutex;
};

template<auto Id>
struct counter {
    using tag = counter;

    struct generator {
        friend consteval auto is_defined(tag)
        { return true; }
    };
    friend consteval auto is_defined(tag);

    template<typename Tag = tag, auto = is_defined(Tag{})>
    static consteval auto exists(auto)
    { return true; }

    static consteval auto exists(...)
    { return generator(), false; }
};

template<auto Id = int{}, auto = []{}>
consteval auto unique_id() {
    if constexpr (counter<Id>::exists(Id)) {
        return unique_id<Id + 1>();
    } else {
        return Id;
    }
}

template <typename T>
constexpr const char* func_sig() {
    #ifdef _MSC_VER
    return __FUNCSIG__;
    #else
    return __PRETTY_FUNCTION__;
    #endif
}

constexpr auto parse_type(std::string_view sv) {
    auto start = sv.find_last_of('<');
    auto end = sv.find_first_of('>');

    if (start == std::string_view::npos || end == std::string_view::npos || end < start) {
        return sv;
    }

    return sv.substr(start, end - start + 1);
}

constexpr auto remove_namespaces(std::string_view sv) {
    auto last = sv.find_last_of(':');
    if (last == std::string_view::npos) {
        return sv;
    }
    return sv.substr(last + 1, sv.size() - (last + 1) - 1);
}

template <typename T>
constexpr auto make_type_name() {
    return parse_type(func_sig<T>());
}

template <typename T>
constexpr auto make_type_name_minimal() {
    return remove_namespaces(parse_type(func_sig<T>()));
}

unsigned constexpr const_hash(std::string_view sv) {
    return *sv.data() ?
        static_cast<unsigned int>(*sv.data()) + 33 * const_hash(sv.data() + 1) :
        5381;
}

template <typename T> struct type_hash {
    static constexpr unsigned int hash{const_hash(make_type_name<T>())};
    static constexpr std::string_view name{make_type_name<T>()};
    static constexpr std::string_view name_minimal{make_type_name_minimal<T>()};
};

template <typename evt> static constexpr auto id = utility::type_hash<evt>::hash;

inline std::optional<std::wstring> ConvertBytesToWString(const std::string &bytes) {
    int required_size =
        MultiByteToWideChar(CP_UTF8, 0, bytes.data(), static_cast<int>(bytes.size()), nullptr, 0);
    if (required_size == 0)
        return std::nullopt;

    std::wstring result(static_cast<size_t>(required_size), L'\0');
    int converted = MultiByteToWideChar(CP_UTF8, 0, bytes.data(), static_cast<int>(bytes.size()),
                                        &result[0], required_size);
    if (converted == 0)
        return std::nullopt;

    return result;
}

inline std::optional<std::string> ConvertWStringToBytes(const std::wstring &wstr) {
    int required_size = WideCharToMultiByte(CP_UTF8, 0, wstr.data(), static_cast<int>(wstr.size()),
                                            nullptr, 0, nullptr, nullptr);
    if (required_size == 0)
        return std::nullopt;

    std::string result(static_cast<size_t>(required_size), '\0');
    int converted = WideCharToMultiByte(CP_UTF8, 0, wstr.data(), static_cast<int>(wstr.size()),
                                        &result[0], required_size, nullptr, nullptr);
    if (converted == 0)
        return std::nullopt;

    return result;
}

inline std::optional<std::vector<char>> SlurpFile(const std::filesystem::path path) {
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    std::streamsize file_size = file.tellg();
    file.seekg(0, std::ios::beg);

    std::vector<char> buffer(file_size);
    if (file.read(buffer.data(), file_size))
        return buffer;
    else
        return std::nullopt;
}

inline std::optional<std::filesystem::path> ExpandPath(const std::filesystem::path &path) {
    std::wstring dst;
    dst.resize(MAX_PATH);
    DWORD ret = ::ExpandEnvironmentStringsW(path.wstring().c_str(), dst.data(), MAX_PATH);
    if (ret == 0)
        return std::nullopt;
    if (!std::filesystem::exists(dst))
        return std::nullopt;
    return std::filesystem::absolute(dst);
}

inline bool FileHasOpenHandle(const std::filesystem::path& file_path) {
    HANDLE hFile = CreateFileW(file_path.generic_wstring().c_str(), // name of the write
                               GENERIC_WRITE,         // open for writing
                               0,                     // *** do not share ***
                               NULL,                  // default security
                               OPEN_EXISTING,         // create new file only
                               FILE_ATTRIBUTE_NORMAL, // normal file
                               NULL);                 // no attr. template

    if ((hFile == INVALID_HANDLE_VALUE) && (GetLastError() == ERROR_SHARING_VIOLATION)) {
        CloseHandle(hFile);
        return true;
    } else {
        CloseHandle(hFile);
        return false;
    }
}

std::optional<DWORD> GetProcessIDFromName(const std::wstring &process_name) {
    HANDLE hSnapshot;
    PROCESSENTRY32 pe;
    DWORD pid = 0;
    BOOL hResult;

    hSnapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (INVALID_HANDLE_VALUE == hSnapshot)
        return std::nullopt;

    pe.dwSize = sizeof(PROCESSENTRY32);
    hResult = Process32First(hSnapshot, &pe);

    while (hResult) {
        if (wcscmp(process_name.c_str(), pe.szExeFile) == 0) {
            pid = pe.th32ProcessID;
            CloseHandle(hSnapshot);
            return pid;
        }
        hResult = Process32Next(hSnapshot, &pe);
    }
    CloseHandle(hSnapshot);
    return std::nullopt;
}

std::optional<DWORD> GetParentProcessID(DWORD pid) {
    HANDLE hSnapshot;
    PROCESSENTRY32 pe;
    DWORD ppid = 0;
    BOOL hResult;

    hSnapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (INVALID_HANDLE_VALUE == hSnapshot)
        return std::nullopt;

    pe.dwSize = sizeof(PROCESSENTRY32);
    hResult = Process32First(hSnapshot, &pe);

    while (hResult) {
        if (pid == pe.th32ProcessID) {
            ppid = pe.th32ParentProcessID;
            CloseHandle(hSnapshot);
            return ppid;
        }
        hResult = Process32Next(hSnapshot, &pe);
    }
    CloseHandle(hSnapshot);
    return std::nullopt;
}

bool IsThreadInProcess(DWORD threadID, DWORD processID) {
    // Take a snapshot of all running threads
    HANDLE hThreadSnap = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);
    if (hThreadSnap == INVALID_HANDLE_VALUE) {
        return false;
    }

    THREADENTRY32 te32;
    te32.dwSize = sizeof(THREADENTRY32);

    // Get the information about the first thread
    if (!Thread32First(hThreadSnap, &te32)) {
        CloseHandle(hThreadSnap);
        return false;
    }

    // Now walk the thread list of the system
    do {
        if (te32.th32OwnerProcessID == processID && te32.th32ThreadID == threadID) {
            // Found a thread with the given thread ID that belongs to the process with the given
            // process ID
            CloseHandle(hThreadSnap);
            return true;
        }
    } while (Thread32Next(hThreadSnap, &te32));

    CloseHandle(hThreadSnap);
    return false;
}
std::optional<DWORD> GetProcessIDFromThreadID(DWORD threadID) {
    HANDLE threadHandle = OpenThread(THREAD_ALL_ACCESS, FALSE, threadID);
    if (threadHandle == NULL) return std::nullopt;
    else {
        return GetProcessIdOfThread(threadHandle);
    }
}

std::optional<std::string> ModuleBaseNameFromProcessID(DWORD processID) {
    std::optional<std::string> ret;
    HANDLE handle = OpenProcess(
        PROCESS_QUERY_LIMITED_INFORMATION,
        FALSE,
        processID /* This is the PID, you can find one from windows task manager */
    );
    if (handle) {
        DWORD buffSize = 1024;
        CHAR buffer[1024];
        if (QueryFullProcessImageNameA(handle, 0, buffer, &buffSize))
        {
            ret = buffer;
        }
        else
        {
            ret = std::nullopt;
        }
        CloseHandle(handle);
    } 
    else {
        ret = std::nullopt;
    }
    return ret;
}

std::optional<std::string> ModuleBaseNameFromThreadID(DWORD threadID) {
    auto ret = GetProcessIDFromThreadID(threadID);
    if (ret.has_value()) {
        return ModuleBaseNameFromProcessID(ret.value());
    } else {
        return std::nullopt;
    }
}

inline std::vector<std::wstring> GetLoadedModulesFromProcessID(DWORD processID) {
    HMODULE hMods[1024];
    HANDLE hProcess;
    DWORD cbNeeded;

    hProcess = OpenProcess(PROCESS_QUERY_INFORMATION | PROCESS_VM_READ, FALSE, processID);

    if (hProcess ==  NULL)
        return {};

    std::vector<std::wstring> modules;

    if (EnumProcessModules(hProcess, hMods, sizeof(hMods), &cbNeeded)) {
        for (size_t i = 0; i < (cbNeeded / sizeof(HMODULE)); ++i) {
            WCHAR szModName[MAX_PATH];

            // Get the full path to the module's file.

            if (GetModuleFileNameExW(hProcess, hMods[i], szModName,
                                    sizeof(szModName) / sizeof(WCHAR))) {
                modules.push_back(std::wstring(szModName));
            }
        }
    }
    CloseHandle(hProcess);

    return modules;
}

inline std::optional<std::wstring> GetModuleFullPathnameW(HMODULE hMod) {
    std::vector<wchar_t> filename(MAX_PATH);
    std::vector<wchar_t> pathname(MAX_PATH);
    DWORD success = GetModuleFileNameW(hMod, filename.data(), (DWORD)filename.size());
    if (success) {
        success = GetFullPathNameW(filename.data(), (DWORD)filename.size(), pathname.data(), NULL);
        if (success) {
            return std::wstring(pathname.data(), pathname.size());
        }
    }
    return std::nullopt;
}

inline int GetImageEncoderClsid(const WCHAR *format, CLSID *pClsid) {
    UINT num = 0;
    UINT size = 0;

    Gdiplus::ImageCodecInfo *pImageCodecInfo = NULL;

    Gdiplus::GetImageEncodersSize(&num, &size);
    if (size == 0)
        return -1;

    pImageCodecInfo = (Gdiplus::ImageCodecInfo *)(malloc(size));
    if (pImageCodecInfo == NULL)
        return -1;

    GetImageEncoders(num, size, pImageCodecInfo);
    for (UINT j = 0; j < num; ++j) {
        if (wcscmp(pImageCodecInfo[j].MimeType, format) == 0) {
            *pClsid = pImageCodecInfo[j].Clsid;
            free(pImageCodecInfo);
            return j;
        }
    }
    free(pImageCodecInfo);
    return 0;
}

inline void ScreenCapture(RECT sr, const std::filesystem::path &save_to) {
    Gdiplus::GdiplusStartupInput gdiplusStartupInput;
    ULONG_PTR gdiplusToken;

    Gdiplus::GdiplusStartup(&gdiplusToken, &gdiplusStartupInput, NULL);

    { 
        HDC scrdc, memdc;
        HBITMAP membit;

        scrdc = ::GetDC(0);

        int w = abs(sr.right - sr.left);
        int h = abs(sr.bottom - sr.top);

        memdc = CreateCompatibleDC(scrdc);
        membit = CreateCompatibleBitmap(scrdc, w, h);

        HBITMAP hOldBitmap = (HBITMAP)SelectObject(memdc, membit);

        BitBlt(memdc, 0, 0, w, h, scrdc, sr.left, sr.top, SRCCOPY);

        Gdiplus::Bitmap bitmap(membit, NULL);
        CLSID clsid;
        
        GetImageEncoderClsid(L"image/jpeg", &clsid);
        bitmap.Save(save_to.generic_wstring().c_str(), &clsid, NULL);

        DeleteObject(memdc);
        DeleteObject(membit);
        ::ReleaseDC(0, scrdc);
    }


    Gdiplus::GdiplusShutdown(gdiplusToken);
}

inline std::string CurrentTimestampISO() {
    time_t now;
    time(&now);

    struct tm tm_buf;
    gmtime_s(&tm_buf, &now);

    char buf[sizeof "2011-10-08T07:07:09Z"];
    strftime(buf, sizeof buf, "%FT%TZ", &tm_buf);

    std::string out_str(buf, sizeof buf - 1);
    return out_str;
}

inline bool PathContains(const std::filesystem::path& path, const std::string& str) {
    return path.generic_string().find(str) != std::string::npos;
}

inline bool PathContainsW(const std::filesystem::path &path, const std::wstring &str) {
    return path.generic_wstring().find(str) != std::wstring::npos;
}

inline bool by_last_file_write_time(const std::filesystem::path &left,
                                    const std::filesystem::path &right) {
    return std::filesystem::last_write_time(left) < std::filesystem::last_write_time(right);
}

inline void PrintTraceEvent(std::wostringstream &woss, const EVENT_RECORD &record,
                            const krabs::trace_context &trace_context) {
    woss << L"\t";
    krabs::schema schema(record, trace_context.schema_locator);
    krabs::parser parser(schema);

    try {
        if (schema.event_opcode() != 11) { // Prevent Process_Terminate (Event Version(2))
            if (schema.event_opcode() != 64) {
                if (schema.event_opcode() != 67) {
                    if ((schema.event_opcode() == 3) &&
                        ((std::wstring(schema.task_name()).find(L"Process") ==
                          std::wstring::npos))) {

                        std::wstring imagefilename = parser.parse<std::wstring>(L"FileName");
                        std::uint32_t pid = parser.parse<std::uint32_t>(L"ProcessId");

                        woss << schema.task_name() << L"_" << schema.opcode_name();
                        woss << L" (" << schema.event_opcode() << L") ";
                        woss << L" ProcessId=" << pid;
                        woss << L" ImageFileName=" << imagefilename;

                    } else {
                        std::string imagefilename = parser.parse<std::string>(L"ImageFileName");
                        std::uint32_t pid = parser.parse<std::uint32_t>(L"ProcessId");
                        woss << schema.task_name() << L"_" << schema.opcode_name();
                        woss << L" (" << schema.event_opcode() << L") ";
                        woss << L" ProcessId=" << pid;
                        auto ws = utility::ConvertBytesToWString(imagefilename);
                        if (ws.has_value())
                            woss << L" ImageFileName=" << ws.value();
                    }
                } else {
                    uint32_t ttid = parser.parse<uint32_t>(L"TTID");
                    uint32_t io_size = parser.parse<uint32_t>(L"IoSize");

                    woss << schema.task_name() << L"_" << schema.opcode_name();
                    woss << L" (" << schema.event_opcode() << L") ";
                    woss << L" pid=" << std::to_wstring(record.EventHeader.ProcessId);
                    woss << L" ttid=" << std::to_wstring(ttid);
                    woss << L" IoSize=" << std::to_wstring(io_size);
                }
            } else {
                std::wstring imagefilename = parser.parse<std::wstring>(L"OpenPath");
                woss << schema.task_name() << L"_" << schema.opcode_name();
                woss << L" (" << schema.event_opcode() << L") ";
                woss << " Path=" << imagefilename;
            }
            woss << std::endl;
        }
    } catch (const std::exception &exc) {
        std::cerr << exc.what();
        throw std::runtime_error("hi :)))))))))))");
    }
}


} // namespace utility
} // namespace mccinfo