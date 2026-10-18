#ifndef AC_EXACT_H
#define AC_EXACT_H

#include "ac_types.h"

/* [English] Three-component exact form: (num / den) * sqrt(rad).
   num, den, rad are int64_t. valid=0 means exact form unavailable. */
extern ac_int_t g_exact_num;
extern ac_int_t g_exact_den;
extern ac_int_t g_exact_rad;
extern int      g_exact_valid;
extern double   g_approx_value;

/* [English] Safe arithmetic primitives (overflow returns 0). */
int ac_safe_mul(ac_int_t a, ac_int_t b, ac_int_t *out);
int ac_safe_add(ac_int_t a, ac_int_t b, ac_int_t *out);
int ac_safe_sub(ac_int_t a, ac_int_t b, ac_int_t *out);

/* [English] GCD for int64_t (iterative Euclidean). */
ac_int_t ac_gcd(ac_int_t a, ac_int_t b);

/* [English] Extract perfect square factors from D. Returns outside coefficient,
   writes remaining squarefree part to g_exact_rad. */
ac_int_t ac_simplify_radical(ac_int_t D);

/* [English] Normalize exact triple to canonical form:
   den > 0, gcd(|num|, den) = 1, rad squarefree, rad=1 iff no radical. */
void ac_exact_normalize(void);

/* [English] Smart constructors. */
void ac_exact_set_fraction(ac_int_t n, ac_int_t d);
void ac_exact_set_radical(ac_int_t k, ac_int_t m);
void ac_exact_set_pure_real(double v);

/* [English] Invalidate exact form, keep approx value. */
void ac_exact_invalidate(void);

/* [English] Reset all exact state. */
void ac_exact_reset(void);

/* [English] Sync approx_value from exact triple. */
void ac_exact_update_approx(void);

/* [English] Arithmetic: current exact op (num2/den2)*sqrt(rad2). */
void ac_exact_add(ac_int_t num2, ac_int_t den2, ac_int_t rad2);
void ac_exact_sub(ac_int_t num2, ac_int_t den2, ac_int_t rad2);
void ac_exact_mul(ac_int_t num2, ac_int_t den2, ac_int_t rad2);
void ac_exact_div(ac_int_t num2, ac_int_t den2, ac_int_t rad2);

/* [English] Extract sqrt(D) as exact triple. */
void ac_exact_from_discriminant(double D);

#endif /* AC_EXACT_H */
