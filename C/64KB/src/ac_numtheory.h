/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Kalyn-Voris (ApolloCalc-CAS). See LICENSE at the repository root. */
#ifndef AC_NUMTHEORY_H
#define AC_NUMTHEORY_H

#include "ac_types.h"

/* [English] Number-theory layer (C-only feature, no JS equivalent).
   All operations on int64_t; overflow detected via safe primitives.
   Independent from ac_exact's ac_gcd by design (module isolation). */

/* [English] Greatest common divisor (non-negative result).
   gcd(0, 0) = 0; gcd(a, 0) = |a|.
   INT64_MIN input is clamped to INT64_MAX (same convention as ac_gcd:
   negating INT64_MIN overflows; unreachable from guarded call sites). */
ac_int_t ac_nt_gcd(ac_int_t a, ac_int_t b);

/* [English] Least common multiple. lcm(a, b) = |a*b| / gcd(a, b).
   Returns 0 on overflow or when either input is 0.
   INT64_MIN input returns 0: any lcm involving 2^63 is a multiple of
   2^63 and therefore never representable in int64_t. */
ac_int_t ac_nt_lcm(ac_int_t a, ac_int_t b);

/* [English] Primality test: 6k+-1 wheel trial division.
   Returns 1 if prime, 0 otherwise. n < 2 -> 0.
   Cost is O(sqrt(n)): fine up to ~10^12; a 10^18-scale prime would need
   ~10^9 iterations (no Miller-Rabin in the 64KB edition — planned for
   C 128KB). Callers should keep arguments at 10^9 scale or below. */
int ac_nt_is_prime(ac_int_t n);

/* [English] Factorization output buffer (max 16 distinct prime factors;
   sufficient for n < 2^63 since the product of the first 16 primes
   (32589158477190044730) exceeds 2^63). */
#define AC_NT_MAX_FACTORS 16

extern ac_int_t g_nt_primes[AC_NT_MAX_FACTORS];   /* distinct prime factors */
extern int      g_nt_exps[AC_NT_MAX_FACTORS];     /* corresponding exponents */
extern int      g_nt_count;                       /* number of distinct factors */

/* [English] Prime factorization (trial division: strip 2, then odd divisors
   up to sqrt(remaining)). Writes g_nt_primes[0..count-1] and
   g_nt_exps[0..count-1] in ascending prime order. Returns 1 on success,
   0 on failure (n <= 0, or factor count exceeds AC_NT_MAX_FACTORS — the
   latter is unreachable for int64_t but kept for safety).
   Performance boundary: 98% of inputs (small factors) resolve fast, but a
   semiprime with two large factors (e.g. product of two 10^9-scale primes)
   may take minutes. This is the inherent limit of trial division; the
   64KB edition trades that worst case for code size. */
int ac_nt_factorize(ac_int_t n);

/* [English] Binomial coefficient C(n, r). Iterative multiply-divide with
   per-step gcd reduction so every multiplication yields an exact
   intermediate binomial C(n-r+i, i) <= C(n, r) — a representable answer is
   never falsely rejected by an intermediate overflow (naive
   multiply-first would overflow on e.g. C(66,33) = 7219428434016265740,
   which fits int64_t). Returns 1 on success, 0 on overflow or invalid
   input (r < 0, r > n, n < 0). Result written to *out. */
int ac_nt_ncr(int n, int r, ac_int_t *out);

/* [English] Permutation P(n, r) = n! / (n-r)!. Pure iterative multiply with
   per-step overflow precheck (intermediates P(n,k) rise monotonically to
   P(n, r), so no false rejection). Same contract as ac_nt_ncr. */
int ac_nt_npr(int n, int r, ac_int_t *out);

/* [English] Reset factorization state. */
void ac_nt_reset(void);

#endif /* AC_NUMTHEORY_H */
