#ifndef AC_UTIL_H
#define AC_UTIL_H

#include "ac_types.h"

/* ============================================================
 * ApolloCalc-CAS · ac_util.h — 工具层（第 9 轮，C 独有）
 * CRC32 / SHA-256 / Base64 / 定点数。JS 64KB / JS 128KB 均无此层
 * （MD5 / AES / RSA 不做，留给 C 128KB 或永不）。
 * 所有函数作用于调用方提供的缓冲区，无堆、无递归。
 * ============================================================ */

/* ==================== CRC32 ==================== */
/* [English] Standard CRC-32 (polynomial 0xEDB88320, reflected,
   zlib-style pre/post conditioning so update() chains).
   Table is lazily initialized on first call. */
uint32_t ac_crc32(const uint8_t *data, int len);
uint32_t ac_crc32_update(uint32_t crc, const uint8_t *data, int len);

/* ==================== SHA-256 ==================== */
/* [English] SHA-256 digest, one-shot (FIPS 180-4).
   Caller provides a 32-byte output buffer. */
#define AC_SHA256_DIGEST_SIZE 32
void ac_sha256(const uint8_t *data, int len, uint8_t out[AC_SHA256_DIGEST_SIZE]);

/* ==================== Base64 ==================== */
/* [English] Base64 encode (standard "+/" alphabet). Output buffer must
   be at least ((len + 2) / 3) * 4 + 1 bytes (including NUL terminator).
   Returns number of characters written (excluding NUL), or -1 on
   overflow. len < 0 is treated as 0 (empty output). */
int ac_base64_encode(const uint8_t *data, int len, char *out, int out_size);

/* [English] Base64 decode. Output buffer size must be >= ((in_len + 3) / 4) * 3.
   Decoding stops at the first '=' padding (stray bits after it are
   ignored, not an error); any other character outside the alphabet
   returns -1. Returns number of bytes written, or -1 on invalid
   input / output overflow. */
int ac_base64_decode(const char *in, int in_len, uint8_t *out, int out_size);

/* ==================== Fixed-point ==================== */
/* [English] Fixed-point arithmetic with 6 decimal digits (scale = 10^6).
   Value stored as int64_t: real_value = fx / 10^6.
   Range: +/-9.22e12 with 1e-6 precision.
   Inherent precision notes (documented, by design):
   - mul: a*b must fit int64 before dividing by 10^6, so operands are
     effectively limited to ~3e3 in real units each;
   - div: a*10^6 must fit int64, so |a| is limited to ~9.2e6 real;
   - single-op truncation error <= 1e-6; chains can accumulate to ~1e-3.
   Higher precision needs the decimal bignum layer (C 128KB). */
#define AC_FX_SCALE     1000000LL
#define AC_FX_MAX       9223372036854LL       /* INT64_MAX / 10^6 */
#define AC_FX_MIN      -9223372036854LL       /* symmetric */

typedef int64_t ac_fixed_t;

/* [English] Construct from double (rounds to nearest, half away from
   zero). Returns 1 on success, 0 on overflow (|v| beyond ~9.223e12)
   or non-finite input (NaN/Inf). */
int ac_fx_from_double(double v, ac_fixed_t *out);

/* [English] Convert to double (for display). */
double ac_fx_to_double(ac_fixed_t v);

/* [English] Arithmetic. Return 1 on success, 0 on overflow.
   Overflow pre-checks reuse ac_exact.h safe primitives (INT64_MIN-safe:
   they never negate operands). */
int ac_fx_add(ac_fixed_t a, ac_fixed_t b, ac_fixed_t *out);
int ac_fx_sub(ac_fixed_t a, ac_fixed_t b, ac_fixed_t *out);
int ac_fx_mul(ac_fixed_t a, ac_fixed_t b, ac_fixed_t *out);
int ac_fx_div(ac_fixed_t a, ac_fixed_t b, ac_fixed_t *out);   /* b == 0 -> 0 */

/* [English] Reset internal state (lazily built CRC / Base64 tables are
   dropped; they are rebuilt on next use). */
void ac_util_reset(void);

#endif /* AC_UTIL_H */
