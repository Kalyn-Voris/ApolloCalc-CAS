/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Kalyn-Voris (ApolloCalc-CAS). See LICENSE at the repository root. */
/* ============================================================
 * ApolloCalc-CAS · ac_util.c — 工具层（第 9 轮，C 独有）
 * 功能：CRC32（0xEDB88320 反射多项式，256 项表懒初始化，可链式
 *       update）、SHA-256（FIPS 180-4 单发实现，K/H 表编译期常量）、
 *       Base64（标准 "+/" 字母表，编解码 + 反查表懒初始化）、
 *       定点数（int64 存 value×10^6，溢出预检复用 ac_exact safe 原语）。
 * Zero-GC：无 malloc、无 VLA、无递归；所有中间缓冲为定长栈数组
 *          （SHA 的 w[64]/block[64]），全局仅两张 static 查找表。
 * ============================================================ */
#include "ac_util.h"
#include "ac_exact.h"

/* ==================== CRC32 ==================== */

/* 256 项表。懒初始化：首次调用时构建 */
static uint32_t s_crc_table[256];
static int      s_crc_ready = 0;

static void crc_build_table(void) {
    uint32_t c;
    int i;
    int j;
    for (i = 0; i < 256; i++) {
        c = (uint32_t)i;
        for (j = 0; j < 8; j++) {
            c = (c & 1u) ? (0xEDB88320u ^ (c >> 1)) : (c >> 1);
        }
        s_crc_table[i] = c;
    }
    s_crc_ready = 1;
}

uint32_t ac_crc32_update(uint32_t crc, const uint8_t *data, int len) {
    int i;
    if (!s_crc_ready) { crc_build_table(); }
    crc = ~crc;
    for (i = 0; i < len; i++) {
        crc = s_crc_table[(crc ^ (uint32_t)data[i]) & 0xFFu] ^ (crc >> 8);
    }
    return ~crc;
}

uint32_t ac_crc32(const uint8_t *data, int len) {
    return ac_crc32_update(0, data, len);
}

/* ==================== SHA-256 ==================== */

/* [English] 32-bit rotate right. n is always 1..31 at call sites. */
static uint32_t sha_rotr(uint32_t x, int n) {
    return (x >> n) | (x << (32 - n));
}

/* [English] FIPS 180-4 message schedule + compression for one 64-byte
   block. w is a fixed-length stack array (256 bytes). */
static void sha256_block(uint32_t h[8], const uint8_t *p) {
    static const uint32_t K[64] = {
        0x428a2f98u, 0x71374491u, 0xb5c0fbcfu, 0xe9b5dba5u,
        0x3956c25bu, 0x59f111f1u, 0x923f82a4u, 0xab1c5ed5u,
        0xd807aa98u, 0x12835b01u, 0x243185beu, 0x550c7dc3u,
        0x72be5d74u, 0x80deb1feu, 0x9bdc06a7u, 0xc19bf174u,
        0xe49b69c1u, 0xefbe4786u, 0x0fc19dc6u, 0x240ca1ccu,
        0x2de92c6fu, 0x4a7484aau, 0x5cb0a9dcu, 0x76f988dau,
        0x983e5152u, 0xa831c66du, 0xb00327c8u, 0xbf597fc7u,
        0xc6e00bf3u, 0xd5a79147u, 0x06ca6351u, 0x14292967u,
        0x27b70a85u, 0x2e1b2138u, 0x4d2c6dfcu, 0x53380d13u,
        0x650a7354u, 0x766a0abbu, 0x81c2c92eu, 0x92722c85u,
        0xa2bfe8a1u, 0xa81a664bu, 0xc24b8b70u, 0xc76c51a3u,
        0xd192e819u, 0xd6990624u, 0xf40e3585u, 0x106aa070u,
        0x19a4c116u, 0x1e376c08u, 0x2748774cu, 0x34b0bcb5u,
        0x391c0cb3u, 0x4ed8aa4au, 0x5b9cca4fu, 0x682e6ff3u,
        0x748f82eeu, 0x78a5636fu, 0x84c87814u, 0x8cc70208u,
        0x90befffau, 0xa4506cebu, 0xbef9a3f7u, 0xc67178f2u
    };
    uint32_t w[64];
    uint32_t a;
    uint32_t b;
    uint32_t c;
    uint32_t d;
    uint32_t e;
    uint32_t f;
    uint32_t g;
    uint32_t hh;
    uint32_t t1;
    uint32_t t2;
    int i;

    for (i = 0; i < 16; i++) {
        w[i] = ((uint32_t)p[4 * i] << 24) | ((uint32_t)p[4 * i + 1] << 16) |
               ((uint32_t)p[4 * i + 2] << 8) | (uint32_t)p[4 * i + 3];
    }
    for (i = 16; i < 64; i++) {
        uint32_t s0 = sha_rotr(w[i - 15], 7) ^ sha_rotr(w[i - 15], 18) ^ (w[i - 15] >> 3);
        uint32_t s1 = sha_rotr(w[i - 2], 17) ^ sha_rotr(w[i - 2], 19) ^ (w[i - 2] >> 10);
        w[i] = w[i - 16] + s0 + w[i - 7] + s1;
    }

    a = h[0]; b = h[1]; c = h[2]; d = h[3];
    e = h[4]; f = h[5]; g = h[6]; hh = h[7];
    for (i = 0; i < 64; i++) {
        uint32_t S1 = sha_rotr(e, 6) ^ sha_rotr(e, 11) ^ sha_rotr(e, 25);
        uint32_t ch = (e & f) ^ (~e & g);
        uint32_t S0 = sha_rotr(a, 2) ^ sha_rotr(a, 13) ^ sha_rotr(a, 22);
        uint32_t maj = (a & b) ^ (a & c) ^ (b & c);
        t1 = hh + S1 + ch + K[i] + w[i];
        t2 = S0 + maj;
        hh = g; g = f; f = e; e = d + t1;
        d = c; c = b; b = a; a = t1 + t2;
    }
    h[0] += a; h[1] += b; h[2] += c; h[3] += d;
    h[4] += e; h[5] += f; h[6] += g; h[7] += hh;
}

void ac_sha256(const uint8_t *data, int len, uint8_t out[AC_SHA256_DIGEST_SIZE]) {
    static const uint32_t H0[8] = {
        0x6a09e667u, 0xbb67ae85u, 0x3c6ef372u, 0xa54ff53au,
        0x510e527fu, 0x9b05688cu, 0x1f83d9abu, 0x5be0cd19u
    };
    uint32_t h[8];
    uint8_t  block[64];
    uint64_t bitlen;
    int off = 0;
    int rem;
    int i;

    if (len < 0) { len = 0; }
    for (i = 0; i < 8; i++) { h[i] = H0[i]; }
    bitlen = (uint64_t)len * 8u;

    /* 整块直接压缩 */
    while (len - off >= 64) {
        sha256_block(h, data + off);
        off += 64;
    }

    /* 尾块填充：0x80 + 零填充 + 64 位大端比特长度；
       rem >= 56 时长度放不下 → 先补满本块压缩，再补一个全零块 */
    rem = len - off;
    for (i = 0; i < rem; i++) { block[i] = data[off + i]; }
    block[rem] = 0x80;
    if (rem >= 56) {
        for (i = rem + 1; i < 64; i++) { block[i] = 0; }
        sha256_block(h, block);
        for (i = 0; i < 56; i++) { block[i] = 0; }
    } else {
        for (i = rem + 1; i < 56; i++) { block[i] = 0; }
    }
    for (i = 0; i < 8; i++) {
        block[56 + i] = (uint8_t)((bitlen >> (56 - 8 * i)) & 0xFFu);
    }
    sha256_block(h, block);

    /* 摘要按大端输出 */
    for (i = 0; i < 8; i++) {
        out[4 * i + 0] = (uint8_t)(h[i] >> 24);
        out[4 * i + 1] = (uint8_t)(h[i] >> 16);
        out[4 * i + 2] = (uint8_t)(h[i] >> 8);
        out[4 * i + 3] = (uint8_t)(h[i]);
    }
}

/* ==================== Base64 ==================== */

static int8_t s_b64_rev[256];
static int    s_b64_ready = 0;

static void b64_build_rev(void) {
    static const char TBL[64] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    int i;
    for (i = 0; i < 256; i++) { s_b64_rev[i] = -1; }
    for (i = 0; i < 64; i++) { s_b64_rev[(uint8_t)TBL[i]] = (int8_t)i; }
    s_b64_ready = 1;
}

int ac_base64_encode(const uint8_t *data, int len, char *out, int out_size) {
    static const char TBL[64] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    int i = 0;
    int oi = 0;
    if (len < 0) { len = 0; }
    while (i + 3 <= len) {
        uint32_t v = ((uint32_t)data[i] << 16) | ((uint32_t)data[i + 1] << 8) | (uint32_t)data[i + 2];
        if (oi + 4 >= out_size) { return -1; }
        out[oi++] = TBL[(v >> 18) & 63u];
        out[oi++] = TBL[(v >> 12) & 63u];
        out[oi++] = TBL[(v >> 6) & 63u];
        out[oi++] = TBL[v & 63u];
        i += 3;
    }
    if (i < len) {
        /* 1 或 2 字节余数 → 补位 + '=' 填充 */
        uint32_t v = (uint32_t)data[i] << 16;
        if (i + 1 < len) { v |= (uint32_t)data[i + 1] << 8; }
        if (oi + 4 >= out_size) { return -1; }
        out[oi++] = TBL[(v >> 18) & 63u];
        out[oi++] = TBL[(v >> 12) & 63u];
        out[oi++] = (i + 1 < len) ? TBL[(v >> 6) & 63u] : '=';
        out[oi++] = '=';
    }
    out[oi] = '\0';
    return oi;
}

int ac_base64_decode(const char *in, int in_len, uint8_t *out, int out_size) {
    int oi = 0;
    int acc = 0;     /* 已累积的比特（≤13 位，int 足够） */
    int nbits = 0;   /* acc 内有效比特数 */
    int i;
    if (in_len < 0) { return -1; }
    if (!s_b64_ready) { b64_build_rev(); }
    for (i = 0; i < in_len; i++) {
        int v;
        uint8_t c = (uint8_t)in[i];
        if (c == '=') { break; }            /* 填充：停止但不报错 */
        v = s_b64_rev[c];
        if (v < 0) { return -1; }           /* 字母表外字符 */
        acc = (acc << 6) | v;
        nbits += 6;
        if (nbits >= 8) {
            nbits -= 8;
            if (oi >= out_size) { return -1; }
            out[oi] = (uint8_t)((acc >> nbits) & 0xFF);
            oi++;
            acc &= (1 << nbits) - 1;
        }
    }
    return oi;
}

/* ==================== 定点数 ==================== */

int ac_fx_from_double(double v, ac_fixed_t *out) {
    double scaled;
    /* [English] NaN slips through magnitude checks (every comparison is
       false) — reject it explicitly before the int64 cast (UB otherwise).
       +/-Inf fails the magnitude check below. */
    if (v != v) { return 0; }
    if (v > 9.223372036854775e12 || v < -9.223372036854775e12) { return 0; }
    scaled = v * 1000000.0;
    /* 四舍五入到最近的整数（远离零取半）；边界内 scaled+0.5 < 2^63，
       double→int64 转换无 UB（第 3 轮 exact 层同款边界论证） */
    if (scaled >= 0.0) {
        *out = (ac_fixed_t)(scaled + 0.5);
    } else {
        *out = (ac_fixed_t)(scaled - 0.5);
    }
    return 1;
}

double ac_fx_to_double(ac_fixed_t v) {
    return (double)v / 1000000.0;
}

int ac_fx_add(ac_fixed_t a, ac_fixed_t b, ac_fixed_t *out) {
    return ac_safe_add(a, b, out);
}

int ac_fx_sub(ac_fixed_t a, ac_fixed_t b, ac_fixed_t *out) {
    return ac_safe_sub(a, b, out);
}

int ac_fx_mul(ac_fixed_t a, ac_fixed_t b, ac_fixed_t *out) {
    /* (a/1e6) * (b/1e6) = a*b/1e12，结果单位仍是 1e-6 → prod/1e6。
       a*b 必须先落进 int64（safe_mul 预检），故实数域各限 ~3e3；
       不整除时的截断 ≤ 1e-6，是定点数固有的精度损失 */
    ac_int_t prod;
    if (!ac_safe_mul(a, b, &prod)) { return 0; }
    *out = prod / AC_FX_SCALE;
    return 1;
}

int ac_fx_div(ac_fixed_t a, ac_fixed_t b, ac_fixed_t *out) {
    /* (a/1e6) / (b/1e6) = a/b，结果单位 1e-6 → a*1e6/b。
       a*1e6 必须落进 int64（|a| 实数域限 ~9.2e6）；b==-1 时
       prod 不可能为 INT64_MIN（1e6 不整除 2^63），除法无 UB */
    ac_int_t prod;
    if (b == 0) { return 0; }
    if (!ac_safe_mul(a, AC_FX_SCALE, &prod)) { return 0; }
    *out = prod / b;
    return 1;
}

/* ==================== 复位 ==================== */

void ac_util_reset(void) {
    s_crc_ready = 0;
    s_b64_ready = 0;
}
