#!/usr/bin/env python3
"""
ApolloCalc-CAS - Pro v1.3 - A1 multivariate GCD reference implementation
========================================================================

WHY THIS SCRIPT EXISTS
----------------------
The C side (ac_pro_multi.c, mv_gcd_prs) kept producing wrong results for
gcd in 2 variables. Symptoms: cases where the first prem already returns
zero were all correct; cases needing >= 2 PRS rounds were all wrong.

Rather than keep guessing inside C, this script pins the mathematics down
with exact rational arithmetic (fractions.Fraction), prints every
intermediate state, and lets C be aligned against it.

WHAT WE LEARNED (recorded here so C does not repeat the mistakes)
-----------------------------------------------------------------
Mistake 1: treating prem(f,g) == 0 as "g divides f".
    In the multivariate case prem works over K(y)[x] (rational function
    field). prem returning zero only means g divides f in K(y)[x]; the
    quotient may carry a denominator in y.
    Example: prem(x^2+2xy+y^2, -2xy-2y^2) == 0, yet -2xy-2y^2 does NOT
    divide x^2+2xy+y^2 in Z[x,y] (the quotient is -(x+y)/(2y)).
    => divisibility must be verified with real polynomial division.

Mistake 2: on degradation, restarting PRS on the other axis from the
    ORIGINAL inputs. That throws away the factors already reduced in the
    first axis.
    => degradation must continue from the CURRENT (f, g) pair.

Mistake 3: computing the common monomial factor from max degrees.
    For f = x^2*y + x the minimum x exponent over all terms is 1 and the
    minimum y exponent is 0 (the "+x" term has no y). Using deg_y would
    wrongly claim a y factor.
    => use the minimum exponent over all terms, per variable.

Mistake 4 (the subtle one): mid-loop normalization.
    make_primitive must strip ONLY the integer content. Stripping
    monomial factors mid-loop changes deg_x and breaks the prem
    invariant (deg_x must decrease by exactly 1 per iteration).

ALGORITHM (as implemented below)
--------------------------------
    gcd_2var(f, g):
        run gcd_pass on the x axis
        if that degrades, run gcd_pass on the y axis using the CURRENT pair
        if both degrade, fall back to the common monomial factor

    gcd_pass(f, g):
        while g != 0:
            R = prem_x(f, g)          # pseudo-remainder, x as main var
            if R == 0:
                if g really divides f: return g
                else: return "degraded" with the current pair
            R = make_primitive(R)     # integer content only
            f, g = g, R
        return f

USAGE
-----
    python tools/ref_prs.py
    python tools/ref_prs.py --quiet
"""

import sys
from fractions import Fraction


# ==================== Polynomial basics ====================
# Representation: dict {(i, j): Fraction}, i = x degree, j = y degree.

def poly_clean(p):
    return {k: v for k, v in p.items() if v != 0}


def poly_add(a, b, sign=1):
    out = dict(a)
    for k, v in b.items():
        nv = out.get(k, Fraction(0)) + sign * v
        if nv == 0:
            out.pop(k, None)
        else:
            out[k] = nv
    return out


def poly_mul(a, b):
    out = {}
    for (i1, j1), c1 in a.items():
        for (i2, j2), c2 in b.items():
            k = (i1 + i2, j1 + j2)
            out[k] = out.get(k, Fraction(0)) + c1 * c2
    return poly_clean(out)


def poly_scale(a, c):
    if c == 0:
        return {}
    return poly_clean({k: v * c for k, v in a.items()})


def poly_is_zero(a):
    return len(poly_clean(a)) == 0


def deg_x(a):
    return max((i for (i, j) in a.keys()), default=-1)


def deg_y(a):
    return max((j for (i, j) in a.keys()), default=-1)


def coef_x(a, i):
    """Coefficient of x^i as a pure-y polynomial {(0, j): c}."""
    return poly_clean({(0, j): c for (ii, j), c in a.items() if ii == i})


def shift_x(a, k):
    return poly_clean({(i + k, j): c for (i, j), c in a.items()})


def mul_y_coef(a, q):
    """Multiply each x-coefficient of a by q (q must be a pure-y poly)."""
    out = {}
    for (i, j), c1 in a.items():
        for (i2, j2), c2 in q.items():
            k = (i, j + j2)
            out[k] = out.get(k, Fraction(0)) + c1 * c2
    return poly_clean(out)


def swap_xy(a):
    return poly_clean({(j, i): c for (i, j), c in a.items()})


def lead_key(k):
    return (k[0] << 8) | k[1]


# ==================== Content ====================

def _gcd(a, b):
    while b:
        a, b = b, a % b
    return a


def int_content(a):
    g = 0
    for v in a.values():
        if v.denominator != 1:
            return 1
        g = _gcd(g, abs(v.numerator))
    return g if g > 0 else 1


def make_primitive(a):
    """Strip integer content only, then make the leading coefficient positive.

    IMPORTANT: do NOT strip monomial factors here. Doing so changes deg_x
    and breaks the prem invariant (deg_x must decrease by exactly 1).
    """
    if poly_is_zero(a):
        return a
    c = int_content(a)
    if c > 1:
        a = poly_clean({k: v / c for k, v in a.items()})
    lead = max(a.keys(), key=lead_key)
    if a[lead] < 0:
        a = poly_scale(a, -1)
    return a


# ==================== Real polynomial division ====================

def poly_div_exact(a, b):
    """Leading-term elimination. Returns (quotient, remainder)."""
    if poly_is_zero(b):
        raise ZeroDivisionError
    q = {}
    r = dict(a)
    guard = 0
    while not poly_is_zero(r):
        lead_r = max(r.keys(), key=lead_key)
        lead_b = max(b.keys(), key=lead_key)
        if lead_r[0] < lead_b[0] or lead_r[1] < lead_b[1]:
            break
        k = (lead_r[0] - lead_b[0], lead_r[1] - lead_b[1])
        c = r[lead_r] / b[lead_b]
        term = {k: c}
        q = poly_add(q, term, sign=1)
        r = poly_add(r, poly_mul(term, b), sign=-1)
        guard += 1
        if guard > 4096:
            raise RuntimeError("poly_div did not terminate")
    return poly_clean(q), poly_clean(r)


def divides(d, a):
    if poly_is_zero(d):
        return False
    _, rem = poly_div_exact(a, d)
    return poly_is_zero(rem)


# ==================== Pseudo-remainder (x as main variable) ====================

def prem_x(f, g, trace=None, depth=0):
    """Pseudo-remainder of f by g with x as the main variable.

    Each iteration:
        lcg = coef_x(g, dg)         # polynomial in y
        lcr = coef_x(R, df)         # polynomial in y
        delta = df - dg
        R = lcg*R - lcr*x^delta*g
    The x^df coefficient is lcg*lcr on both sides, so it cancels exactly
    and deg_x(R) strictly decreases. An assertion enforces this.
    """
    dg = deg_x(g)
    if dg < 0:
        return {}
    R = dict(f)
    it = 0
    while not poly_is_zero(R):
        df = deg_x(R)
        if df < dg:
            break
        lcg = coef_x(g, dg)
        lcr = coef_x(R, df)
        delta = df - dg
        A = mul_y_coef(R, lcg)
        B = mul_y_coef(shift_x(g, delta), lcr)
        R_new = poly_add(A, B, sign=-1)
        it += 1

        if not poly_is_zero(R_new) and deg_x(R_new) >= df:
            raise AssertionError(
                "prem_x: deg_x did not decrease (%d -> %d)" % (df, deg_x(R_new)))

        R = R_new
        if trace is not None:
            trace.append(("prem_iter", depth, df, dg, delta, dict(R), it))
        if it > 64:
            raise RuntimeError("prem_x did not converge")
    return R


# ==================== Single-axis PRS pass ====================

def gcd_pass(f, g, trace=None, depth=0, axis="x"):
    """One PRS pass along the given axis.

    Returns (result, degraded, cur_f, cur_g).
    When degraded is True, cur_f/cur_g hold the CURRENT pair (not the
    original inputs) so the caller can continue on the other axis.
    """
    f = make_primitive(poly_clean(f))
    g = make_primitive(poly_clean(g))

    if poly_is_zero(f):
        return g, False, f, g
    if poly_is_zero(g):
        return f, False, f, g

    guard = 0
    while not poly_is_zero(g):
        dg = deg_x(g)
        df = deg_x(f)

        if dg == 0:
            # g is constant along this axis; pseudo-division cannot proceed.
            if trace is not None:
                trace.append(("degrade", depth, df, dg, dict(f), dict(g), None))
            return f, True, f, g

        R = prem_x(f, g, trace, depth)

        if poly_is_zero(R):
            # prem == 0 over K(y)[x] does not imply divisibility in Z[x,y].
            if divides(g, f):
                if trace is not None:
                    trace.append(("result", depth, dict(g), None, None, None, None))
                return g, False, f, g

            # 【关键补充：用真正的欧几里得除法兜底】
            #
            # prem 返回零只说明「在 K(y)[x] 里整除」。此时正确的做法
            # 不是换轴重来（那会丢掉已约化的因子），而是**对当前的
            # (f, g) 直接做多项式欧几里得**——它们已经被约化到很小的
            # 规模，真实除法几轮就能收敛。
            #
            # 实测：gcd(x²−y², x²+2xy+y²) 退化到
            #       f = x²+2xy+y², g = y(x+y)
            # 对这两个做真实除法：
            #       (x²+2xy+y²) ÷ (xy+y²)  余式在 Q 上是 0
            #       => 商 = (x+y)/y，分子 x+y 即 GCD
            # 所以取「商的分子」作为候选。
            cand = euclid_gcd(f, g, trace, depth)
            if trace is not None:
                trace.append(("prem_zero_not_divisible", depth, dict(f), dict(g),
                              dict(cand), None, None))
            if not poly_is_zero(cand):
                return cand, False, f, g
            return f, True, f, g

        R = make_primitive(R)
        f, g = g, R
        guard += 1
        if guard > 64:
            raise RuntimeError("gcd_pass(%s) did not converge" % axis)

    if trace is not None:
        trace.append(("result", depth, dict(f), None, None, None, None))
    return f, False, f, g


# ==================== Euclidean fallback (rational coefficients) ====================

def euclid_gcd(f, g, trace=None, depth=0):
    """Plain Euclidean GCD over Q[x,y] using real division.

    Used as a fallback when prem returns zero without real divisibility.

    IMPORTANT: the candidate returned at termination must be validated
    against the ORIGINAL f (call it f0), not the current one -- the
    Euclid loop mutates f. A factor that divides the current f may not
    divide f0.
    """
    f0 = poly_clean(f)
    g0 = poly_clean(g)
    f = poly_clean(f)
    g = poly_clean(g)

    guard = 0
    while not poly_is_zero(g):
        _, rem = poly_div_exact(f, g)
        if poly_is_zero(rem):
            return strip_spurious(g, f0, g0)
        rem = clear_denominators(rem)
        rem = make_primitive(rem)
        f, g = g, rem
        guard += 1
        if guard > 32:
            break
    return strip_spurious(make_primitive(g if poly_is_zero(f) else f), f0, g0)


def strip_spurious(cand, f, g=None):
    """Reduce cand to the largest factor that divides BOTH f and g.

    The Euclid / prem machinery can return a candidate carrying spurious
    factors (typically a monomial in y that the other operand lacks).
    This routine searches the divisors of cand for the largest one that
    still divides both inputs.

    Search space (small, since |cand| <= 28 terms):
      · every monomial x^a*y^b with a <= min exponent of cand,
        b <= min exponent of cand
      · every quotient cand / (x^a*y^b)
    Pick the one with the most terms that divides both f and g.
    """
    cand = make_primitive(cand)
    if poly_is_zero(cand):
        return cand

    targets = [f] if g is None else [f, g]

    def ok(p):
        return all(divides(p, t) for t in targets)

    if ok(cand):
        return cand

    def min_exp(p):
        mx = min(i for (i, j) in p.keys())
        my = min(j for (i, j) in p.keys())
        return mx, my

    mx, my = min_exp(cand)
    best = None
    best_terms = -1

    # candidate divisors: cand / (x^a * y^b)
    for a in range(mx + 1):
        for b in range(my + 1):
            mono = {(0, 0): Fraction(1)}
            if a:
                mono = poly_mul(mono, {(a, 0): Fraction(1)})
            if b:
                mono = poly_mul(mono, {(0, b): Fraction(1)})
            q, r = poly_div_exact(cand, mono)
            if not poly_is_zero(r):
                continue
            q = make_primitive(q)
            if poly_is_zero(q):
                continue
            if ok(q) and len(q) > best_terms:
                best = q
                best_terms = len(q)

    if best is not None:
        return best

    # fallback: the common monomial of f and g
    if g is not None:
        return common_monomial(f, g)
    return {(0, 0): Fraction(1)}


def clear_denominators(p):
    """Multiply p by the LCM of all coefficient denominators."""
    if poly_is_zero(p):
        return p
    lcm = 1
    for v in p.values():
        d = v.denominator
        lcm = lcm * d // _gcd(lcm, d)
    if lcm == 1:
        return p
    return poly_clean({k: v * lcm for k, v in p.items()})


# ==================== Common monomial factor ====================

def common_monomial(f, g):
    """Common monomial factor x^a * y^b.

    Uses the MINIMUM exponent over all terms of each polynomial (not the
    maximum degree). For f = x^2*y + x the min y exponent is 0, so no y
    factor is claimed.
    """
    def min_exp(p):
        if poly_is_zero(p):
            return (0, 0)
        mx = min(i for (i, j) in p.keys())
        my = min(j for (i, j) in p.keys())
        return (mx, my)

    fx, fy = min_exp(f)
    gx, gy = min_exp(g)
    a = min(fx, gx)
    b = min(fy, gy)

    cand = {(0, 0): Fraction(1)}
    if a > 0:
        cand = poly_mul(cand, {(a, 0): Fraction(1)})
    if b > 0:
        cand = poly_mul(cand, {(0, b): Fraction(1)})

    if divides(cand, f) and divides(cand, g):
        return cand
    return {(0, 0): Fraction(1)}


# ==================== 2-variable GCD ====================

def gcd_2var(f, g, trace=None, depth=0):
    f = poly_clean(f)
    g = poly_clean(g)

    if poly_is_zero(f):
        return make_primitive(g)
    if poly_is_zero(g):
        return make_primitive(f)

    if trace is not None:
        trace.append(("start", depth, "x", dict(f), dict(g), None, None))

    res, degraded, cf, cg = gcd_pass(f, g, trace, depth, "x")
    if not degraded:
        if trace is not None:
            trace.append(("done_x", depth, dict(res), None, None, None, None))
        return make_primitive(res)

    if trace is not None:
        trace.append(("switch_y", depth, dict(cf), dict(cg), None, None, None))
    fs = swap_xy(cf)
    gs = swap_xy(cg)
    res2, degraded2, _, _ = gcd_pass(fs, gs, trace, depth + 1, "y")
    if not degraded2:
        out = swap_xy(res2)
        # MUST validate: the y-axis result, after swapping back, has to
        # divide BOTH original inputs. Otherwise it carries a factor that
        # only holds in one coordinate system.
        # Measured: gcd(x^2*y-y, x*y^2-x) yields y^2-1 on the y axis,
        # which divides g but not f, so it must be demoted.
        out = strip_spurious(out, f, g)
        if trace is not None:
            trace.append(("done_y", depth, dict(out), None, None, None, None))
        return make_primitive(out)

    mono = common_monomial(cf, cg)
    if trace is not None:
        trace.append(("monomial", depth, dict(mono), None, None, None, None))
    return make_primitive(mono)


# ==================== Formatting and parsing ====================

def poly_str(p, var0="x", var1="y"):
    if poly_is_zero(p):
        return "0"
    terms = sorted(p.items(), key=lambda kv: lead_key(kv[0]), reverse=True)
    parts = []
    for (i, j), c in terms:
        neg = c < 0
        mag = abs(c)
        if mag.denominator == 1:
            coef_s = "" if (mag == 1 and (i or j)) else str(mag.numerator)
        else:
            coef_s = "%d/%d" % (mag.numerator, mag.denominator)

        vars_s = ""
        if i > 0:
            vars_s += var0 if i == 1 else "%s^%d" % (var0, i)
        if j > 0:
            if vars_s:
                vars_s += "*"
            vars_s += var1 if j == 1 else "%s^%d" % (var1, j)

        if coef_s and vars_s:
            body = "%s*%s" % (coef_s, vars_s)
        elif coef_s:
            body = coef_s
        else:
            body = vars_s

        if not parts:
            parts.append(("-" if neg else "") + body)
        else:
            parts.append((" - " if neg else " + ") + body)
    return "".join(parts)


def parse_poly(s, var0="x", var1="y"):
    import re
    s = s.replace(" ", "")
    if not s:
        raise ValueError("empty")
    tokens = re.findall(r'[+-]?[^+-]+', s)
    out = {}
    for tok in tokens:
        sign = 1
        if tok.startswith('+'):
            tok = tok[1:]
        elif tok.startswith('-'):
            sign = -1
            tok = tok[1:]
        coef = 1
        m = re.match(r'^(\d+)', tok)
        if m:
            coef = int(m.group(1))
            tok = tok[m.end():]
        tok = tok.lstrip('*')
        i = j = 0
        while tok:
            if tok[0] in (var0, var1):
                var = tok[0]
                tok = tok[1:]
                e = 1
                if tok.startswith('^'):
                    tok = tok[1:]
                    m = re.match(r'^(\d+)', tok)
                    e = int(m.group(1))
                    tok = tok[m.end():]
                if var == var0:
                    i = e
                else:
                    j = e
            else:
                break
            tok = tok.lstrip('*')
        if tok:
            raise ValueError("cannot parse remainder %r" % tok)
        k = (i, j)
        out[k] = out.get(k, Fraction(0)) + sign * coef
    return poly_clean(out)


# ==================== Test cases ====================
# All expectations verified by hand factorization.

CASES = [
    ("x - y",             "x + y",             "1"),
    ("x^2 - y^2",         "x - y",             "x - y"),
    ("x^2 - y^2",         "x^2 + 2*x*y + y^2", "x + y"),
    ("x*y - 1",           "x^2*y - x",         "x*y - 1"),
    ("2*x^2 - 2*y^2",     "4*x^2 - 4*x*y",     "x - y"),
    # x^3+y^3 = (x+y)(x^2-xy+y^2); g itself divides f, so gcd = g
    ("x^3 + y^3",         "x^2 - x*y + y^2",   "x^2 - x*y + y^2"),
    ("x^2 + y^2 - 1",     "x^2 - y",           "1"),
    ("x^2*y - y",         "x*y^2 - x",         "1"),
    ("x^2*y",             "x*y^2",             "x*y"),
    # x^2y+x = x(xy+1), xy+x = x(y+1); the only common factor is x
    ("x^2*y + x",         "x*y + x",           "x"),
    ("x*y + x",           "x*y + x",           "x*y + x"),
    ("x*y - x",           "y - 1",             "y - 1"),
]


def run_case(fsrc, gsrc, expect, verbose=True):
    f = parse_poly(fsrc)
    g = parse_poly(gsrc)
    trace = []
    res = gcd_2var(f, g, trace)

    if verbose:
        print("=" * 74)
        print("gcd( %s , %s )" % (fsrc, gsrc))
        print("  f = %s      deg_x=%d deg_y=%d" % (poly_str(f), deg_x(f), deg_y(f)))
        print("  g = %s      deg_x=%d deg_y=%d" % (poly_str(g), deg_x(g), deg_y(g)))
        print("-" * 74)
        for ev in trace:
            kind = ev[0]
            pad = "  " * (ev[1] if len(ev) > 1 and isinstance(ev[1], int) else 0)
            if kind == "start":
                print("  [start] axis=%s" % ev[2])
            elif kind == "prem_iter":
                _, depth, df, dg, delta, R, it = ev
                print("%s  prem#%d: df=%d dg=%d delta=%d -> R = %s  (deg_x=%d)"
                      % (pad, it, df, dg, delta, poly_str(R), deg_x(R)))
            elif kind == "degrade":
                _, depth, df, dg, ff, gg, _ = ev
                print("%s  ** DEGRADE: deg_x(f)=%d deg_x(g)=%d **" % (pad, df, dg))
            elif kind == "prem_zero_not_divisible":
                _, depth, ff, gg, _, _, _ = ev
                print("%s  ** prem=0 but NOT divisible: f=%s g=%s **"
                      % (pad, poly_str(ff), poly_str(gg)))
            elif kind == "switch_y":
                _, depth, ff, gg, _, _, _ = ev
                print("%s  ** SWITCH to y axis: f=%s g=%s **"
                      % (pad, poly_str(ff), poly_str(gg)))
            elif kind == "monomial":
                _, depth, mm, _, _, _, _ = ev
                print("%s  [monomial] %s" % (pad, poly_str(mm)))
            elif kind == "result":
                _, depth, rr, _, _, _, _ = ev
                print("%s  [result] %s" % (pad, poly_str(rr)))
            elif kind in ("done_x", "done_y"):
                _, depth, rr, _, _, _, _ = ev
                print("%s  [%s] %s" % (pad, kind, poly_str(rr)))

    got = poly_str(res)
    ok = (got.replace(" ", "") == expect.replace(" ", ""))
    print("-" * 74)
    print("  RESULT: %s" % got)
    print("  EXPECT: %s" % expect)
    print("  %s" % ("OK" if ok else "MISMATCH"))
    return ok, got


def main():
    quiet = "--quiet" in sys.argv
    passed = 0
    failed = 0
    results = []
    for fsrc, gsrc, expect in CASES:
        ok, got = run_case(fsrc, gsrc, expect, verbose=not quiet)
        if ok:
            passed += 1
        else:
            failed += 1
        results.append((fsrc, gsrc, expect, got, ok))
        if not quiet:
            print()

    print("=" * 74)
    print("SUMMARY: %d passed, %d failed, %d total" % (passed, failed, len(CASES)))
    print("=" * 74)
    if failed:
        print("\nFailures:")
        for fsrc, gsrc, expect, got, ok in results:
            if not ok:
                print("  gcd(%s, %s) = %s   (expected %s)" % (fsrc, gsrc, got, expect))
    return 0 if failed == 0 else 1


if __name__ == "__main__":
    sys.exit(main())
