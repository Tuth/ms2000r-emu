// MAC-5: several MS2000R CLAP instances in one host process - the CLAP counterpart of ms2k_vst3_shim.cpp.
//
// The emulator core keeps function-local statics and globals, so one copy of the plugin binary holds one machine.
// The host loads this small module as the .clap; the JUCE plugin (clap-juce-extensions) travels next to it as
// MS2000R_engine (macOS: <bundle>/Contents/Resources/MS2000R_engine; Windows: MS2000R_engine.dll beside the .clap).
// The first machine runs from that file; every further one from a private copy in $TMPDIR/MS2000R_clap_engines
// (%TEMP%\MS2000R_clap_engines), dlopen()ed / LoadLibrary()ed - a different file is a different image: its own statics,
// its own JUCE.
//
// CLAP makes this simpler than VST3/AU: one clap_entry serves every instance through factory->create_plugin(), and the
// JUCE wrapper constructs the processor inside that call, so ms2k_liveProcessors() is current as soon as it returns.
// create_plugin goes to a copy whose machine is not in use (ms2k_liveProcessors() == 0), else to a new copy. The
// returned clap_plugin_t belongs to that copy, so the host talks to the engine's own objects from then on.
// Copies stay loaded until the last deinit (a released instance's copy is reused by the next one).
#ifdef _WIN32
#include <windows.h>
#else
#include <dlfcn.h>
#include <signal.h>
#include <unistd.h>
#include <cerrno>
#include <filesystem>
#endif
#include <cstdlib>
#include <cstring>
#include <deque>
#include <iterator>
#include <mutex>
#include <string>

#include <clap/clap.h>

namespace {

typedef int (*LiveProc)();

// ---------------------------------------------------------------- platform
#ifdef _WIN32
using ModuleHandle = HMODULE;
using PathStr = std::wstring;

std::wstring modulePath()
{
    HMODULE self = nullptr;
    GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                       reinterpret_cast<LPCWSTR>(&modulePath), &self);
    wchar_t buf[MAX_PATH * 4] = {};
    GetModuleFileNameW(self, buf, static_cast<DWORD>(std::size(buf)));
    return buf;
}

std::wstring parentDir(const std::wstring& p)
{
    const auto i = p.find_last_of(L"\\/");
    return i == std::wstring::npos ? std::wstring() : p.substr(0, i);
}

// <folder>\MS2000R.clap -> <folder>\MS2000R_engine.dll
std::wstring enginePath() { return parentDir(modulePath()) + L"\\MS2000R_engine.dll"; }

std::wstring tempDir()
{
    wchar_t buf[MAX_PATH] = {};
    GetTempPathW(MAX_PATH, buf);
    std::wstring d = std::wstring(buf) + L"MS2000R_clap_engines";
    CreateDirectoryW(d.c_str(), nullptr);
    return d;
}

bool processAlive(unsigned long pid)
{
    HANDLE p = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    if (!p) return GetLastError() == ERROR_ACCESS_DENIED;
    DWORD code = 0;
    const bool alive = GetExitCodeProcess(p, &code) && code == STILL_ACTIVE;
    CloseHandle(p);
    return alive;
}

// copies left behind by a process that is gone (engine_<pid>_<n>.dll). Copies of a running process stay: hosts that
// scan or sandbox plugins load the .clap in several processes at once, and a copy made but not loaded yet could go.
void cleanStaleCopies()
{
    const auto dir = tempDir();
    WIN32_FIND_DATAW fd;
    HANDLE h = FindFirstFileW((dir + L"\\engine_*.dll").c_str(), &fd);
    if (h == INVALID_HANDLE_VALUE) return;
    do {
        const unsigned long pid = std::wcstoul(fd.cFileName + 7, nullptr, 10);
        if (pid == GetCurrentProcessId() || !processAlive(pid)) DeleteFileW((dir + L"\\" + fd.cFileName).c_str());
    } while (FindNextFileW(h, &fd));
    FindClose(h);
}

void* symbol(ModuleHandle m, const char* name) { return reinterpret_cast<void*>(GetProcAddress(m, name)); }
void closeModule(ModuleHandle m) { FreeLibrary(m); }
void deleteFile(const PathStr& p) { DeleteFileW(p.c_str()); }
ModuleHandle openModule(const PathStr& p) { return LoadLibraryExW(p.c_str(), nullptr, LOAD_WITH_ALTERED_SEARCH_PATH); }
void setBinaryEnv(const PathStr& self)
{
    SetEnvironmentVariableW(L"MS2K_PLUGIN_BINARY", self.c_str());
    _wputenv_s(L"MS2K_PLUGIN_BINARY", self.c_str());
}
PathStr copyPath(unsigned n) { return tempDir() + L"\\engine_" + std::to_wstring(GetCurrentProcessId()) + L"_" + std::to_wstring(n) + L".dll"; }
bool copyEngine(const PathStr& from, const PathStr& to) { return CopyFileW(from.c_str(), to.c_str(), FALSE) != 0; }
#else
using ModuleHandle = void*;
using PathStr = std::string;

PathStr modulePath()
{
    Dl_info info{};
    return dladdr(reinterpret_cast<const void*>(&modulePath), &info) && info.dli_fname ? PathStr(info.dli_fname) : PathStr();
}

PathStr parentDir(const PathStr& p)
{
    const auto i = p.find_last_of('/');
    return i == PathStr::npos ? PathStr() : p.substr(0, i);
}

// <bundle>/Contents/MacOS/MS2000R -> <bundle>/Contents/Resources/MS2000R_engine
PathStr enginePath() { return parentDir(parentDir(modulePath())) + "/Resources/MS2000R_engine"; }

PathStr tempDir()
{
    const char* t = std::getenv("TMPDIR");
    PathStr d = PathStr(t && *t ? t : "/tmp/");
    if (d.back() != '/') d += '/';
    d += "MS2000R_clap_engines";
    std::error_code ec; std::filesystem::create_directories(d, ec);
    return d;
}

// copies left behind by a process that is gone (engine_<pid>_<n>). Copies of a running process stay: hosts that scan
// or sandbox plugins load the .clap in several processes at once, and a copy made but not dlopen()ed yet could go.
void cleanStaleCopies()
{
    std::error_code ec;
    for (const auto& e : std::filesystem::directory_iterator(tempDir(), ec)) {
        const auto name = e.path().filename().string();
        if (name.rfind("engine_", 0) != 0) continue;
        const pid_t pid = pid_t(std::strtol(name.c_str() + 7, nullptr, 10));
        if (pid == getpid() || pid <= 0 || (kill(pid, 0) != 0 && errno == ESRCH)) std::filesystem::remove(e.path(), ec);
    }
}

void* symbol(ModuleHandle m, const char* name) { return dlsym(m, name); }
void closeModule(ModuleHandle m) { dlclose(m); }
void deleteFile(const PathStr& p) { std::error_code ec; std::filesystem::remove(p, ec); }
ModuleHandle openModule(const PathStr& p) { return dlopen(p.c_str(), RTLD_NOW | RTLD_LOCAL); }
void setBinaryEnv(const PathStr& self) { setenv("MS2K_PLUGIN_BINARY", self.c_str(), 1); }
PathStr copyPath(unsigned n) { return tempDir() + "/engine_" + std::to_string(getpid()) + "_" + std::to_string(n); }
bool copyEngine(const PathStr& from, const PathStr& to)
{
    std::error_code ec;
    return std::filesystem::copy_file(from, to, std::filesystem::copy_options::overwrite_existing, ec);
}
#endif

// ---------------------------------------------------------------- shared
struct EngineCopy {
    ModuleHandle module = nullptr;
    const clap_plugin_entry_t* entry = nullptr;
    const clap_plugin_factory_t* factory = nullptr;
    LiveProc live = nullptr;
    PathStr path;
    bool temporary = false;
};

std::mutex g_lock;
std::deque<EngineCopy> g_copies;   // a deque: pointers to its elements survive push_back
std::string g_pluginPath;          // what the host passed to init - handed on to every engine copy
int g_initCount = 0;               // init/deinit may be called more than once (clap/entry.h)

bool loadCopy(EngineCopy& c)
{
    c.module = openModule(c.path);
    if (!c.module) return false;
    // from this copy's own handle: a global lookup would find this module's clap_entry
    c.entry = static_cast<const clap_plugin_entry_t*>(symbol(c.module, "clap_entry"));
    c.live = reinterpret_cast<LiveProc>(symbol(c.module, "ms2k_liveProcessors"));
    if (c.entry && clap_version_is_compatible(c.entry->clap_version) && c.entry->init(g_pluginPath.c_str())) {
        c.factory = static_cast<const clap_plugin_factory_t*>(c.entry->get_factory(CLAP_PLUGIN_FACTORY_ID));
        if (c.factory) return true;
        c.entry->deinit();
    }
    closeModule(c.module);
    c.module = nullptr;
    c.entry = nullptr;
    return false;
}

EngineCopy* first()   // the engine file shipped with the plugin; it also answers every descriptor question
{
    if (!g_copies.empty()) return g_copies[0].module ? &g_copies[0] : nullptr;
    // the engine finds the MS2000 folder above the plugin binary (PUBLIC-1); a copy in the temp folder must look above this file
    setBinaryEnv(modulePath());
    cleanStaleCopies();
    EngineCopy c;
    c.path = enginePath();
    g_copies.push_back(c);
    return loadCopy(g_copies[0]) ? &g_copies[0] : nullptr;
}

EngineCopy* newCopy()
{
    static unsigned counter = 0;
    EngineCopy c;
    c.path = copyPath(++counter);
    c.temporary = true;
    if (!copyEngine(g_copies[0].path, c.path)) return nullptr;
    g_copies.push_back(c);
    if (!loadCopy(g_copies.back())) { deleteFile(c.path); g_copies.pop_back(); return nullptr; }
    return &g_copies.back();
}

bool isFree(const EngineCopy& c) { return c.module && c.live && c.live() == 0; }

void unloadAll()
{
    for (auto it = g_copies.rbegin(); it != g_copies.rend(); ++it) {
        if (!it->module) continue;
        it->entry->deinit();
        closeModule(it->module);
        if (it->temporary) deleteFile(it->path);
    }
    g_copies.clear();
}

// ---------------------------------------------------------------- factory
uint32_t CLAP_ABI factoryCount(const clap_plugin_factory_t*)
{
    std::lock_guard<std::mutex> l(g_lock);
    auto* c = first();
    return c ? c->factory->get_plugin_count(c->factory) : 0;
}

const clap_plugin_descriptor_t* CLAP_ABI factoryDescriptor(const clap_plugin_factory_t*, uint32_t index)
{
    std::lock_guard<std::mutex> l(g_lock);
    auto* c = first();
    return c ? c->factory->get_plugin_descriptor(c->factory, index) : nullptr;
}

const clap_plugin_t* CLAP_ABI factoryCreate(const clap_plugin_factory_t*, const clap_host_t* host, const char* pluginId)
{
    std::lock_guard<std::mutex> l(g_lock);
    if (!first()) return nullptr;
    EngineCopy* target = nullptr;
    for (auto& c : g_copies) if (isFree(c)) { target = &c; break; }
    if (!target) target = newCopy();
    if (!target) return nullptr;
    return target->factory->create_plugin(target->factory, host, pluginId);
}

const clap_plugin_factory_t g_factory = { factoryCount, factoryDescriptor, factoryCreate };

// ---------------------------------------------------------------- entry
bool CLAP_ABI entryInit(const char* pluginPath)
{
    std::lock_guard<std::mutex> l(g_lock);
    if (g_initCount > 0) { ++g_initCount; return true; }
    g_pluginPath = pluginPath ? pluginPath : "";
    if (!first()) { g_copies.clear(); return false; }
    g_initCount = 1;
    return true;
}

void CLAP_ABI entryDeinit()
{
    std::lock_guard<std::mutex> l(g_lock);
    if (g_initCount == 0 || --g_initCount > 0) return;
    unloadAll();
}

const void* CLAP_ABI entryGetFactory(const char* factoryId)
{
    return std::strcmp(factoryId, CLAP_PLUGIN_FACTORY_ID) == 0 ? &g_factory : nullptr;
}

} // namespace

extern "C" CLAP_EXPORT const clap_plugin_entry_t clap_entry = { CLAP_VERSION_INIT, entryInit, entryDeinit, entryGetFactory };
