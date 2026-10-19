// MAC-4: several MS2000R Audio Units in one host process (macOS) - the AU counterpart of ms2k_vst3_shim.cpp.
//
// The emulator core keeps function-local statics and globals, so one copy of the plugin binary holds one machine.
// The component's Info.plist names MS2000RAUFactory as its factory; this module (Contents/MacOS/MS2000R) exports it,
// and the JUCE plugin travels as Contents/Resources/MS2000R_engine. The first instance runs from that file; every
// further one from a private copy in $TMPDIR/MS2000R_au_engines, dlopen()ed - a different file is a different image:
// its own statics, its own JUCE (its Cocoa view class has its own runtime name, so each editor opens).
//
// An AU instance is one factory call; the host then opens it (JUCE constructs the processor there) and closes it,
// and the factory is not told about either. So a copy is handed to a new instance only when it holds no machine
// (ms2k_liveProcessors() == 0) and the instance it was last handed to has opened (ms2k_createdProcessors() grew
// since) - an instance that is created but never opened costs one more copy, nothing worse.
// Copies stay loaded for the life of the process; stale copy files are removed on the next first load.
#include <AudioToolbox/AudioToolbox.h>
#include <dlfcn.h>
#include <signal.h>
#include <unistd.h>

#include <cerrno>
#include <cstdlib>
#include <deque>
#include <filesystem>
#include <mutex>
#include <string>

namespace {

typedef AudioComponentPlugInInterface* (*FactoryProc)(const AudioComponentDescription*);
typedef int (*CountProc)();

struct EngineCopy {
    void* module = nullptr;
    FactoryProc factory = nullptr;
    CountProc live = nullptr, created = nullptr;
    std::string path;
    bool handed = false;          // given to an instance at least once
    int createdAtHandout = 0;     // ms2k_createdProcessors() then
};

std::mutex g_lock;
std::deque<EngineCopy> g_copies;   // a deque: pointers to its elements survive push_back

std::string modulePath()
{
    Dl_info info{};
    return dladdr(reinterpret_cast<const void*>(&modulePath), &info) && info.dli_fname ? std::string(info.dli_fname) : std::string();
}

std::string parentDir(const std::string& p)
{
    const auto i = p.find_last_of('/');
    return i == std::string::npos ? std::string() : p.substr(0, i);
}

// <component>/Contents/MacOS/MS2000R -> <component>/Contents/Resources/MS2000R_engine
std::string enginePath() { return parentDir(parentDir(modulePath())) + "/Resources/MS2000R_engine"; }

std::string tempDir()
{
    const char* t = std::getenv("TMPDIR");
    std::string d = t && *t ? t : "/tmp/";
    if (d.back() != '/') d += '/';
    d += "MS2000R_au_engines";
    std::error_code ec; std::filesystem::create_directories(d, ec);
    return d;
}

// copies left behind by a process that is gone (engine_<pid>_<n>). Copies of a running process stay: hosts that scan
// or sandbox plugins load the component in several processes at once, and a copy made but not dlopen()ed yet could go.
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

bool loadCopy(EngineCopy& c)
{
    c.module = dlopen(c.path.c_str(), RTLD_NOW | RTLD_LOCAL);
    if (!c.module) return false;
    c.factory = reinterpret_cast<FactoryProc>(dlsym(c.module, "MS2000RAUFactory"));
    c.live = reinterpret_cast<CountProc>(dlsym(c.module, "ms2k_liveProcessors"));
    c.created = reinterpret_cast<CountProc>(dlsym(c.module, "ms2k_createdProcessors"));
    if (!c.factory) { dlclose(c.module); c.module = nullptr; return false; }
    return true;
}

EngineCopy* first()   // the engine file in the bundle
{
    if (!g_copies.empty()) return g_copies[0].module ? &g_copies[0] : nullptr;
    // the engine finds the MS2000 folder above the plugin binary (PUBLIC-1); a copy in $TMPDIR must look above this file
    setenv("MS2K_PLUGIN_BINARY", modulePath().c_str(), 1);
    cleanStaleCopies();
    EngineCopy c;
    c.path = enginePath();
    g_copies.push_back(c);
    if (!loadCopy(g_copies[0])) return nullptr;
    return &g_copies[0];
}

EngineCopy* newCopy()
{
    static unsigned counter = 0;
    EngineCopy c;
    c.path = tempDir() + "/engine_" + std::to_string(getpid()) + "_" + std::to_string(++counter);
    std::error_code ec;
    if (!std::filesystem::copy_file(g_copies[0].path, c.path, std::filesystem::copy_options::overwrite_existing, ec)) return nullptr;
    g_copies.push_back(c);
    if (!loadCopy(g_copies.back())) { std::filesystem::remove(c.path, ec); g_copies.pop_back(); return nullptr; }
    return &g_copies.back();
}

bool isFree(const EngineCopy& c)
{
    if (!c.module || !c.live || c.live() != 0) return false;
    if (c.handed && c.created && c.created() <= c.createdAtHandout) return false;   // handed out, not opened yet
    return true;
}

} // namespace

extern "C" __attribute__((visibility("default"))) AudioComponentPlugInInterface* MS2000RAUFactory(const AudioComponentDescription* desc)
{
    std::lock_guard<std::mutex> l(g_lock);
    if (!first()) return nullptr;
    EngineCopy* target = nullptr;
    for (auto& c : g_copies) if (isFree(c)) { target = &c; break; }
    if (!target) target = newCopy();
    if (!target) return nullptr;
    target->handed = true;
    target->createdAtHandout = target->created ? target->created() : 0;
    return target->factory(desc);
}
