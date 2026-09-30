// Simplified stb_image_write wrapper for PNG only
#pragma once

#ifdef __cplusplus
extern "C" {
#endif

// Write PNG file - returns 1 on success, 0 on failure  
int stbi_write_png_simple(char const *filename, int w, int h, int comp, const void *data, int stride_in_bytes);

#ifdef __cplusplus
}
#endif

#ifdef STB_IMAGE_WRITE_SIMPLE_IMPLEMENTATION

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include <math.h>

// Minimal PNG writer implementation
static void stbiw__putc(FILE* f, unsigned char c) { fputc(c, f); }
static void stbiw__write3(FILE* f, unsigned char a, unsigned char b, unsigned char c) { 
    fputc(a, f); fputc(b, f); fputc(c, f); 
}

static unsigned int stbiw__crc32(unsigned char *buffer, int len) {
    static unsigned int crc_table[256];
    static int table_computed = 0;
    
    if (!table_computed) {
        for (int n = 0; n < 256; n++) {
            unsigned int c = n;
            for (int k = 0; k < 8; k++) {
                if (c & 1) c = 0xedb88320 ^ (c >> 1);
                else c = c >> 1;
            }
            crc_table[n] = c;
        }
        table_computed = 1;
    }
    
    unsigned int c = 0xffffffff;
    for (int n = 0; n < len; n++) {
        c = crc_table[(c ^ buffer[n]) & 0xff] ^ (c >> 8);
    }
    return c ^ 0xffffffff;
}

static void stbiw__wp32(FILE* f, unsigned int v) {
    fputc((v >> 24) & 0xff, f);
    fputc((v >> 16) & 0xff, f);
    fputc((v >> 8) & 0xff, f);
    fputc(v & 0xff, f);
}

static void stbiw__wptag(FILE* f, char* s) {
    fputc(s[0], f); fputc(s[1], f); fputc(s[2], f); fputc(s[3], f);
}

int stbi_write_png_simple(char const *filename, int w, int h, int comp, const void *data, int stride_in_bytes) {
    FILE *f = fopen(filename, "wb");
    if (!f) return 0;
    (void)comp; (void)stride_in_bytes;

    // PNG signature
    unsigned char sig[8] = {137,80,78,71,13,10,26,10};
    fwrite(sig, 1, 8, f);

    // ---- IHDR ----
    unsigned char ihdr[17];
    memcpy(ihdr, "IHDR", 4);
    ihdr[4]=(unsigned char)((w>>24)&0xff); ihdr[5]=(unsigned char)((w>>16)&0xff);
    ihdr[6]=(unsigned char)((w>>8)&0xff);  ihdr[7]=(unsigned char)(w&0xff);
    ihdr[8]=(unsigned char)((h>>24)&0xff); ihdr[9]=(unsigned char)((h>>16)&0xff);
    ihdr[10]=(unsigned char)((h>>8)&0xff); ihdr[11]=(unsigned char)(h&0xff);
    ihdr[12]=8; ihdr[13]=0; ihdr[14]=0; ihdr[15]=0; ihdr[16]=0;
    stbiw__wp32(f, 13);
    fwrite(ihdr, 1, 17, f);
    stbiw__wp32(f, stbiw__crc32(ihdr, 17));

    // ---- Build raw scanlines (filter byte 0 + row pixels) ----
    int raw_len = h * (w + 1);
    unsigned char *raw = (unsigned char*)malloc((size_t)raw_len);
    const unsigned char *src = (const unsigned char*)data;
    int rp = 0;
    for (int y = 0; y < h; y++) {
        raw[rp++] = 0; // filter type none
        for (int x = 0; x < w; x++) raw[rp++] = src[y * w + x];
    }

    // ---- Adler-32 over raw ----
    unsigned int a = 1, b = 0;
    for (int i = 0; i < raw_len; i++) {
        a = (a + raw[i]) % 65521u;
        b = (b + a) % 65521u;
    }
    unsigned int adler = (b << 16) | a;

    // ---- Zlib stream: header(2) + stored deflate blocks + adler(4) ----
    // Stored block max payload is 65535; chunk it for safety on large images.
    // Pre-size: header 2 + adler 4 + per-block 5 overhead.
    int nblocks = (raw_len + 65534) / 65535;
    if (nblocks == 0) nblocks = 1;
    int zlen = 2 + raw_len + nblocks * 5 + 4;
    unsigned char *zbuf = (unsigned char*)malloc((size_t)zlen);
    int zp = 0;
    zbuf[zp++] = 0x78; // CMF
    zbuf[zp++] = 0x01; // FLG (check bits make 0x7801 a multiple of 31)
    int remaining = raw_len, off = 0;
    while (remaining > 0) {
        int block = remaining > 65535 ? 65535 : remaining;
        int final = (remaining - block) <= 0 ? 1 : 0;
        zbuf[zp++] = (unsigned char)final;               // BFINAL + BTYPE=00
        zbuf[zp++] = (unsigned char)(block & 0xff);       // LEN low
        zbuf[zp++] = (unsigned char)((block >> 8) & 0xff);// LEN high
        zbuf[zp++] = (unsigned char)(~block & 0xff);      // NLEN low
        zbuf[zp++] = (unsigned char)((~block >> 8) & 0xff);// NLEN high
        memcpy(zbuf + zp, raw + off, (size_t)block);
        zp += block; off += block; remaining -= block;
    }
    zbuf[zp++] = (unsigned char)((adler >> 24) & 0xff);
    zbuf[zp++] = (unsigned char)((adler >> 16) & 0xff);
    zbuf[zp++] = (unsigned char)((adler >> 8) & 0xff);
    zbuf[zp++] = (unsigned char)(adler & 0xff);

    // ---- IDAT chunk (length + tag + zdata), CRC over tag+zdata ----
    stbiw__wp32(f, (unsigned int)zp);
    unsigned char *idat = (unsigned char*)malloc((size_t)(zp + 4));
    memcpy(idat, "IDAT", 4);
    memcpy(idat + 4, zbuf, (size_t)zp);
    fwrite(idat, 1, (size_t)(zp + 4), f);
    stbiw__wp32(f, stbiw__crc32(idat, zp + 4));

    free(idat); free(zbuf); free(raw);

    // ---- IEND ----
    stbiw__wp32(f, 0);
    unsigned char iend[4]; memcpy(iend, "IEND", 4);
    fwrite(iend, 1, 4, f);
    stbiw__wp32(f, stbiw__crc32(iend, 4));

    fclose(f);
    return 1;
}

#endif // STB_IMAGE_WRITE_SIMPLE_IMPLEMENTATION