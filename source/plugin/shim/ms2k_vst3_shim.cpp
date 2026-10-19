// MULTI-1 (2026-10-02): several MS2000R instances in one host process.
//
// The emulator core keeps function-local statics and globals, so one copy of the plugin binary can hold exactly
// one machine. Instead of auditing ~300 statics, every instance gets its own copy of the binary: this small VST3
// module is what the host loads (MS2000R.vst3), and the real JUCE plugin travels in the bundle as
// Contents/Resources/MS2000R_engine.dll. The first machine runs from that file; every further one from a private
// copy in %TEMP%\MS2000R_engines, loaded with LoadLibrary - its own statics, its own JUCE, nothing shared but the
// process (as with any two different JUCE plugins).
//
// The host only ever talks to the engine's own objects: GetPluginFactory hands out a factory that forwards every
// call to the engine's factory, except createInstance, which first picks the engine copy:
//  - a component (the audio processor) goes to a copy whose machine is not in use (ms2k_liveProcessors() == 0),
//    else to a new copy;
//  - an edit controller goes to the copy whose component was created last and has no controller yet - JUCE's
//    component and controller find each other through a raw pointer, so they must live in the same copy.
// Copies stay loaded until ExitDll (a released instance's copy is reused by the next one).
//
// MAC-4 (macOS): the same scheme on a VST3 bundle - the host loads Contents/MacOS/MS2000R (this module), the JUCE
// plugin is Contents/Resources/MS2000R_engine, further copies go to $TMPDIR/MS2000R_engines and are dlopen()ed (a
// different file is a different image: its own statics, its own JUCE). bundleEntry/bundleExit stand for
// InitDll/ExitDll. Everything below the platform section is shared.
#ifdef _WIN32
#include <windows.h>
#else
#include <CoreFoundation/CoreFoundation.h>
#include <dlfcn.h>
#include <signal.h>
#include <unistd.h>
#include <cerrno>
#include <filesystem>
#endif
#include <atomic>
#include <cstdlib>
#include <cstring>
#include <cwchar>
#include <deque>
#include <iterator>
#include <mutex>
#include <string>
#include <vector>

#include "pluginterfaces/base/ipluginbase.h"
#include "pluginterfaces/vst/ivstcomponent.h"
#include "pluginterfaces/vst/ivsteditcontroller.h"

using namespace Steinberg;

namespace {

typedef IPluginFactory* (PLUGIN_API* GetFactoryProc)();
typedef bool (*InitExitProc)();
typedef int (*LiveProc)();

// ---------------------------------------------------------------- platform
#ifdef _WIN32
using ModuleHandle = HMODULE;
using PathStr = std::wstring;
#else
using ModuleHandle = void*;
using PathStr = std::string;
#endif

struct EngineCopy {
    ModuleHandle module = nullptr;
    IPluginFactory* factory = nullptr;
    IPluginFactory3* factory3 = nullptr;
    LiveProc live = nullptr;
    PathStr path;
    bool temporary = false;
    uint64_t compSerial = 0;   // creation sequence number of the last component made here
    uint64_t ctrlSerial = 0;   // ... and of the last controller
};

std::mutex g_lock;
std::deque<EngineCopy> g_copies;   // a deque: pointers to its elements survive push_back
uint64_t g_serial = 0;
FUnknown* g_hostContext = nullptr;

#ifdef _WIN32
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

// <bundle>\Contents\x86_64-win\MS2000R.vst3 -> <bundle>\Contents\Resources\MS2000R_engine.dll
std::wstring enginePath() { return parentDir(parentDir(modulePath())) + L"\\Resources\\MS2000R_engine.dll"; }

std::wstring tempDir()
{
    wchar_t buf[MAX_PATH] = {};
    GetTempPathW(MAX_PATH, buf);
    std::wstring d = std::wstring(buf) + L"MS2000R_engines";
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
// scan or sandbox plugins load the module in several processes at once, and a copy made but not loaded yet could go.
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
bool callInit(ModuleHandle m) { auto init = reinterpret_cast<InitExitProc>(symbol(m, "InitDll")); return !init || init(); }
void callExit(ModuleHandle m) { if (auto exitDll = reinterpret_cast<InitExitProc>(symbol(m, "ExitDll"))) exitDll(); }
ModuleHandle openModule(const PathStr& p) { return LoadLibraryExW(p.c_str(), nullptr, LOAD_WITH_ALTERED_SEARCH_PATH); }
void setBinaryEnv(const PathStr& self)
{
    SetEnvironmentVariableW(L"MS2K_PLUGIN_BINARY", self.c_str());
    _wputenv_s(L"MS2K_PLUGIN_BINARY", self.c_str());
}
PathStr copyPath(unsigned n) { return tempDir() + L"\\engine_" + std::to_wstring(GetCurrentProcessId()) + L"_" + std::to_wstring(n) + L".dll"; }
bool copyEngine(const PathStr& from, const PathStr& to) { return CopyFileW(from.c_str(), to.c_str(), FALSE) != 0; }
#else
CFBundleRef g_bundle = nullptr;   // this module's bundle, from bundleEntry - handed to every engine copy

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
    d += "MS2000R_engines";
    std::error_code ec; std::filesystem::create_directories(d, ec);
    return d;
}

// copies left behind by a process that is gone (engine_<pid>_<n>). Copies of a running process stay: hosts that scan
// or sandbox plugins load the module in several processes at once, and a copy made but not dlopen()ed yet could go.
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
typedef bool (*BundleEntryProc)(CFBundleRef);
bool callInit(ModuleHandle m) { auto f = reinterpret_cast<BundleEntryProc>(symbol(m, "bundleEntry")); return !f || f(g_bundle); }
void callExit(ModuleHandle m) { if (auto f = reinterpret_cast<InitExitProc>(symbol(m, "bundleExit"))) f(); }
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
bool loadCopy(EngineCopy& c)
{
    c.module = openModule(c.path);
    if (!c.module) return false;
    if (!callInit(c.module)) { closeModule(c.module); c.module = nullptr; return false; }
    auto get = reinterpret_cast<GetFactoryProc>(symbol(c.module, "GetPluginFactory"));
    c.live = reinterpret_cast<LiveProc>(symbol(c.module, "ms2k_liveProcessors"));
    c.factory = get ? get() : nullptr;
    if (!c.factory) { closeModule(c.module); c.module = nullptr; return false; }
    void* f3 = nullptr;
    if (c.factory->queryInterface(IPluginFactory3::iid, &f3) == kResultOk) c.factory3 = static_cast<IPluginFactory3*>(f3);
    if (c.factory3 && g_hostContext) c.factory3->setHostContext(g_hostContext);
    return true;
}

EngineCopy* first()   // the engine file in the bundle; it also answers every class-info question
{
    if (!g_copies.empty()) return g_copies[0].module ? &g_copies[0] : nullptr;
    // the engine finds the MS2000 folder above the plugin binary (PUBLIC-1); a copy in %TEMP% must look above this file
    const auto self = modulePath();
    setBinaryEnv(self);
    cleanStaleCopies();
    EngineCopy c;
    c.path = enginePath();
    g_copies.push_back(c);
    if (!loadCopy(g_copies[0])) return nullptr;
    return &g_copies[0];
}

EngineCopy* newCopy()
{
    static std::atomic<unsigned> counter{ 0 };
    EngineCopy c;
    c.path = copyPath(++counter);
    c.temporary = true;
    if (!copyEngine(g_copies[0].path, c.path)) return nullptr;
    g_copies.push_back(c);
    if (!loadCopy(g_copies.back())) { deleteFile(c.path); g_copies.pop_back(); return nullptr; }
    return &g_copies.back();
}

bool isFree(const EngineCopy& c) { return c.module && c.live && c.live() == 0 && c.ctrlSerial >= c.compSerial; }

class ShimFactory final : public IPluginFactory3 {
public:
    tresult PLUGIN_API queryInterface(const TUID iid, void** obj) override
    {
        if (FUnknownPrivate::iidEqual(iid, FUnknown::iid) || FUnknownPrivate::iidEqual(iid, IPluginFactory::iid) ||
            FUnknownPrivate::iidEqual(iid, IPluginFactory2::iid) || FUnknownPrivate::iidEqual(iid, IPluginFactory3::iid)) {
            addRef(); *obj = this; return kResultOk;
        }
        *obj = nullptr; return kNoInterface;
    }
    uint32 PLUGIN_API addRef() override { return ++m_refs; }
    uint32 PLUGIN_API release() override { return --m_refs; }   // a static object: never deleted

    tresult PLUGIN_API getFactoryInfo(PFactoryInfo* info) override
    { std::lock_guard<std::mutex> l(g_lock); auto* c = first(); return c ? c->factory->getFactoryInfo(info) : kResultFalse; }
    int32 PLUGIN_API countClasses() override
    { std::lock_guard<std::mutex> l(g_lock); auto* c = first(); return c ? c->factory->countClasses() : 0; }
    tresult PLUGIN_API getClassInfo(int32 index, PClassInfo* info) override
    { std::lock_guard<std::mutex> l(g_lock); auto* c = first(); return c ? c->factory->getClassInfo(index, info) : kResultFalse; }
    tresult PLUGIN_API getClassInfo2(int32 index, PClassInfo2* info) override
    { std::lock_guard<std::mutex> l(g_lock); auto* c = first(); return c && c->factory3 ? c->factory3->getClassInfo2(index, info) : kResultFalse; }
    tresult PLUGIN_API getClassInfoUnicode(int32 index, PClassInfoW* info) override
    { std::lock_guard<std::mutex> l(g_lock); auto* c = first(); return c && c->factory3 ? c->factory3->getClassInfoUnicode(index, info) : kResultFalse; }
    tresult PLUGIN_API setHostContext(FUnknown* context) override
    {
        std::lock_guard<std::mutex> l(g_lock);
        g_hostContext = context;
        first();
        for (auto& c : g_copies) if (c.factory3) c.factory3->setHostContext(context);
        return kResultOk;
    }

    tresult PLUGIN_API createInstance(FIDString cid, FIDString iid, void** obj) override
    {
        std::lock_guard<std::mutex> l(g_lock);
        *obj = nullptr;
        auto* c0 = first();
        if (!c0) return kResultFalse;

        const bool controller = isController(*c0, cid);
        EngineCopy* target = nullptr;
        if (controller) {
            // the component made last that has no controller yet
            for (auto& c : g_copies)
                if (c.module && c.compSerial > c.ctrlSerial && (!target || c.compSerial > target->compSerial)) target = &c;
        } else {
            // a controller that came first (some hosts) waits for its component; else a copy with no machine in it
            for (auto& c : g_copies) if (c.module && c.ctrlSerial > c.compSerial && c.live && c.live() == 0) { target = &c; break; }
            if (!target) for (auto& c : g_copies) if (isFree(c)) { target = &c; break; }
        }
        if (!target) for (auto& c : g_copies) if (controller && isFree(c)) { target = &c; break; }
        if (!target) target = newCopy();
        if (!target) return kResultFalse;

        const tresult r = target->factory->createInstance(cid, iid, obj);
        if (r == kResultOk) (controller ? target->ctrlSerial : target->compSerial) = ++g_serial;
        return r;
    }

private:
    static bool isController(EngineCopy& c, FIDString cid)
    {
        const int32 n = c.factory->countClasses();
        for (int32 i = 0; i < n; ++i) {
            PClassInfo info;
            if (c.factory->getClassInfo(i, &info) == kResultOk && std::memcmp(info.cid, cid, sizeof(TUID)) == 0)
                return std::strcmp(info.category, kVstComponentControllerClass) == 0;
        }
        return false;
    }
    std::atomic<uint32> m_refs{ 1 };
};

ShimFactory g_factory;

} // namespace

#ifdef _WIN32
#define MS2K_EXPORT __declspec(dllexport)
#else
#define MS2K_EXPORT __attribute__((visibility("default")))
#endif

namespace {
bool unloadAll()
{
    std::lock_guard<std::mutex> l(g_lock);
    for (auto it = g_copies.rbegin(); it != g_copies.rend(); ++it) {
        if (!it->module) continue;
        callExit(it->module);
        closeModule(it->module);
        if (it->temporary) deleteFile(it->path);
    }
    g_copies.clear();
    return true;
}
} // namespace

extern "C" {
MS2K_EXPORT IPluginFactory* PLUGIN_API GetPluginFactory()
{
    g_factory.addRef();
    return &g_factory;
}

#ifdef _WIN32
MS2K_EXPORT bool InitDll() { return true; }
MS2K_EXPORT bool ExitDll() { return unloadAll(); }
#else
MS2K_EXPORT bool bundleEntry(CFBundleRef ref) { if (ref && !g_bundle) { CFRetain(ref); g_bundle = ref; } return true; }
MS2K_EXPORT bool bundleExit() { const bool r = unloadAll(); if (g_bundle) { CFRelease(g_bundle); g_bundle = nullptr; } return r; }
#endif
}
