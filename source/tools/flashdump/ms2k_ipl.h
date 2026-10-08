// FLASHDUMP-1 (2026-10-08): the KORG MS2000 / MS2000R firmware-update ("IPL") protocol, host side - no UI, no MIDI API.
// Spec: docs/ipl_protocol.md (v1.07 IPL disassembled, KORG vup_ms2000.exe v1.3 host code read). Flash dump: the IPL
// command 1 of unknown-technologies' fwpatch (fwpatch_bin.h), installed as a normal SYS update.
//
// MS2000B / MS2000BR: NOT compatible - other hardware and firmware. Never use any of this with them.
//
// Safety rules this code keeps (docs/ipl_protocol.md section 5):
//  - only images that pass validateImage() are ever sent; a SYS image must be KORG's v1.07 x811v107.sys or exactly
//    makeDumpSys() of it (sha1 checked), because block 0 hands the IPL the NEW image's flash routines;
//  - blocks go in ascending order, block 0 first; every reply is awaited, nothing is sent while the unit is busy;
//  - no resume after an error: a failed update is restarted from Set-Type (the IPL erases sectors lazily, resuming in
//    the middle of a sector would erase the blocks already written there);
//  - the IPL stays running after an error: the user must NOT power off - retry the whole update.
#pragma once
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace ms2kipl {

using Bytes = std::vector<uint8_t>;

constexpr uint32_t kSysSize   = 0x40000;    // system area = KORG's .sys file
constexpr uint32_t kFlashSize = 0x100000;   // MBM29LV800BA
constexpr uint32_t kBlockSize = 0x2000;     // one Data Block message
constexpr const char* kV107Sha1 = "945a75555fcd3f17d7ad9f15d38a431869a07bb8";   // x811v107.sys, KORG MS2000 updater

enum class Type : uint8_t { SYS = 0, PCM = 1, USR = 2, ALL = 3 };
uint32_t typeSize(Type t);

std::string sha1Hex(const uint8_t* data, size_t n);
inline std::string sha1Hex(const Bytes& b) { return sha1Hex(b.data(), b.size()); }

// x811v107.sys -> the same image with the flash-dump command (fwpatch) in the IPL and the word sum kept 0.
// Strict: only KORG's v1.07 file (sha1) is accepted; every patched location is checked before it is changed.
bool makeDumpSys(const Bytes& v107, Bytes& out, std::string& err);
// "" if the image may be sent as this type, else why not (see the safety rules above).
std::string validateImage(Type t, const Bytes& image);
bool isDumpSys(const Bytes& image);          // == makeDumpSys(v1.07)

// ---- messages host -> unit ----
Bytes msgVersion();
Bytes msgSetType(Type t);
Bytes msgBlock(uint8_t blockInType, const uint8_t* data8k);   // KORG packing: msb byte FIRST, then 7 bytes
Bytes msgDump(uint32_t addr, uint32_t size);                  // patched IPL only

// ---- replies unit -> host (complete SysEx F0 .. F7, realtime bytes already removed) ----
struct Version { uint16_t sys = 0, pcm = 0, usr = 0; };       // major << 8 | minor (v1.07 = 0x0107)
bool parseVersion(const Bytes& sx, Version& v);
int  parseStatus(const Bytes& sx);                            // 0 ok, 1..5 error, -1 not a status reply
std::string statusText(int code);
bool decodeDump(const Bytes& sx, uint32_t size, Bytes& out);  // F0 <7 data, msb>... F7
std::string versionText(uint16_t v);

// ---- a session over any MIDI transport ----
struct Transport {
    virtual ~Transport() = default;
    virtual bool send(const Bytes& msg) = 0;
    // the next complete SysEx (F0..F7) within timeoutMs; realtime bytes (F8..FF) must be dropped by the transport
    virtual bool receive(Bytes& sysex, int timeoutMs) = 0;
    // let ms pass (the IPL is deaf while it writes its LCD after a reply - KORG and fwdump wait 20 ms)
    virtual void wait(int ms) = 0;
};
// called after every step; return false to stop (only ever between two messages)
using Progress = std::function<bool(int done, int total, const std::string& what)>;
struct Result { bool ok = false; std::string error; };

Result getVersion(Transport& t, Version& v);
Result install(Transport& t, Type type, const Bytes& image, const Progress& progress);
Result dump(Transport& t, uint32_t addr, uint32_t size, Bytes& out, const Progress& progress, int retries = 3);

}
