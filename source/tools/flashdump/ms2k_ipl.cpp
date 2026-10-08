// FLASHDUMP-1: see ms2k_ipl.h and docs/ipl_protocol.md.
#include "ms2k_ipl.h"
#include "fwpatch_bin.h"
#include <cstdio>
#include <cstring>

namespace ms2kipl {

// SHA-1 of the image makeDumpSys() builds from x811v107.sys (computed by ipl_e2e, checked against the rules there)
static constexpr const char* kDumpSysSha1 = "cd85d579969411b8cd55a6ccd83690247b51c7ca";

uint32_t typeSize(Type t)
{
    switch (t) { case Type::SYS: return kSysSize; case Type::PCM: case Type::USR: return 0x60000; case Type::ALL: return kFlashSize; }
    return 0;
}

// ---- SHA-1 (FIPS 180-1) ----
std::string sha1Hex(const uint8_t* data, size_t n)
{
    uint32_t h[5] = { 0x67452301u, 0xEFCDAB89u, 0x98BADCFEu, 0x10325476u, 0xC3D2E1F0u };
    auto rol = [](uint32_t x, int s) { return (x << s) | (x >> (32 - s)); };
    std::vector<uint8_t> m(data, data + n);
    const uint64_t bits = uint64_t(n) * 8;
    m.push_back(0x80);
    while (m.size() % 64 != 56) m.push_back(0);
    for (int i = 7; i >= 0; --i) m.push_back(uint8_t(bits >> (i * 8)));
    for (size_t off = 0; off < m.size(); off += 64) {
        uint32_t w[80];
        for (int i = 0; i < 16; ++i) w[i] = uint32_t(m[off + 4 * i]) << 24 | uint32_t(m[off + 4 * i + 1]) << 16 | uint32_t(m[off + 4 * i + 2]) << 8 | m[off + 4 * i + 3];
        for (int i = 16; i < 80; ++i) w[i] = rol(w[i - 3] ^ w[i - 8] ^ w[i - 14] ^ w[i - 16], 1);
        uint32_t a = h[0], b = h[1], c = h[2], d = h[3], e = h[4];
        for (int i = 0; i < 80; ++i) {
            uint32_t f, k;
            if (i < 20) { f = (b & c) | (~b & d); k = 0x5A827999u; }
            else if (i < 40) { f = b ^ c ^ d; k = 0x6ED9EBA1u; }
            else if (i < 60) { f = (b & c) | (b & d) | (c & d); k = 0x8F1BBCDCu; }
            else { f = b ^ c ^ d; k = 0xCA62C1D6u; }
            const uint32_t t = rol(a, 5) + f + e + k + w[i];
            e = d; d = c; c = rol(b, 30); b = a; a = t;
        }
        h[0] += a; h[1] += b; h[2] += c; h[3] += d; h[4] += e;
    }
    char s[41];
    for (int i = 0; i < 5; ++i) std::snprintf(s + 8 * i, 9, "%08x", h[i]);
    return s;
}

// ---- the dump-patched system image ----
static uint32_t be16(const Bytes& b, size_t o) { return uint32_t(b[o]) << 8 | b[o + 1]; }
static uint32_t be32(const Bytes& b, size_t o) { return be16(b, o) << 16 | be16(b, o + 2); }
static void put16(Bytes& b, size_t o, uint32_t v) { b[o] = uint8_t(v >> 8); b[o + 1] = uint8_t(v); }
static void put32(Bytes& b, size_t o, uint32_t v) { put16(b, o, v >> 16); put16(b, o + 2, v & 0xFFFF); }

// v1.07 layout (docs/ipl_protocol.md 1, 2, 6): the boot code copies kIplCount bytes from file kIplFile to RAM 0x407000;
// the copy count is the word at kCountAt, the command table (8 x 32 bit) at kTableAt; main firmware from 0x2000.
static constexpr size_t   kIplFile = 0xD54, kCountAt = 0x1CCE, kTableAt = 0x1CDC, kFirmware = 0x2000;
static constexpr uint32_t kIplCount = 0x1083, kIplRam = 0x407000;
static constexpr uint32_t kTableV107[8] = { 0x407376, 0x40730E, 0x40737A, 0x40730E, 0x40738C, 0x40730E, 0x40730E, 0x40730E };
static constexpr size_t   kSumWord = 0x1FFE;   // free (0xFFFF), not copied, not run: keeps the image's word sum 0

bool makeDumpSys(const Bytes& v107, Bytes& out, std::string& err)
{
    if (v107.size() != kSysSize) { err = "not a 256 KB system file"; return false; }
    if (sha1Hex(v107) != kV107Sha1) { err = "this is not KORG's MS2000 v1.07 system file x811v107.sys (SHA-1 differs) - only that file is supported"; return false; }
    if (be16(v107, kCountAt) != kIplCount) { err = "unexpected IPL size"; return false; }
    for (int i = 0; i < 8; ++i) if (be32(v107, kTableAt + 4 * i) != kTableV107[i]) { err = "unexpected IPL command table"; return false; }
    const size_t at = kIplFile + kIplCount + 1, n = sizeof kDumpPatch;
    if (at + n > kSumWord) { err = "the patch does not fit"; return false; }
    for (size_t i = kIplFile + kIplCount; i < kFirmware; ++i) if (v107[i] != 0xFF) { err = "the space after the IPL is not free"; return false; }
    uint32_t sum = 0;
    for (size_t i = 0; i < kSysSize; i += 2) sum += be16(v107, i);
    if ((sum & 0xFFFF) != 0) { err = "the file's word sum is not 0"; return false; }

    out = v107;
    put16(out, kCountAt, uint32_t(kIplCount + n + 1));                 // the boot code copies the patch too
    std::memcpy(out.data() + at, kDumpPatch, n);                         // RAM 0x408084
    put32(out, kTableAt + 4 * 1, kIplRam + uint32_t(at - kIplFile));     // command 1 = the dump
    put16(out, kSumWord, 0);
    sum = 0;
    for (size_t i = 0; i < kSysSize; i += 2) sum += be16(out, i);
    put16(out, kSumWord, (0x10000u - (sum & 0xFFFF)) & 0xFFFF);
    return true;
}

bool isDumpSys(const Bytes& image) { return image.size() == kSysSize && sha1Hex(image) == kDumpSysSha1; }

std::string validateImage(Type t, const Bytes& image)
{
    if (image.size() != typeSize(t)) return "wrong file size for this update type";
    const bool sysPart = t == Type::SYS || t == Type::ALL;
    if (sysPart) {
        const Bytes sys(image.begin(), image.begin() + kSysSize);
        if (sha1Hex(sys) != kV107Sha1 && !isDumpSys(sys))
            return "the system part is neither KORG's v1.07 nor its flash-dump version - refused (block 0 runs its code)";
    }
    if (t != Type::SYS) {
        const size_t hdr = t == Type::ALL ? 0x800 : 0;
        if (std::memcmp(image.data() + hdr, "KORG", 4) != 0 || image[hdr + 6] != 0x58) return "no KORG MS2000 header";
        if (t != Type::ALL && image[hdr + 7] != uint8_t(t)) return "the file is for another update type";
    }
    return {};
}

// ---- messages ----
static Bytes head(uint8_t cmd) { return { 0xF0, 0x42, 0x23, 0x58, cmd }; }
Bytes msgVersion() { Bytes m = head(0x00); m.push_back(0xF7); return m; }
Bytes msgSetType(Type t) { Bytes m = head(0x02); m.push_back(uint8_t(t)); m.push_back(0xF7); return m; }

Bytes msgBlock(uint8_t bb, const uint8_t* d)
{
    Bytes m = head(0x04);
    m.push_back(bb & 0x7F); m.push_back(0x00);
    unsigned sum = 0;
    for (uint32_t g = 0; g < kBlockSize; g += 7) {
        const uint32_t n = (kBlockSize - g) < 7 ? (kBlockSize - g) : 7;
        uint8_t msb = 0;
        for (uint32_t i = 0; i < n; ++i) if (d[g + i] & 0x80) msb |= uint8_t(1u << i);
        m.push_back(msb); sum += msb;                                  // the msb byte FIRST (IPL 0x407756, vup 0x404A88)
        for (uint32_t i = 0; i < n; ++i) { m.push_back(d[g + i] & 0x7F); sum += d[g + i] & 0x7F; }
    }
    m.push_back(uint8_t(sum & 0x7F));
    m.push_back(0xF7);
    return m;
}

static void put24(Bytes& m, uint32_t v)
{
    const uint8_t b[3] = { uint8_t(v >> 16), uint8_t(v >> 8), uint8_t(v) };
    for (uint8_t x : b) m.push_back(x & 0x7F);
    m.push_back(uint8_t((b[0] & 0x80 ? 1 : 0) | (b[1] & 0x80 ? 2 : 0) | (b[2] & 0x80 ? 4 : 0)));
}
Bytes msgDump(uint32_t addr, uint32_t size) { Bytes m = head(0x01); put24(m, addr); put24(m, size); m.push_back(0xF7); return m; }

// ---- replies ----
static bool isIpl(const Bytes& s, uint8_t cmd, size_t len)
{
    return s.size() == len && s[0] == 0xF0 && s[1] == 0x42 && s[2] == 0x23 && s[3] == 0x58 && s[4] == cmd && s.back() == 0xF7;
}
bool parseVersion(const Bytes& s, Version& v)
{
    if (!isIpl(s, 0x01, 12)) return false;
    v.sys = uint16_t(s[5] | s[6] << 8); v.pcm = uint16_t(s[7] | s[8] << 8); v.usr = uint16_t(s[9] | s[10] << 8);
    return true;
}
int parseStatus(const Bytes& s) { return isIpl(s, 0x03, 7) ? s[5] : -1; }
std::string statusText(int c)
{
    switch (c) {
    case 0: return "OK";
    case 1: return "format error (the unit did not accept the message)";
    case 2: return "checksum error (data damaged on the MIDI cable)";
    case 3: return "timeout";
    case 4: return "MIDI receive error at the unit (overrun / framing)";
    case 5: return "flash WRITE error at the unit";
    }
    return "unknown reply " + std::to_string(c);
}
std::string versionText(uint16_t v)
{
    if (!v) return "-";
    char s[16]; std::snprintf(s, sizeof s, "%u.%02u", unsigned(v >> 8), unsigned(v & 0xFF)); return s;
}

bool decodeDump(const Bytes& s, uint32_t size, Bytes& out)
{
    out.assign(size, 0);
    size_t j = 1;
    if (s.size() < 2 || s[0] != 0xF0) return false;
    uint32_t i = 0, k = 0;
    for (; i < size; ++i) {
        if (j >= s.size()) return false;
        out[i] = s[j++]; ++k;
        if (k == 7) {
            if (j >= s.size()) return false;
            const uint8_t top = s[j++];
            for (uint32_t n = 0; n < 7; ++n) if (top & (1u << n)) out[i - 6 + n] |= 0x80;
            k = 0;
        }
    }
    if (k) {
        if (j >= s.size()) return false;
        const uint8_t top = s[j++];
        for (uint32_t n = 0; n < k; ++n) if (top & (1u << n)) out[i - k + n] |= 0x80;
    }
    return j == s.size() - 1 && s[j] == 0xF7;
}

// ---- session ----
static int waitStatus(Transport& t, int timeoutMs)
{
    Bytes s;
    for (int tries = 0; tries < 8; ++tries) {          // anything else (a stray reply) is skipped
        if (!t.receive(s, timeoutMs)) return -2;
        const int c = parseStatus(s);
        if (c >= 0) return c;
    }
    return -3;
}

Result getVersion(Transport& t, Version& v)
{
    Result r;
    if (!t.send(msgVersion())) { r.error = "MIDI send failed"; return r; }
    Bytes s;
    for (int tries = 0; tries < 8; ++tries) {
        if (!t.receive(s, 3000)) { r.error = "no answer - is the unit in update mode (hold WRITE + TYPE while switching on) and are MIDI IN/OUT connected both ways?"; return r; }
        if (parseVersion(s, v)) { t.wait(20); r.ok = true; return r; }
    }
    r.error = "unexpected answer (not an MS2000 / MS2000R in update mode)";
    return r;
}

Result install(Transport& t, Type type, const Bytes& image, const Progress& progress)
{
    Result r;
    if (const std::string e = validateImage(type, image); !e.empty()) { r.error = e; return r; }
    const int n = int(image.size() / kBlockSize);
    const std::string again = " The unit is still in update mode: do NOT switch it off - run the whole update again.";
    if (progress && !progress(0, n, "set type")) { r.error = "stopped before anything was sent"; return r; }
    if (!t.send(msgSetType(type))) { r.error = "MIDI send failed"; return r; }
    int c = waitStatus(t, 20000);
    if (c != 0) { r.error = c < 0 ? "no answer to Set Type" : "Set Type: " + statusText(c); return r; }
    t.wait(20);
    for (int bb = 0; bb < n; ++bb) {                    // ascending, block 0 first (docs/ipl_protocol.md 5)
        if (progress && !progress(bb, n, "block " + std::to_string(bb))) { r.error = "stopped by the user after block " + std::to_string(bb - 1) + "." + again; return r; }
        if (!t.send(msgBlock(uint8_t(bb), image.data() + size_t(bb) * kBlockSize))) { r.error = "MIDI send failed at block " + std::to_string(bb) + "." + again; return r; }
        c = waitStatus(t, 30000);
        if (c != 0) { r.error = "block " + std::to_string(bb) + ": " + (c < 0 ? std::string("no answer") : statusText(c)) + "." + again; return r; }
        t.wait(20);
    }
    if (progress) progress(n, n, "done");
    r.ok = true;
    return r;
}

Result dump(Transport& t, uint32_t addr, uint32_t size, Bytes& out, const Progress& progress, int retries)
{
    Result r;
    constexpr uint32_t kChunk = 0x80;
    out.clear(); out.reserve(size);
    const int total = int((size + kChunk - 1) / kChunk);
    for (uint32_t off = 0; off < size; off += kChunk) {
        const uint32_t len = (size - off) < kChunk ? (size - off) : kChunk;
        if (progress && !progress(int(off / kChunk), total, "read")) { r.error = "stopped by the user"; return r; }
        bool ok = false;
        for (int a = 0; a <= retries && !ok; ++a) {
            if (a) t.wait(300);
            if (!t.send(msgDump(addr + off, len))) continue;
            Bytes s, d;
            if (t.receive(s, 3000) && decodeDump(s, len, d)) { out.insert(out.end(), d.begin(), d.end()); ok = true; }
            t.wait(20);
        }
        if (!ok) {
            char e[96]; std::snprintf(e, sizeof e, "no valid data for 0x%06X after %d tries", unsigned(addr + off), retries + 1);
            r.error = std::string(e) + " - is the flash-dump update installed, and the unit in update mode?";
            return r;
        }
    }
    if (progress) progress(total, total, "done");
    r.ok = true;
    return r;
}

}
