#pragma once
#include <array>
#include <string>
#include <vector>
#include <cstdint>
#include <algorithm>
#include <chrono>

// fw25.txt: Compact event-driven LCD observer with fuzzy matching

class LcdObserver {
public:
  void reset() {
    line_[0].fill(' '); line_[1].fill(' ');
    ddram_ = 0; lastWriteMs_ = nowMs(); matched_ = false; hitWhich_ = -1; hitRow_=-1; hitCol_=-1;
  }
  void setPatterns(std::vector<std::string> pats) {
    pats_ = std::move(pats);
    // normalizálás nagybetűre
    for (auto& p: pats_) for (auto& ch: p) ch = norm(ch);
  }
  void setStableMs(uint32_t ms){ stableMs_ = ms; }

  // HD44780 parancsok figyelése
  void onCmd(uint8_t c) {
    if (c == 0x01) { // Clear
      line_[0].fill(' '); line_[1].fill(' '); ddram_=0;
    } else if (c == 0x02) { // Home
      ddram_ = 0;
    } else if ((c & 0xF8) == 0x04) { // fw26.txt: Entry Mode Set 0b000001IS
      increment_ = (c & 0x02) != 0;
      shiftOnWrite_ = (c & 0x01) != 0;
    } else if (c & 0x80) { // Set DDRAM
      ddram_ = c & 0x7F;
    }
  }
  void onData(uint8_t d) {
    char ch = (d>=0x20 && d<=0x7E) ? char(d) : ' ';
    auto rc = map(ddram_);
    if (rc.first!=-1) {
      line_[rc.first][rc.second] = ch;
    }
    // fw26.txt: Step according to Entry Mode
    ddram_ = increment_ ? inc(ddram_) : dec(ddram_);
    lastWriteMs_ = nowMs();
    checkPatterns(); // ESEMÉNY-ALAPÚ: minden karakterre ellenőrzünk
  }

  bool matched() const { return matched_; }
  bool stableWindowReached() const { return (nowMs() - lastWriteMs_) >= stableMs_; }

  // találat meta
  int  hitPatternIndex() const { return hitWhich_; }
  int  hitRow() const { return hitRow_; }
  int  hitCol() const { return hitCol_; }

  // pillanatnyi 2×16 (opcionális)
  std::array<char,17> line0() const { std::array<char,17> s{}; copyLn(0,s); return s; }
  std::array<char,17> line1() const { std::array<char,17> s{}; copyLn(1,s); return s; }

private:
  std::array<std::array<char,16>,2> line_{{}};
  uint8_t ddram_ = 0;
  uint64_t lastWriteMs_ = 0;
  uint32_t stableMs_ = 250;
  bool matched_ = false;
  int hitWhich_ = -1, hitRow_ = -1, hitCol_ = -1;
  std::vector<std::string> pats_;
  
  // fw26.txt: Entry Mode tracking for proper cursor movement
  bool increment_ = true;
  bool shiftOnWrite_ = false;

  static uint64_t nowMs(){
    using namespace std::chrono;
    return duration_cast<milliseconds>(steady_clock::now().time_since_epoch()).count();
  }
  static char norm(char c){ if (c>='a'&&c<='z') return char(c-32); return c; }

  std::pair<int,int> map(uint8_t a) const {
    if (a <= 0x0F) return {0,int(a)};
    if (a >= 0x40 && a <= 0x4F) return {1,int(a-0x40)};
    return {-1,-1};
  }
  // fw26.txt: Entry Mode aware cursor movement
  uint8_t inc(uint8_t addr) const {
    if (++addr == 0x10) return 0x40;
    if (addr == 0x50) return 0x00;
    return addr;
  }
  uint8_t dec(uint8_t addr) const {
    if (addr == 0x00) return 0x4F;
    if (addr == 0x40) return 0x0F;
    return addr - 1;
  }
  void copyLn(int r, std::array<char,17>& out) const {
    for(int i=0;i<16;i++) out[i]=norm(line_[r][i]);
    out[16]=0;
  }
  static int hamming(const char* a, const char* b, int n){
    int d=0; for(int i=0;i<n;i++) if (norm(a[i])!=norm(b[i])) d++; return d;
  }
  void checkPatterns(){
    if (matched_ || pats_.empty()) return;
    std::array<char,17> L0=line0(), L1=line1();
    for (int row=0; row<2; ++row){
      const char* L = (row==0? L0.data(): L1.data());
      for (size_t p=0; p<pats_.size(); ++p){
        const auto& pat = pats_[p];
        int m = int(pat.size());
        for (int i=0;i<=16-m;i++){
          if (hamming(L+i, pat.c_str(), m) <= 1) {
            matched_ = true; hitWhich_=int(p); hitRow_=row; hitCol_=i;
            return;
          }
        }
      }
    }
  }
};