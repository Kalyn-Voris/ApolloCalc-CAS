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
#include "ac_bigint.h"
#include "ac_exact.h"

/* ==================== CRC32 ==================== */

/* [English] CRC-32 (IEEE 802.3, reflected, poly 0xEDB88320) lookup table.
   Previously built lazily into .bss (1024 B mutable). The table is a pure
   function of a constant polynomial, so it is emitted as a compile-time
   constant: 1024 B move from .bss to .rdata (ROM), and a mutable cache
   that could be clobbered by an unrelated module write disappears. */
static const uint32_t S_CRC_TABLE[256] = {
    0x00000000u, 0x77073096u, 0xEE0E612Cu, 0x990951BAu,
    0x076DC419u, 0x706AF48Fu, 0xE963A535u, 0x9E6495A3u,
    0x0EDB8832u, 0x79DCB8A4u, 0xE0D5E91Eu, 0x97D2D988u,
    0x09B64C2Bu, 0x7EB17CBDu, 0xE7B82D07u, 0x90BF1D91u,
    0x1DB71064u, 0x6AB020F2u, 0xF3B97148u, 0x84BE41DEu,
    0x1ADAD47Du, 0x6DDDE4EBu, 0xF4D4B551u, 0x83D385C7u,
    0x136C9856u, 0x646BA8C0u, 0xFD62F97Au, 0x8A65C9ECu,
    0x14015C4Fu, 0x63066CD9u, 0xFA0F3D63u, 0x8D080DF5u,
    0x3B6E20C8u, 0x4C69105Eu, 0xD56041E4u, 0xA2677172u,
    0x3C03E4D1u, 0x4B04D447u, 0xD20D85FDu, 0xA50AB56Bu,
    0x35B5A8FAu, 0x42B2986Cu, 0xDBBBC9D6u, 0xACBCF940u,
    0x32D86CE3u, 0x45DF5C75u, 0xDCD60DCFu, 0xABD13D59u,
    0x26D930ACu, 0x51DE003Au, 0xC8D75180u, 0xBFD06116u,
    0x21B4F4B5u, 0x56B3C423u, 0xCFBA9599u, 0xB8BDA50Fu,
    0x2802B89Eu, 0x5F058808u, 0xC60CD9B2u, 0xB10BE924u,
    0x2F6F7C87u, 0x58684C11u, 0xC1611DABu, 0xB6662D3Du,
    0x76DC4190u, 0x01DB7106u, 0x98D220BCu, 0xEFD5102Au,
    0x71B18589u, 0x06B6B51Fu, 0x9FBFE4A5u, 0xE8B8D433u,
    0x7807C9A2u, 0x0F00F934u, 0x9609A88Eu, 0xE10E9818u,
    0x7F6A0DBBu, 0x086D3D2Du, 0x91646C97u, 0xE6635C01u,
    0x6B6B51F4u, 0x1C6C6162u, 0x856530D8u, 0xF262004Eu,
    0x6C0695EDu, 0x1B01A57Bu, 0x8208F4C1u, 0xF50FC457u,
    0x65B0D9C6u, 0x12B7E950u, 0x8BBEB8EAu, 0xFCB9887Cu,
    0x62DD1DDFu, 0x15DA2D49u, 0x8CD37CF3u, 0xFBD44C65u,
    0x4DB26158u, 0x3AB551CEu, 0xA3BC0074u, 0xD4BB30E2u,
    0x4ADFA541u, 0x3DD895D7u, 0xA4D1C46Du, 0xD3D6F4FBu,
    0x4369E96Au, 0x346ED9FCu, 0xAD678846u, 0xDA60B8D0u,
    0x44042D73u, 0x33031DE5u, 0xAA0A4C5Fu, 0xDD0D7CC9u,
    0x5005713Cu, 0x270241AAu, 0xBE0B1010u, 0xC90C2086u,
    0x5768B525u, 0x206F85B3u, 0xB966D409u, 0xCE61E49Fu,
    0x5EDEF90Eu, 0x29D9C998u, 0xB0D09822u, 0xC7D7A8B4u,
    0x59B33D17u, 0x2EB40D81u, 0xB7BD5C3Bu, 0xC0BA6CADu,
    0xEDB88320u, 0x9ABFB3B6u, 0x03B6E20Cu, 0x74B1D29Au,
    0xEAD54739u, 0x9DD277AFu, 0x04DB2615u, 0x73DC1683u,
    0xE3630B12u, 0x94643B84u, 0x0D6D6A3Eu, 0x7A6A5AA8u,
    0xE40ECF0Bu, 0x9309FF9Du, 0x0A00AE27u, 0x7D079EB1u,
    0xF00F9344u, 0x8708A3D2u, 0x1E01F268u, 0x6906C2FEu,
    0xF762575Du, 0x806567CBu, 0x196C3671u, 0x6E6B06E7u,
    0xFED41B76u, 0x89D32BE0u, 0x10DA7A5Au, 0x67DD4ACCu,
    0xF9B9DF6Fu, 0x8EBEEFF9u, 0x17B7BE43u, 0x60B08ED5u,
    0xD6D6A3E8u, 0xA1D1937Eu, 0x38D8C2C4u, 0x4FDFF252u,
    0xD1BB67F1u, 0xA6BC5767u, 0x3FB506DDu, 0x48B2364Bu,
    0xD80D2BDAu, 0xAF0A1B4Cu, 0x36034AF6u, 0x41047A60u,
    0xDF60EFC3u, 0xA867DF55u, 0x316E8EEFu, 0x4669BE79u,
    0xCB61B38Cu, 0xBC66831Au, 0x256FD2A0u, 0x5268E236u,
    0xCC0C7795u, 0xBB0B4703u, 0x220216B9u, 0x5505262Fu,
    0xC5BA3BBEu, 0xB2BD0B28u, 0x2BB45A92u, 0x5CB36A04u,
    0xC2D7FFA7u, 0xB5D0CF31u, 0x2CD99E8Bu, 0x5BDEAE1Du,
    0x9B64C2B0u, 0xEC63F226u, 0x756AA39Cu, 0x026D930Au,
    0x9C0906A9u, 0xEB0E363Fu, 0x72076785u, 0x05005713u,
    0x95BF4A82u, 0xE2B87A14u, 0x7BB12BAEu, 0x0CB61B38u,
    0x92D28E9Bu, 0xE5D5BE0Du, 0x7CDCEFB7u, 0x0BDBDF21u,
    0x86D3D2D4u, 0xF1D4E242u, 0x68DDB3F8u, 0x1FDA836Eu,
    0x81BE16CDu, 0xF6B9265Bu, 0x6FB077E1u, 0x18B74777u,
    0x88085AE6u, 0xFF0F6A70u, 0x66063BCAu, 0x11010B5Cu,
    0x8F659EFFu, 0xF862AE69u, 0x616BFFD3u, 0x166CCF45u,
    0xA00AE278u, 0xD70DD2EEu, 0x4E048354u, 0x3903B3C2u,
    0xA7672661u, 0xD06016F7u, 0x4969474Du, 0x3E6E77DBu,
    0xAED16A4Au, 0xD9D65ADCu, 0x40DF0B66u, 0x37D83BF0u,
    0xA9BCAE53u, 0xDEBB9EC5u, 0x47B2CF7Fu, 0x30B5FFE9u,
    0xBDBDF21Cu, 0xCABAC28Au, 0x53B39330u, 0x24B4A3A6u,
    0xBAD03605u, 0xCDD70693u, 0x54DE5729u, 0x23D967BFu,
    0xB3667A2Eu, 0xC4614AB8u, 0x5D681B02u, 0x2A6F2B94u,
    0xB40BBE37u, 0xC30C8EA1u, 0x5A05DF1Bu, 0x2D02EF8Du,
};

uint32_t ac_crc32_update(uint32_t crc, const uint8_t *data, int len) {
    int i;
    crc = ~crc;
    for (i = 0; i < len; i++) {
        crc = S_CRC_TABLE[(crc ^ (uint32_t)data[i]) & 0xFFu] ^ (crc >> 8);
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

/* [English] Base64 reverse lookup (-1 = invalid). Same rationale as
   S_CRC_TABLE: pure function of a constant alphabet -> .rdata, 256 B out
   of .bss, and no mutable cache to corrupt. */
static const int8_t S_B64_REV[256] = {
     -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,
     -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,
     -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  62,  -1,  -1,  -1,  63,
     52,  53,  54,  55,  56,  57,  58,  59,  60,  61,  -1,  -1,  -1,  -1,  -1,  -1,
     -1,   0,   1,   2,   3,   4,   5,   6,   7,   8,   9,  10,  11,  12,  13,  14,
     15,  16,  17,  18,  19,  20,  21,  22,  23,  24,  25,  -1,  -1,  -1,  -1,  -1,
     -1,  26,  27,  28,  29,  30,  31,  32,  33,  34,  35,  36,  37,  38,  39,  40,
     41,  42,  43,  44,  45,  46,  47,  48,  49,  50,  51,  -1,  -1,  -1,  -1,  -1,
     -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,
     -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,
     -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,
     -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,
     -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,
     -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,
     -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,
     -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,
};

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
    for (i = 0; i < in_len; i++) {
        int v;
        uint8_t c = (uint8_t)in[i];
        if (c == '=') { break; }            /* 填充：停止但不报错 */
        v = S_B64_REV[c];
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

/* [English] No mutable caches remain: the CRC and Base64 tables are now
   compile-time constants, so reset has nothing to invalidate. Kept as a
   no-op to preserve the public API. */
void ac_util_reset(void) {
}

/* ==================== C 128KB 第 9 轮：定点桥接 + 单位换算 + 物理常数 ==================== */

int ac_fx_from_bigint(const ac_bigint_t *v, ac_fixed_t *out) {
    ac_bigint_t scaled;
    if (v == 0 || v->sign == 0) { *out = 0; return 1; }
    ac_bi_copy(&scaled, v);
    ac_bi_from_int64(&g_bi_work2, AC_FX_SCALE);   /* scratch 槽（契约见第 6 轮） */
    ac_bi_mul(&scaled, &g_bi_work2, &scaled);     /* out 别名安全（原始积缓冲） */
    return ac_bi_to_int64(&scaled, out);          /* 溢出返 0 */
}

int ac_fx_to_bigint(ac_fixed_t fx, ac_bigint_t *out) {
    /* 整数部分（向零截断）；|fx/1e6| <= ~9.2e12 恒在 int64 内 */
    ac_bi_from_int64(out, fx / AC_FX_SCALE);
    return 1;
}

/* ---- 单位换算 ----
 * 线性类：factor 换算到基准（m / kg / s），跨基准两跳；
 * 温度类：以摄氏为枢轴做仿射换算（含偏移，非法纯乘法陷阱）。
 * 因数表 static const（加载期初始化，SHA/Lanczos 同款）。 */
static const double UNIT_LEN_FACTOR[7] = {
    0.001,          /* mm -> m */
    0.01,           /* cm */
    1.0,            /* m */
    1000.0,         /* km */
    0.0254,         /* in（英寸国际定义） */
    0.3048,         /* ft */
    1609.344        /* mi */
};
static const double UNIT_MASS_FACTOR[5] = {
    0.000001,       /* mg -> kg */
    0.001,          /* g */
    1.0,            /* kg */
    0.45359237,     /* lb（国际磅定义） */
    0.028349523125  /* oz */
};
static const double UNIT_TIME_FACTOR[4] = {
    1.0,            /* s */
    60.0,           /* min */
    3600.0,         /* h */
    86400.0         /* day */
};

static int unit_category(int unit) {
    if (unit >= AC_UNIT_LEN_MM && unit <= AC_UNIT_LEN_MI) { return 0; }
    if (unit >= AC_UNIT_MASS_MG && unit <= AC_UNIT_MASS_OZ) { return 1; }
    if (unit >= AC_UNIT_TIME_S && unit <= AC_UNIT_TIME_DAY) { return 2; }
    if (unit >= AC_UNIT_TEMP_C && unit <= AC_UNIT_TEMP_K) { return 3; }
    return -1;
}

int ac_unit_convert(double value, int from_unit, int to_unit, double *out) {
    int fc = unit_category(from_unit);
    int tc = unit_category(to_unit);
    double base;

    if (fc < 0 || tc < 0 || fc != tc) { return 0; }   /* 越界 / 跨类拒绝 */
    if (out == 0) { return 0; }

    if (fc == 3) {
        /* 温度：先折算到摄氏（仿射），再转目标 */
        double c;
        if (from_unit == AC_UNIT_TEMP_C) { c = value; }
        else if (from_unit == AC_UNIT_TEMP_F) { c = (value - 32.0) * (5.0 / 9.0); }
        else { c = value - 273.15; }   /* K */
        if (to_unit == AC_UNIT_TEMP_C) { base = c; }
        else if (to_unit == AC_UNIT_TEMP_F) { base = c * (9.0 / 5.0) + 32.0; }
        else { base = c + 273.15; }    /* K */
        *out = base;
        return 1;
    }

    /* 线性类：value × f_from = base → base / f_to = 目标值 */
    if (fc == 0) { base = value * UNIT_LEN_FACTOR[from_unit - AC_UNIT_LEN_MM]; }
    else if (fc == 1) { base = value * UNIT_MASS_FACTOR[from_unit - AC_UNIT_MASS_MG]; }
    else { base = value * UNIT_TIME_FACTOR[from_unit - AC_UNIT_TIME_S]; }

    if (tc == 0) { *out = base / UNIT_LEN_FACTOR[to_unit - AC_UNIT_LEN_MM]; }
    else if (tc == 1) { *out = base / UNIT_MASS_FACTOR[to_unit - AC_UNIT_MASS_MG]; }
    else { *out = base / UNIT_TIME_FACTOR[to_unit - AC_UNIT_TIME_S]; }
    return 1;
}

/* ---- 物理常数（SI 2019） ---- */

static const double CONST_TABLE[AC_CONST_COUNT] = {
    299792458.0,        /* c，m/s（定义精确） */
    6.62607015e-34,     /* h，J·s（定义精确） */
    6.02214076e23,      /* N_A，1/mol（定义精确） */
    1.602176634e-19,    /* e，C（定义精确） */
    1.380649e-23,       /* k，J/K（定义精确） */
    6.67430e-11         /* G，m³/(kg·s²)（测量值） */
};

double ac_const_get(int constant_id) {
    if (constant_id < 0 || constant_id >= AC_CONST_COUNT) { return 0.0; }
    return CONST_TABLE[constant_id];
}