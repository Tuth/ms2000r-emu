/* Local-only differential-test shim around the UKNTCH2000 reference H8S core.
 * The reference source is NOT copied into this repository: it is #included from
 * the path given by MS2K_REF_H8S at configure time (CMake option MS2K_REFCORE).
 * The reference is a witness, not scripture: every disagreement is adjudicated
 * against the RENDERED Hitachi manual pages before anything is changed. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <setjmp.h>

#ifdef _MSC_VER
#define __builtin_bswap16 _byteswap_ushort
#define __builtin_bswap32 _byteswap_ulong
#define __builtin_bswap64 _byteswap_uint64
#endif
#ifndef __BYTE_ORDER__
#define __ORDER_LITTLE_ENDIAN__ 1234
#define __ORDER_BIG_ENDIAN__    4321
#define __BYTE_ORDER__          __ORDER_LITTLE_ENDIAN__
#endif

static jmp_buf g_refJmp;
static int g_refQuiet = 1;
static void ref_exit(int c) { (void)c; longjmp(g_refJmp, 1); }
static int ref_printf(const char* f, ...) { if (g_refQuiet) return 0; va_list a; va_start(a, f); int n = vprintf(f, a); va_end(a); return n; }
#define exit   ref_exit
#define printf ref_printf

#include MS2K_REF_H8S

#undef exit
#undef printf

/* ---- access log ---- */
#define REF_MAXACC 64
typedef struct { unsigned addr, size, write, value; } RefAcc;
static RefAcc g_acc[REF_MAXACC];
static int g_nacc;
static void logacc(u32 a, unsigned s, unsigned w, u32 v)
{ if (g_nacc < REF_MAXACC) { g_acc[g_nacc].addr = a; g_acc[g_nacc].size = s; g_acc[g_nacc].write = w; g_acc[g_nacc].value = v; } g_nacc++; }

void H8STraceReadI8(H8S* c, u32 a, u8 v)   { (void)c; logacc(a, 1, 0, v); }
void H8STraceReadI16(H8S* c, u32 a, u16 v) { (void)c; logacc(a, 2, 0, v); }
void H8STraceReadI32(H8S* c, u32 a, u32 v) { (void)c; logacc(a, 4, 0, v); }
void H8STraceWriteI8(H8S* c, u32 a, u8 v)  { (void)c; logacc(a, 1, 1, v); }
void H8STraceWriteI16(H8S* c, u32 a, u16 v){ (void)c; logacc(a, 2, 1, v); }
void H8STraceWriteI32(H8S* c, u32 a, u32 v){ (void)c; logacc(a, 4, 1, v); }
void H8STraceStep(H8S* c) { (void)c; }
void H8STraceIRQ(H8S* c, u16 i) { (void)c; (void)i; }
void H8STraceClose(H8S* c) { (void)c; }
void H8SInitTPG(H8S* c) { (void)c; }  void H8SStepTPG(H8S* c) { (void)c; }
void H8SInitSCI(H8S* c) { (void)c; }  void H8SStepSCI(H8S* c) { (void)c; }
void H8SInitDMAC(H8S* c) { (void)c; } void H8SStepDMAC(H8S* c) { (void)c; }
u8 H8SReceiveSCI0(H8S* c) { (void)c; return 0; } u8 H8SReceiveSCI1(H8S* c) { (void)c; return 0; }
void H8STransmitSCI0(H8S* c, u8 d) { (void)c; (void)d; } void H8STransmitSCI1(H8S* c, u8 d) { (void)c; (void)d; }
void LCDInit(H8S* c) { (void)c; } void LCDWrite(H8S* c, u8 d) { (void)c; (void)d; } u8 LCDRead(H8S* c) { (void)c; return 0; }

/* ---- flat API for the C++ side ---- */
static H8S g_ref;
void ref_init(void) { H8SInit(&g_ref); memset(g_ref.ram, 0, 0x1000000); g_ref.trace = TRUE; }
unsigned char* ref_ram(void) { return g_ref.ram; }
void ref_set(const unsigned er[8], unsigned pc, unsigned ccr, unsigned exr)
{ for (int i = 0; i < 8; ++i) g_ref.er[i] = er[i]; g_ref.pc = pc; g_ref.ccr = (u8)ccr; g_ref.exr = (u8)exr; }
void ref_get(unsigned er[8], unsigned* pc, unsigned* ccr, unsigned* exr)
{ for (int i = 0; i < 8; ++i) er[i] = g_ref.er[i]; *pc = g_ref.pc; *ccr = g_ref.ccr; *exr = g_ref.exr; }
/* 1 = executed, 0 = reference does not know the opcode, -1 = reference aborted (exit) */
int ref_step(void)
{
    g_nacc = 0;
    if (setjmp(g_refJmp)) return -1;
    return H8SStepCPU(&g_ref) ? 1 : 0;
}
int ref_nacc(void) { return g_nacc; }
void ref_acc(int i, unsigned* addr, unsigned* size, unsigned* write, unsigned* value)
{ *addr = g_acc[i].addr; *size = g_acc[i].size; *write = g_acc[i].write; *value = g_acc[i].value; }
