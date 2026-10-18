#ifndef AC_MATH_H
#define AC_MATH_H

#include "ac_types.h"

/* [English] Shared double constants, bit-identical to JS Math.PI / Math.E and
   to DEG_TO_RAD = Math.PI/180, RAD_TO_DEG = 180/Math.PI. Lived in
   ac_evaluator.c (round 2); owned by the math layer since round 5.
   ac_evaluator.c still consumes AC_PI / AC_E for T_CONST tokens. */
#define AC_PI         3.141592653589793
#define AC_E          2.718281828459045
#define AC_DEG_TO_RAD 0.017453292519943295
#define AC_RAD_TO_DEG 57.29577951308232

/* [English] Angle mode: 1=DEG (default, mirrors JS angleMode="DEG"), 0=RAD.
   Migrated from ac_evaluator.c (round 5) — now owned by the math layer. */
extern int g_angle_deg;

/* [English] Dispatch a function by ID. atan2 uses both args; others only arg1.
   Unknown ids return NaN. Lives here since round 5 (was static in
   ac_evaluator.c rounds 2-4). */
double ac_apply_function(int func_id, double arg1, double arg2);

/* [English] Trig functions with DEG/RAD awareness (called by the dispatcher;
   kept public for direct white-box tests). Inverse trig / atan2 output the
   angle in the current mode's unit. */
double ac_math_sin(double v);
double ac_math_cos(double v);
double ac_math_tan(double v);
double ac_math_asin(double v);
double ac_math_acos(double v);
double ac_math_atan(double v);
double ac_math_atan2(double y, double x);

/* [English] Non-trig functions (direct libm passthrough for now). */
double ac_math_ln(double v);
double ac_math_log10(double v);
double ac_math_exp(double v);
double ac_math_abs(double v);

/* [English] LUT acceleration (compile-time switch AC_MATH_USE_LUT).
   If enabled, sin/cos use a 1024-entry table with linear interpolation.
   Precision: worst case ~4.7e-6 (h^2/8, h = 2*pi/1024; measured ~2.1e-6 at
   30 deg) — coarser than libm's ~1e-15. Off by default; enable only when
   that trade is acceptable. */
#ifdef AC_MATH_USE_LUT
void ac_math_lut_init(void);
/* [English] 1 once ac_math_lut_init has filled the tables, else 0. */
int  ac_math_lut_ready(void);
#endif

#endif /* AC_MATH_H */
