#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
# Copyright (c) 2026 Kalyn-Voris (ApolloCalc-CAS). See LICENSE at the repository root.
"""ref-complex.py — F1 复变函数扩展契约参考值生成器（tools/ 文件级归属：feat/math-ext）

按 docs/contracts/complex-ext-v25.md §9 的溯源口径，用 mpmath（40 位十进制精度）
在**精确 float64 输入**上计算 tests/complex-functions.json 全部用例的期望值：

  1. 输入保真：Python float 即 IEEE binary64；带符号零的分量按其符号扰动
     ±1e-35（C99 G 系列的割线点值 = 沿零符号一侧的解析极限；1e-35 远低于
     float64 舍入，等价于割线点本身）。不扰动会被 mpmath 自身的割线约定
     干扰（实测 mp.atan(-0+2i) 与 cmath 不一致）。
  2. 误差码：按契约 §5/§7 的奇点表判 EDOM(-1)；|期望| > 1e308 判 EOVER(-2)；
     cnthroot k<=0 判 EPARAM(-3)。契约 JSON 的错误行是**表驱动**的，不依赖
     mpmath 抛错。
  3. cgamma：mpmath.gamma 任意精度复 Gamma（与文献 Γ(1+i) 值核对一致）。

运行：python tools/ref-complex.py  （仓库根或任意 CWD，输出到 stdout）
重生成 tests/complex-functions.json 后人工 diff 审查、随契约提交冻结。
"""
import json
import math
import sys

from mpmath import mp, mpf, mpc

mp.dps = 40

EPS = mpf("1e-35")
OVF_LIMIT = 1e308

# 与 ac_special.c / core_special.js 同一张 Lanczos 表（仅用于注释溯源，
# 期望值全部来自 mpmath，不经过本表）。
LITERATURE_CHECKS = {
    "gamma(1+i)": (0.498015668118356042713691117462, -0.154949828301810685124955130484),
}


NO_CUT_FNS = {"csin", "ccos", "ctan", "csinh", "ccosh", "ctanh", "cgamma"}


def to_mpc(x, y, perturb=True):
    """float64 输入 -> mpmath 复数；带符号零分量按符号扰动 ±1e-35。

    扰动只对含分支割线的函数有意义（C99 G 系列割线点值 = 沿零符号一侧的
    解析极限）。csin/ccos/ctan/csinh/ccosh/ctanh 是整函数/亚纯函数（无割线，
    只有极点），cgamma 同（只有极点）——三者带符号零输入直接原样求值，
    否则割点邻域 |f'|~1e32 的极点行会被扰动污染出 1e-3 量级的假偏移。"""
    if not perturb:
        return mpc(mpf(x), mpf(y))
    sx = mpf(x) if x != 0 else (EPS if math.copysign(1.0, x) > 0 else -EPS)
    sy = mpf(y) if y != 0 else (EPS if math.copysign(1.0, y) > 0 else -EPS)
    return mpc(sx, sy)


def table_value(fn, x, y, u, v, k):
    """z=0 的表驱动行（契约 §5）：0^0:=1、0^正实:=0、0 的主根:=0。
    返回 None 表示该行不在表内，走 mpmath 求值。"""
    if fn == "cpow" and x == 0 and y == 0:
        if u == 0 and v == 0:
            return (1.0, 0.0)
        if u > 0 and v == 0:
            return (0.0, 0.0)
        return None  # EDOM，由 is_edom 判定
    if fn == "cnthroot" and x == 0 and y == 0:
        return (0.0, 0.0)
    return None


def ref(fn, x, y, u=0.0, v=0.0, k=0):
    """返回 (re, im) 期望值（float64）。奇点/溢出由调用方按表判定后再取值。"""
    tv = table_value(fn, x, y, u, v, k)
    if tv is not None:
        return tv[0] + tv[1] * 1j
    if fn == "cpow":
        # 底数 z（≠0，z=0 已表驱动）必须扰动：负实轴割线 ±0 侧决定 ln 分支；
        # 指数 w 无割线语义，原样求值。
        w = to_mpc(u, v, perturb=False)
        return complex(mp.exp(w * mp.log(to_mpc(x, y))))
    z = to_mpc(x, y, perturb=fn not in NO_CUT_FNS)
    if fn == "cnthroot":
        return complex(mp.power(z, mpf(1) / mpf(k)))
    f = {
        "csin": mp.sin, "ccos": mp.cos, "ctan": mp.tan,
        "csinh": mp.sinh, "ccosh": mp.cosh, "ctanh": mp.tanh,
        "casin": mp.asin, "cacos": mp.acos, "catan": mp.atan,
        "casinh": mp.asinh, "cacosh": mp.acosh, "catanh": mp.atanh,
        "cgamma": mp.gamma,
    }[fn]
    return complex(f(z))


def is_edom(fn, x, y, u=0.0, v=0.0, k=0):
    """契约 §5 奇点表：返回 True 表示该输入在本契约下返回 AC_PRO_CX_EDOM(-1)。"""
    if math.isnan(x) or math.isnan(y) or math.isinf(x) or math.isinf(y):
        return True
    if fn == "catan":
        return x == 0 and abs(y) == 1
    if fn == "catanh":
        return y == 0 and abs(x) == 1
    if fn == "cgamma":
        return y == 0 and x <= 0 and x == math.floor(x)
    if fn == "cpow":
        if x == 0 and y == 0:
            if u == 0 and v == 0:
                return False  # 0^0 := 1
            return not (u > 0 and v == 0)
        return False
    return False


def case(cid, fn, z, w=None, k=None, note=""):
    x, y = z
    u, v = (w or (0.0, 0.0))
    if k is not None and k <= 0:
        want = {"err": -3}
    elif is_edom(fn, x, y, u, v, k or 0):
        want = {"err": -1}
    else:
        c = ref(fn, x, y, u, v, k or 0)
        if abs(c.real) > OVF_LIMIT or abs(c.imag) > OVF_LIMIT:
            want = {"err": -2}
        else:
            want = {"re": c.real, "im": c.imag}
    row = {"id": cid, "fn": fn, "z": [x, y]}
    if w is not None:
        row["w"] = [u, v]
    if k is not None:
        row["k"] = k
    row["want"] = want
    if note:
        row["note"] = note
    return row


SQRT2 = math.sqrt(2.0)
SQRT3 = math.sqrt(3.0)
PI = math.pi
HALF_PI = math.pi / 2.0
E = math.exp(1.0)

CASES = [
    # ---- csin（8）----
    case("cx-csin-01", "csin", (0.0, 1.0), note="csin(i)=i*sinh(1) 恒等式锚"),
    case("cx-csin-02", "csin", (0.0, 0.0), note="csin(0)=0"),
    case("cx-csin-03", "csin", (1.0, 0.0), note="实轴退化 sin(1)"),
    case("cx-csin-04", "csin", (0.5, 0.5)),
    case("cx-csin-05", "csin", (-1.0, 2.0)),
    case("cx-csin-06", "csin", (3.0, -2.5)),
    case("cx-csin-07", "csin", (0.1, -9.0), note="大 |Im| 域内"),
    case("cx-csin-08", "csin", (-0.0, 0.0), note="-0 边角"),
    # ---- ccos（6）----
    case("cx-ccos-01", "ccos", (0.0, 0.0), note="ccos(0)=1"),
    case("cx-ccos-02", "ccos", (0.0, 1.0), note="ccos(i)=cosh(1)"),
    case("cx-ccos-03", "ccos", (1.0, 1.0)),
    case("cx-ccos-04", "ccos", (-2.0, -0.5)),
    case("cx-ccos-05", "ccos", (5.0, 5.0)),
    case("cx-ccos-06", "ccos", (PI, 0.0), note="ccos(pi)=-1"),
    # ---- ctan（7，含极点边界大有限值）----
    case("cx-ctan-01", "ctan", (0.0, 0.0), note="ctan(0)=0"),
    case("cx-ctan-02", "ctan", (1.0, 0.0), note="实轴 tan(1)"),
    case("cx-ctan-03", "ctan", (0.0, 1.0), note="ctan(i)=i*tanh(1)"),
    case("cx-ctan-04", "ctan", (1.0, 1.0)),
    case("cx-ctan-05", "ctan", (HALF_PI, 0.0), note="实极点：比值形大有限值，非 EDOM"),
    case("cx-ctan-06", "ctan", (0.0, 2.0), note="和式形路径 |y|>1"),
    case("cx-ctan-07", "ctan", (-3.0, 0.5)),
    # ---- csinh（5）----
    case("cx-csinh-01", "csinh", (0.0, 0.0), note="csinh(0)=0"),
    case("cx-csinh-02", "csinh", (0.0, 1.0), note="csinh(i)=i*sin(1)"),
    case("cx-csinh-03", "csinh", (1.0, 1.0)),
    case("cx-csinh-04", "csinh", (-2.0, 3.0)),
    case("cx-csinh-05", "csinh", (0.25, -7.0)),
    # ---- ccosh（5）----
    case("cx-ccosh-01", "ccosh", (0.0, 0.0), note="ccosh(0)=1"),
    case("cx-ccosh-02", "ccosh", (0.0, 1.0), note="ccosh(i)=cos(1)"),
    case("cx-ccosh-03", "ccosh", (2.0, -1.0)),
    case("cx-ccosh-04", "ccosh", (-0.5, 8.0)),
    case("cx-ccosh-05", "ccosh", (4.0, 4.0)),
    # ---- ctanh（6，含虚极点边界大有限值）----
    case("cx-ctanh-01", "ctanh", (0.0, 0.0), note="ctanh(0)=0"),
    case("cx-ctanh-02", "ctanh", (0.0, 1.0), note="ctanh(i)=i*tan(1)"),
    case("cx-ctanh-03", "ctanh", (1.0, 1.0)),
    case("cx-ctanh-04", "ctanh", (2.0, 0.0), note="实轴 tanh(2)，和式形路径"),
    case("cx-ctanh-05", "ctanh", (0.0, HALF_PI), note="虚极点：比值形大有限值"),
    case("cx-ctanh-06", "ctanh", (-3.0, -2.0)),
    # ---- casin（8）----
    case("cx-casin-01", "casin", (0.0, 0.0), note="casin(0)=0"),
    case("cx-casin-02", "casin", (1.0, 0.0), note="割线端点 casin(1)=pi/2"),
    case("cx-casin-03", "casin", (0.5, 0.0), note="实轴 pi/6"),
    case("cx-casin-04", "casin", (0.0, 0.5), note="casin(0.5i)=i*asinh(0.5)"),
    case("cx-casin-05", "casin", (2.0, 0.0), note="割线上侧 +i*acosh(2)"),
    case("cx-casin-06", "casin", (2.0, -0.0), note="割线下侧共轭"),
    case("cx-casin-07", "casin", (-2.0, 0.0), note="割线上侧 -pi/2+i*acosh(2)"),
    case("cx-casin-08", "casin", (1.0, 1.0)),
    # ---- cacos（7）----
    case("cx-cacos-01", "cacos", (0.0, 0.0), note="cacos(0)=pi/2"),
    case("cx-cacos-02", "cacos", (1.0, 0.0), note="cacos(1)=0"),
    case("cx-cacos-03", "cacos", (2.0, 0.0), note="割线上侧 -i*acosh(2)"),
    case("cx-cacos-04", "cacos", (-2.0, -0.0), note="割线下侧 pi+i*acosh(2)"),
    case("cx-cacos-05", "cacos", (0.5, 0.0), note="实轴 pi/3"),
    case("cx-cacos-06", "cacos", (0.0, 1.0), note="cacos(i)=pi/2-i*asinh(1)"),
    case("cx-cacos-07", "cacos", (-1.0, 3.0)),
    # ---- catan（9，含 2 条 EDOM）----
    case("cx-catan-01", "catan", (0.0, 0.0), note="catan(0)=0"),
    case("cx-catan-02", "catan", (1.0, 0.0), note="实轴 pi/4"),
    case("cx-catan-03", "catan", (0.0, 0.5), note="catan(0.5i)=i*atanh(0.5)"),
    case("cx-catan-04", "catan", (2.0, 0.0), note="实割线外侧仍实值"),
    case("cx-catan-05", "catan", (0.0, 2.0), note="虚割线 pi/2+i*0.25ln9"),
    case("cx-catan-06", "catan", (1.0, 1.0)),
    case("cx-catan-07", "catan", (-3.0, -0.25)),
    case("cx-catan-08", "catan", (0.0, 1.0), note="支点 +i：EDOM"),
    case("cx-catan-09", "catan", (0.0, -1.0), note="支点 -i：EDOM"),
    # ---- casinh（6）----
    case("cx-casinh-01", "casinh", (0.0, 0.0), note="casinh(0)=0"),
    case("cx-casinh-02", "casinh", (1.0, 0.0), note="实轴 asinh(1)"),
    case("cx-casinh-03", "casinh", (0.0, 1.0), note="支点值 casinh(i)=i*pi/2（有限）"),
    case("cx-casinh-04", "casinh", (0.0, 2.0), note="虚割线 acosh(2)+i*pi/2"),
    case("cx-casinh-05", "casinh", (1.0, 1.0)),
    case("cx-casinh-06", "casinh", (-0.5, 4.0)),
    # ---- cacosh（8）----
    case("cx-cacosh-01", "cacosh", (2.0, 0.0), note="实轴 acosh(2)"),
    case("cx-cacosh-02", "cacosh", (-2.0, 0.0), note="割线上侧 acosh(2)+i*pi"),
    case("cx-cacosh-03", "cacosh", (-2.0, -0.0), note="割线下侧共轭"),
    case("cx-cacosh-04", "cacosh", (0.0, 0.0), note="割线内 cacosh(0)=i*pi/2"),
    case("cx-cacosh-05", "cacosh", (0.5, 0.0), note="割线内 i*pi/3"),
    case("cx-cacosh-06", "cacosh", (1.0, 0.0), note="cacosh(1)=0（支点有限）"),
    case("cx-cacosh-07", "cacosh", (0.0, 1.0)),
    case("cx-cacosh-08", "cacosh", (3.0, -4.0)),
    # ---- catanh（9，含 2 条 EDOM）----
    case("cx-catanh-01", "catanh", (0.0, 0.0), note="catanh(0)=0"),
    case("cx-catanh-02", "catanh", (0.5, 0.0), note="实轴 atanh(0.5)"),
    case("cx-catanh-03", "catanh", (2.0, 0.0), note="实割线上侧 0.5ln3+i*pi/2"),
    case("cx-catanh-04", "catanh", (2.0, -0.0), note="实割线下侧共轭"),
    case("cx-catanh-05", "catanh", (0.0, 2.0), note="catanh(2i)=i*atan(2)"),
    case("cx-catanh-06", "catanh", (0.5, 0.5)),
    case("cx-catanh-07", "catanh", (-3.0, 1.0)),
    case("cx-catanh-08", "catanh", (1.0, 0.0), note="支点 +1：EDOM"),
    case("cx-catanh-09", "catanh", (-1.0, -0.0), note="支点 -1：EDOM"),
    # ---- cpow（11）----
    case("cx-cpow-01", "cpow", (E, 0.0), w=(0.0, PI), note="欧拉锚 e^(i*pi)=-1"),
    case("cx-cpow-02", "cpow", (2.0, 0.0), w=(3.0, 0.0), note="2^3=8"),
    case("cx-cpow-03", "cpow", (0.0, 1.0), w=(0.0, 1.0), note="i^i=e^(-pi/2) 实正值"),
    case("cx-cpow-04", "cpow", (1.0, 0.0), w=(2.5, -3.0), note="1^w=1"),
    case("cx-cpow-05", "cpow", (0.0, 0.0), w=(2.0, 0.0), note="0^正实=0"),
    case("cx-cpow-06", "cpow", (0.0, 0.0), w=(0.0, 0.0), note="0^0:=1（契约 §5）"),
    case("cx-cpow-07", "cpow", (0.0, 0.0), w=(-1.0, 0.0), note="0^负实：EDOM"),
    case("cx-cpow-08", "cpow", (0.0, 0.0), w=(0.0, 1.0), note="0^纯虚：EDOM"),
    case("cx-cpow-09", "cpow", (-1.0, 0.0), w=(0.5, 0.0), note="(-1)^0.5=i"),
    case("cx-cpow-10", "cpow", (2.0, 0.0), w=(1.0, 1.0), note="2^(1+i)"),
    case("cx-cpow-11", "cpow", (10.0, 0.0), w=(400.0, 0.0), note="10^400 溢出：EOVER"),
    # ---- cnthroot（10，含 2 条 EPARAM）----
    case("cx-cnth-01", "cnthroot", (8.0, 0.0), k=3, note="立方根 2"),
    case("cx-cnth-02", "cnthroot", (-8.0, 0.0), k=3, note="主值 1+i*sqrt(3)"),
    case("cx-cnth-03", "cnthroot", (-8.0, -0.0), k=3, note="-0 侧共轭"),
    case("cx-cnth-04", "cnthroot", (0.0, 1.0), k=2, note="sqrt(i)=(1+i)/sqrt(2)"),
    case("cx-cnth-05", "cnthroot", (1.0, 0.0), k=4, note="1 的四次主根=1"),
    case("cx-cnth-06", "cnthroot", (0.0, 0.0), k=5, note="0 的主根=0"),
    case("cx-cnth-07", "cnthroot", (2.0, 3.0), k=7),
    case("cx-cnth-08", "cnthroot", (10.0, 0.0), k=1, note="k=1 恒等"),
    case("cx-cnth-09", "cnthroot", (8.0, 0.0), k=0, note="k=0：EPARAM"),
    case("cx-cnth-10", "cnthroot", (8.0, 0.0), k=-2, note="k<0：EPARAM"),
    # ---- cgamma（12，含 3 条 EDOM + 1 条 EOVER）----
    case("cx-cgamma-01", "cgamma", (1.0, 0.0), note="Gamma(1)=1"),
    case("cx-cgamma-02", "cgamma", (5.0, 0.0), note="Gamma(5)=24"),
    case("cx-cgamma-03", "cgamma", (0.5, 0.0), note="Gamma(0.5)=sqrt(pi)"),
    case("cx-cgamma-04", "cgamma", (-0.5, 0.0), note="反射 Gamma(-0.5)=-2*sqrt(pi)"),
    case("cx-cgamma-05", "cgamma", (-1.5, 0.0), note="反射 Gamma(-1.5)=4*sqrt(pi)/3"),
    case("cx-cgamma-06", "cgamma", (1.0, 1.0), note="文献锚 Gamma(1+i)"),
    case("cx-cgamma-07", "cgamma", (0.5, 2.0)),
    case("cx-cgamma-08", "cgamma", (2.0, -3.0)),
    case("cx-cgamma-09", "cgamma", (0.1, -1.0)),
    case("cx-cgamma-10", "cgamma", (0.0, 0.0), note="极点 0：EDOM"),
    case("cx-cgamma-11", "cgamma", (-2.0, 0.0), note="极点 -2：EDOM"),
    case("cx-cgamma-12", "cgamma", (200.0, 0.0), note="Gamma(200) 溢出：EOVER"),
]


def main():
    # Windows 重定向默认 cp936；JSON 交付一律 UTF-8
    try:
        sys.stdout.reconfigure(encoding="utf-8")
    except AttributeError:
        pass
    # 溯源自检：mpmath 与文献 Γ(1+i) 一致才允许生成（防 mpmath 版本漂移）
    g = complex(mp.gamma(mpc(1, 1)))
    lit = LITERATURE_CHECKS["gamma(1+i)"]
    if abs(g.real - lit[0]) > 1e-28 or abs(g.imag - lit[1]) > 1e-28:
        print("LITERATURE CHECK FAILED: mpmath gamma(1+i) drifted", file=sys.stderr)
        sys.exit(1)

    doc = {
        "meta": {
            "name": "complex-functions",
            "feature": "v2.5 F1 复变函数扩展（feat/math-ext）",
            "contract": "docs/contracts/complex-ext-v25.md",
            "frozen": "2026-11-03",
            "tolerance": "err = |a-b| / max(1, |a|, |b|) <= 1e-9（逐分量，相对-绝对混合）；错误码精确比较；±0 不比符号",
            "errCodes": {"OK": 0, "EDOM": -1, "EOVER": -2, "EPARAM": -3},
            "provenance": "期望值由 tools/ref-complex.py 用 mpmath 40dps 在精确 float64 输入上生成；带符号零分量按 C99 G 系列割线约定沿零符号扰动 ±1e-35；cgamma 另经文献 Γ(1+i) 自检",
        },
        "cases": CASES,
    }
    json.dump(doc, sys.stdout, indent=2, ensure_ascii=False)
    sys.stdout.write("\n")


if __name__ == "__main__":
    main()
