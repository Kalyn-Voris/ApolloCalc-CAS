/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Kalyn-Voris (ApolloCalc-CAS). See LICENSE at the repository root. */
#ifndef AC_COMPLEX_H
#define AC_COMPLEX_H

#include "ac_types.h"

/* [English] Complex operand and result registers (mirrors core_complex.js
   _aRe/_aIm/_bRe/_bIm/_tmpRe/_tmpIm). Inputs A/B, result TMP. */
extern double g_c_a_re;
extern double g_c_a_im;
extern double g_c_b_re;
extern double g_c_b_im;
extern double g_c_tmp_re;
extern double g_c_tmp_im;

/* [English] Basic arithmetic: TMP = A op B. */
void ac_c_add(void);
void ac_c_sub(void);
void ac_c_mul(void);
void ac_c_div(void);   /* B == 0 -> TMP = NaN + NaN*i */

/* [English] Magnitude / argument / conjugate. */
double ac_c_abs(void);        /* returns |A| */
double ac_c_arg(void);        /* returns atan2(A_im, A_re) */
void   ac_c_conj(void);       /* TMP = conj(A) */

/* [English] Complex square root (three-branch: zero, pure real, polar).
   TMP = sqrt(A). The other root is the negation; caller chooses. */
void ac_c_sqrt(void);

/* [English] A^n for integer n (square-and-multiply). n < 0 -> reciprocal first. */
void ac_c_pow_int(int n);

/* [English] Reset all six registers. */
void ac_c_reset(void);

#endif /* AC_COMPLEX_H */
