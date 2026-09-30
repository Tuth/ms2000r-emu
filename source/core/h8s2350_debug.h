#pragma once
#include <cstdint>
#include <cstdio>

// Log szintek: 0=OFF, 1=ERROR, 2=WARN, 3=INFO, 4=TRACE
#ifndef H8S_LOG_LEVEL
  #ifdef H8S_DEV_FAILSAFE
    #define H8S_LOG_LEVEL 3
  #else
    #define H8S_LOG_LEVEL 0
  #endif
#endif

// --- Member-függvényből hívható makrók (használja: this->getProgramCounter(), this->getSP24()) ---
#define H8S_ERR(TAG, FMT, ...)   do{ if(H8S_LOG_LEVEL>=1) std::printf("[H8S-%s][ERR ] PC=%06X SP=%06X " FMT "\n",  TAG, this->getProgramCounter(), this->getSP24(), ##__VA_ARGS__);}while(0)
#define H8S_WARN(TAG, FMT, ...)  do{ if(H8S_LOG_LEVEL>=2) std::printf("[H8S-%s][WARN] PC=%06X SP=%06X " FMT "\n",  TAG, this->getProgramCounter(), this->getSP24(), ##__VA_ARGS__);}while(0)
#define H8S_INFO(TAG, FMT, ...)  do{ if(H8S_LOG_LEVEL>=3) std::printf("[H8S-%s][INFO] PC=%06X SP=%06X " FMT "\n",  TAG, this->getProgramCounter(), this->getSP24(), ##__VA_ARGS__);}while(0)
#define H8S_TRACE(TAG, FMT, ...) do{ if(H8S_LOG_LEVEL>=4) std::printf("[H8S-%s][TRCE] PC=%06X SP=%06X " FMT "\n",  TAG, this->getProgramCounter(), this->getSP24(), ##__VA_ARGS__);}while(0)

// --- Kontextusos változatok (nem-memberből, pl. free function): ctx = H8S2350Emulator* ---
#define H8S_ERR_CTX(CTX,TAG,FMT,...)   do{ if(H8S_LOG_LEVEL>=1) std::printf("[H8S-%s][ERR ] PC=%06X SP=%06X " FMT "\n",  TAG, (CTX)->getProgramCounter(), (CTX)->getSP24(), ##__VA_ARGS__);}while(0)
#define H8S_WARN_CTX(CTX,TAG,FMT,...)  do{ if(H8S_LOG_LEVEL>=2) std::printf("[H8S-%s][WARN] PC=%06X SP=%06X " FMT "\n",  TAG, (CTX)->getProgramCounter(), (CTX)->getSP24(), ##__VA_ARGS__);}while(0)
#define H8S_INFO_CTX(CTX,TAG,FMT,...)  do{ if(H8S_LOG_LEVEL>=3) std::printf("[H8S-%s][INFO] PC=%06X SP=%06X " FMT "\n",  TAG, (CTX)->getProgramCounter(), (CTX)->getSP24(), ##__VA_ARGS__);}while(0)
#define H8S_TRACE_CTX(CTX,TAG,FMT,...) do{ if(H8S_LOG_LEVEL>=4) std::printf("[H8S-%s][TRCE] PC=%06X SP=%06X " FMT "\n",  TAG, (CTX)->getProgramCounter(), (CTX)->getSP24(), ##__VA_ARGS__);}while(0)

// Egyszerű rate-limit (N-edenként logol)
#define H8S_EVERY(N, CODE) do { static uint32_t __cnt=0; if(((++__cnt) % (N))==0){ CODE; } } while(0)

// Cycle debug trace (csak TRACE szinten)
#define H8S_CYCLES(TAG, DELTA) \
  do { if(H8S_LOG_LEVEL>=4) std::printf("[H8S-%s][CYC ] +%u => %llu\n", TAG, (unsigned)(DELTA), (unsigned long long)this->getCycles()); } while(0)