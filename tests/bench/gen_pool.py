#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""ApolloCalc-CAS · tests/bench/pool.json 生成器（一次性构建工具，非 bench 运行时依赖）

用途
    确定性生成 100 万次压测的表达式池：
        parse 250 / eval 250 / solve 100 / exact 50 = 650 条
    生成结果写死为 pool.json；bench 脚本运行时只读取，绝不做字符串拼接
    （字符串生成开销会污染测量，见任务书「设计 1」）。

约束
    - 所有表达式必须能被四版本内核解析，唯一例外是标注 ["128KB"] 的双曲函数
      （sinh/cosh/tanh/asinh/acosh/atanh 仅 JS128/C128 支持，64KB 两版报 parseError=1）。
    - 变量只有 x（内核不支持 y，实测 "x+y" 四版本均报错）。
    - eval 池每条必须给出使函数域有效的 x（避免测到域错误短路而非真实函数调用）。

用法
    python gen_pool.py          # 写出同目录 pool.json
"""

import json
import math
import os
import sys

V128 = ["128KB"]


# 条目构造
def P(src):
    """parse 池通用条目：裸字符串（无参数、无版本限制）。"""
    return src


def P128(src):
    """parse 池 128KB 专属条目。"""
    return {"src": src, "versions": V128}


def E(src, x, angle=None):
    """eval 池条目；angle 缺省 DEG（与内核缺省一致），仅 RAD 时显式落盘。"""
    d = {"src": src, "x": x}
    if angle is not None and angle != "DEG":
        d["angleMode"] = angle
    return d


def E128(src, x, angle=None):
    d = E(src, x, angle)
    d["versions"] = V128
    return d


def S(src, degree):
    """solve / exact 池条目。"""
    return {"src": src, "degree": degree}


# parse 池（250）
def build_parse():
    # 1) 纯数字 10：整数 / 小数 / 科学计数法（e 与 E）/ 前导零 / 大数
    nums = [
        "0", "123", "3.14", "1e5", "1.5e-10",
        "2.7e10", "0.001", "42", "1E3", "999999",
    ]

    # 2) 变量 + 运算符 50：隐式乘法 / 幂 / 一元负号 / 高次 / 系数变体
    vars_ops = [
        "x", "2x", "x^2", "2x^2+3x-5", "x+1", "x-1", "x*2", "x/2", "x^3", "x^4",
        "x^5", "3x+2", "5x-10", "-x", "+x", "x^2+x", "x^2-x", "2x+1", "2x-1", "4x^2",
        "9x^2-1", "x*x", "x/x", "x+2x", "3x-2x", "x^2*3", "x^3+1", "x^3-1", "6x^2", "x^2/2",
        "x^-1", "x^0.5", "2.5x", "100x", "7x", "12x^3", "5x^5", "x^2+x+1", "x^2-x-1", "2x^2+3x+1",
        "3x^2-2x-5", "x^4-1", "x^4+1", "x^3-6x^2", "x-2", "2-x", "x*0", "0*x", "x+0", "1*x",
    ]

    # 3) 括号 40：嵌套 / 隐式乘法 / 连续括号 / 深度变体
    parens = [
        "(2+3)", "((1+2)*3)", "2(3+4)", "(1+2)(3+4)", "(((x)))", "(x)", "(x+1)", "(x-1)", "(2x)", "(x^2)",
        "((x))", "((((1+2)*3)-4)/5)^2", "(1+(2*(3+(4*5))))", "(1+2)*(3+4)", "2*((3+4))", "((2+3)*(4+5))", "(x+1)(x-1)", "(x+1)^2", "(x-1)^2", "(2x+1)(x-3)",
        "-(2x)", "-(x+1)", "(-x)", "(0)", "(1)", "((0))", "3(4)(5)", "(2)(3)(4)", "((1))", "(((1)))",
        "(x^2+1)", "(x^2-1)", "(2+3)(4+5)(6+7)", "((x+1)*(x+2))", "2(x+1)(x+2)", "(1/2)", "(3/4)x", "(x/2)+1", "(x+1)/2", "((x+1)/2)^2",
    ]

    # 4) 函数 80：12 个基础函数 + 嵌套组合（不含双曲）
    funcs = [
        "sin(x)", "sin(0)", "sin(pi)", "sin(x+1)", "sin(2x)", "sin(x^2)", "sin(1/x)", "sin(-x)",
        "cos(x)", "cos(0)", "cos(pi)", "cos(x+1)", "cos(2x)", "cos(x^2)", "cos(1/x)",
        "tan(x)", "tan(0)", "tan(x/2)", "tan(2x)", "tan(x+1)", "tan(pi/4)",
        "asin(x)", "asin(0.5)", "asin(1)", "asin(-0.5)", "asin(x/2)",
        "acos(x)", "acos(0.5)", "acos(1)", "acos(-1)", "acos(x/2)",
        "atan(x)", "atan(1)", "atan(0)", "atan(-1)", "atan(2x)",
        "atan2(3,4)", "atan2(x,1)", "atan2(1,x)", "atan2(-1,2)", "atan2(x,x)",
        "sqrt(x)", "sqrt(2)", "sqrt(x^2)", "sqrt(x+1)", "sqrt(abs(x))",
        "ln(x)", "ln(2)", "ln(e)", "ln(x+1)", "ln(abs(x))",
        "log(x)", "log(10)", "log(100)", "log(x+1)", "log(abs(x))",
        "exp(x)", "exp(0)", "exp(1)", "exp(-x)", "exp(x^2)",
        "abs(x)", "abs(-5)", "abs(x-1)", "abs(2x)", "abs(-x)",
        "sin(cos(x))", "cos(sin(x))", "exp(ln(x))", "ln(exp(x))", "sqrt(x^2+1)",
        "sin(x)+cos(x)", "sin(x)*cos(x)", "sin(x)/cos(x)", "tan(x)+1", "exp(x)*ln(x)",
        "log(x)+ln(x)", "abs(sin(x))", "sqrt(abs(x))+1", "atan2(sin(x),cos(x))",
    ]

    # 5) 双曲 30（仅 128KB）：6 函数 × 5 实参
    hyper = []
    for fname, args in [
        ("sinh", ["x", "0", "1", "x+1", "2x"]),
        ("cosh", ["x", "0", "1", "x+1", "2x"]),
        ("tanh", ["x", "0", "1", "x+1", "2x"]),
        ("asinh", ["x", "0", "1", "x+1", "2x"]),
        ("acosh", ["x", "1", "2", "x+1", "2x"]),
        ("atanh", ["x", "0", "0.5", "x+1", "x/2"]),
    ]:
        for a in args:
            hyper.append(P128(fname + "(" + a + ")"))

    # 6) 方程形式 40：含等号、移项、右端带 x
    eqs = [
        "x^2-5x+6=0", "2x+6=9", "x^3-1=0", "x^2=9", "x^2-4x+4=0",
        "2x^2+3x-2=0", "3x^2-5x+2=0", "4x^2-1=0", "6x^2-x-1=0", "x^2+2x=0",
        "x^2-2=0", "x^2-3=0", "x^2-5=0", "x^2-49=0", "x^2+1=0",
        "x^2+2x+5=0", "x^2-4x+13=0", "x^2-2x-1=0", "x^2-6x+9=0", "x^2=2x+3",
        "x^2-x-1=0", "x^3-6x^2+11x-6=0", "x^3=8", "x^3-6x^2+12x-8=0", "x^3-3x=0",
        "2x^3-x-1=0", "x^3+x^2+x+1=0", "x^4-5x^2+4=0", "x^4-1=0", "x^4+5x^2+4=0",
        "x^4-4x^3+6x^2-4x+1=0", "x^4+4=0", "x^5-1=0", "5x-10=0", "3x+1=0",
        "x^4=16", "x/2=3", "x^2+3x+1=0", "2x^4-x^3-14x^2-5x+6=0", "x^4-10x^3+35x^2-50x+24=0",
    ]

    items = [P(s) for s in nums + vars_ops + parens + funcs] + hyper + [P(s) for s in eqs]
    assert len(nums) == 10, len(nums)
    assert len(vars_ops) == 50, len(vars_ops)
    assert len(parens) == 40, len(parens)
    assert len(funcs) == 80, len(funcs)
    assert len(hyper) == 30, len(hyper)
    assert len(eqs) == 40, len(eqs)
    return items


# eval 池（250）
def build_eval():
    # 1) 多项式 80：16 式 × 5 个 x
    poly_srcs = [
        "x^2-5x+6", "x^3-6x^2+11x-6", "x^4-5x^2+4", "x^2+2x+5", "x^3-3x",
        "2x^4-x^3-14x^2-5x+6", "x^5-1", "x^2-2", "x^3-2x-5", "x^4-10x^3+35x^2-50x+24",
        "3x^2-5x+2", "x^3+x^2+x+1", "x^4+4", "x^2-x-1", "x^3-6x^2+12x-8",
        "x^4-4x^3+6x^2-4x+1",
    ]
    poly_xs = [0.0, 0.5, 1.5, 2.5, -1.2]
    poly = [E(s, x) for s in poly_srcs for x in poly_xs]

    # 2) 三角 60：正/余弦与正切 DEG+RAD 混合（避开 tan 的 90° 奇点）+ 反三角
    trig = []
    for fname in ("sin", "cos", "tan"):
        for x in (0.0, 30.0, 45.0, 60.0, 135.0, 180.0):
            trig.append(E(fname + "(x)", x, "DEG"))
        for x in (0.0, 0.5, 1.0, 1.5, 2.0, 3.0):
            trig.append(E(fname + "(x)", x, "RAD"))
    for x in (0.0, 0.5, 1.0, -0.5, -1.0, 0.25):
        trig.append(E("asin(x)", x))
        trig.append(E("acos(x)", x))
    for x in (0.0, 1.0, -1.0, 2.0, -2.0, 0.5):
        trig.append(E("atan(x)", x))
    for a, b in ((3.0, 4.0), (1.0, 1.0), (-1.0, 2.0), (0.0, 1.0), (1.0, 0.0), (2.0, -3.0)):
        trig.append(E("atan2(" + repr(a) + "," + repr(b) + ")", 0.0))

    # 3) 对数 / 指数 40：20 单函数 + 20 复合
    logexp = []
    for x in (1.0, 2.0, math.e, 0.5, 10.0):
        logexp.append(E("ln(x)", x))
    for x in (1.0, 10.0, 100.0, 0.5, 1000.0):
        logexp.append(E("log(x)", x))
    for x in (0.0, 1.0, -1.0, 2.0, 0.5):
        logexp.append(E("exp(x)", x))
    for x in (0.0, 1.0, 2.0, 4.0, 16.0):
        logexp.append(E("sqrt(x)", x))
    logexp += [
        E("ln(x)+log(x)", 2.0), E("exp(x)*ln(x)", 2.5), E("sqrt(x)*ln(x)", 4.0),
        E("log(x)*exp(x)", 1.5), E("ln(x^2)", 3.0), E("exp(x^2)", 1.2),
        E("sqrt(x+1)", 3.0), E("ln(x+1)", 2.0), E("log(x+1)", 9.0),
        E("exp(-x)", 1.5), E("sqrt(abs(x))", -4.0), E("ln(abs(x))", -2.0),
        E("log(abs(x))", -10.0), E("exp(x)/x", 2.0), E("ln(x)/x", 3.0),
        E("x*ln(x)", 2.5), E("x^2*exp(x)", 1.0), E("sqrt(x)*exp(x)", 1.5),
        E("ln(x)*log(x)", 5.0), E("exp(ln(x)+1)", 2.0),
    ]

    # 4) 双曲 30（仅 128KB）：6 函数 × 5 个域内 x
    hyper = []
    for fname, xs in [
        ("sinh", (0.0, 1.0, -1.0, 2.0, 0.5)),
        ("cosh", (0.0, 1.0, -1.0, 2.0, 0.5)),
        ("tanh", (0.0, 1.0, -1.0, 2.0, 0.5)),
        ("asinh", (0.0, 1.0, -1.0, 2.0, 0.5)),
        ("acosh", (1.0, 2.0, 3.0, 1.5, 10.0)),
        ("atanh", (0.0, 0.5, -0.5, 0.25, 0.75)),
    ]:
        for x in xs:
            hyper.append(E128(fname + "(x)", x))

    # 5) 复合函数 40
    composite = [
        E("sin(x)+cos(x)", 0.7), E("sin(x)*cos(x)", 1.2), E("sin(x)/cos(x)", 0.5),
        E("tan(x)+1", 0.5), E("sin(x^2)", 1.5), E("cos(2x)", 0.7),
        E("exp(sin(x))", 1.0), E("ln(cos(x))", 0.5), E("sqrt(sin(x))", 1.0),
        E("abs(sin(x))", -1.0), E("sin(exp(x))", 0.5), E("cos(ln(x))", 2.0),
        E("exp(x)*sin(x)", 1.5), E("x*sin(x)", 2.0), E("x^2*cos(x)", 1.5),
        E("sin(x)+x", 1.0), E("cos(x)-x", 0.5), E("tan(x)*x", 0.5),
        E("atan2(sin(x),cos(x))", 0.7), E("atan2(x,1)", 2.0),
        E("asin(sin(x))", 0.5), E("acos(cos(x))", 1.0), E("atan(tan(x))", 0.5),
        E("log(x^2)", 3.0), E("exp(x)/x^2", 1.5), E("sqrt(x^2+1)", 2.0),
        E("ln(x^2+1)", 1.5), E("abs(x^2-1)", 2.0), E("sin(x)^2+cos(x)^2", 0.7),
        E("(x+1)*(x-1)", 3.0), E("x^3-2x+1", 1.5), E("x^4-3x^2+1", 0.8),
        E("2x^3-x^2+3x-1", 0.6), E("5x^2-3x+2", 1.1), E("x^5-x^4+x-1", 1.3),
        E("3x^3+2x^2-x+4", 0.9), E("sin(x)+sin(2x)+sin(3x)", 0.4), E("cos(x)*cos(2x)", 0.6),
        E("exp(-x^2)", 1.0), E("1/(1+x^2)", 2.0),
    ]

    items = poly + trig + logexp + hyper + composite
    assert len(poly) == 80, len(poly)
    assert len(trig) == 60, len(trig)
    assert len(logexp) == 40, len(logexp)
    assert len(hyper) == 30, len(hyper)
    assert len(composite) == 40, len(composite)
    return items


# solve 池（100）
def build_solve():
    quad = [
        # 整数根
        "x^2-5x+6=0", "x^2=9", "x^2-6x+9=0", "x^2-7x+12=0", "x^2+x-12=0",
        "x^2-9x+20=0", "x^2=2x+3", "x^2-4x+4=0", "x^2+2x=0", "x^2-49=0",
        # 分数 / 有理根
        "2x^2+3x-2=0", "3x^2-5x+2=0", "4x^2-1=0", "6x^2-x-1=0", "2x^2-7x+3=0",
        "6x^2-5x+1=0", "4x^2+4x+1=0", "8x^2-2x-1=0", "3x^2+7x+2=0", "9x^2-1=0",
        # 无理根
        "x^2-2=0", "x^2-3=0", "x^2-5=0", "x^2-6=0", "x^2-10=0",
        "x^2-13=0", "2x^2-1=0", "3x^2-1=0", "5x^2-2=0", "x^2-2x-1=0",
        # 复根
        "x^2+1=0", "x^2+2x+5=0", "x^2-4x+13=0", "x^2+4x+13=0", "x^2+2x+10=0",
        "x^2-8x+17=0",
        # 混合（无理部分，四版本通用）
        "x^2-x-1=0", "x^2+3x+1=0", "x^2-3x+1=0", "x^2+5x+1=0",
    ]

    cubic = [
        # 整数根 / 有理根
        "x^3-6x^2+11x-6=0", "x^3=8", "x^3-6x^2+12x-8=0", "x^3-3x=0", "x^3-1=0",
        "x^3-27=0", "x^3-4x=0", "x^3-9x=0", "x^3-7x+6=0", "x^3-4x^2+x+6=0",
        "x^3-2x^2-5x+6=0", "x^3-12x+16=0", "x^3-13x+12=0", "x^3-5x^2+2x+8=0",
        "x^3-6x+4=0", "x^3+4x^2+x-6=0", "x^3-x^2-4x+4=0", "x^3+6x^2+11x+6=0",
        "x^3-2x^2-x+2=0", "x^3-16x=0", "x^3+2x^2-x-2=0",
        # 重根 / 完全立方
        "x^3+3x^2+3x+1=0",
        # 不可约 / Cardano（无有理根）
        "2x^3-x-1=0", "x^3-2x-5=0", "2x^3-3x^2-3x+2=0", "3x^3-3x=0", "4x^3-3x=0",
        # 复根
        "x^3+1=0", "x^3+x^2+x+1=0", "x^3-2=0",
    ]

    quartic = [
        # 双二次 / 退化
        "x^4-5x^2+4=0", "x^4-1=0", "x^4=16", "x^4-13x^2+36=0", "x^4-16=0",
        "x^4-2x^2-3=0", "x^4-3x^2+2=0", "x^4+2x^2+1=0", "x^4-8x^2+16=0", "x^4-6x^2+5=0",
        "x^4-7x^2+12=0", "x^4-11x^2+18=0", "x^4-9x^2+20=0", "x^4-x^2-2=0", "x^4-4x^2+3=0",
        "3x^4-4x^2+1=0", "x^4-10x^2+9=0", "x^4-25x^2+144=0", "2x^4-9x^2+4=0",
        # 一般情形（含重根 / 整数根 / 有理根）
        "x^4-10x^3+35x^2-50x+24=0", "2x^4-x^3-14x^2-5x+6=0", "x^4-4x^3+6x^2-4x+1=0",
        "x^4-5x^3+5x^2+5x-6=0", "x^4-2x^3-3x^2+4x+4=0", "x^4-3x^3-3x^2+11x-6=0",
        "x^4-2x^3-13x^2+14x+24=0", "x^4-6x^3+11x^2-6x=0", "x^4-5x^3+9x^2-7x+2=0",
        # 复根
        "x^4+5x^2+4=0", "x^4+4=0",
    ]

    items = [S(s, 2) for s in quad] + [S(s, 3) for s in cubic] + [S(s, 4) for s in quartic]
    assert len(quad) == 40, len(quad)
    assert len(cubic) == 30, len(cubic)
    assert len(quartic) == 30, len(quartic)
    return items


# exact 池（50）
def build_exact():
    # 纯分数根 20（判别式为完全平方，精确形式为有理数）
    frac = [
        "x^2-5x+6=0", "3x^2-2x-5=0", "2x^2+3x-2=0", "3x^2-5x+2=0", "4x^2-1=0",
        "6x^2-x-1=0", "x^2+2x=0", "x^2-4x+4=0", "x^2-6x+9=0", "2x^2-8=0",
        "4x^2-9=0", "9x^2-1=0", "x^2-7x+12=0", "x^2+x-12=0", "x^2-9x+20=0",
        "2x^2-7x+3=0", "6x^2-5x+1=0", "4x^2+4x+1=0", "8x^2-2x-1=0", "3x^2+7x+2=0",
    ]

    # 纯根式根 20（判别式为正非完全平方）
    rad = [
        "x^2-2=0", "x^2-3=0", "x^2-5=0", "x^2-6=0", "x^2-7=0",
        "x^2-10=0", "x^2-11=0", "x^2-13=0", "x^2-15=0", "x^2-17=0",
        "2x^2-1=0", "3x^2-1=0", "2x^2-3=0", "3x^2-2=0", "4x^2-3=0",
        "x^2-8=0", "x^2-12=0", "x^2-18=0", "x^2-20=0", "5x^2-2=0",
    ]

    # 五分量混合形式 10（四版本通用；64KB 两版 valid=0，128KB 两版 valid=1）
    # 全部满足 b^2-4ac 为正非完全平方 → 根为 (a+b√c)/d 型
    mixed = [
        "x^2-x-1=0", "x^2+3x+1=0", "x^2+x-1=0", "x^2-3x+1=0", "x^2+5x+1=0",
        "2x^2+2x-1=0", "x^2-4x-1=0", "x^2+7x+1=0", "3x^2+3x-1=0", "x^2-6x-1=0",
    ]

    items = [S(s, 2) for s in frac + rad + mixed]
    assert len(frac) == 20, len(frac)
    assert len(rad) == 20, len(rad)
    assert len(mixed) == 10, len(mixed)
    return items


def main():
    pool = {
        "parse": build_parse(),
        "eval": build_eval(),
        "solve": build_solve(),
        "exact": build_exact(),
        "iterations": {
            "parse": 1000000,
            "eval": 1000000,
            "solve": 1000000,
            "exact": 1000000,
        },
    }

    expected = {"parse": 250, "eval": 250, "solve": 100, "exact": 50}
    ok = True
    for stage, want in expected.items():
        got = len(pool[stage])
        n128 = sum(
            1 for it in pool[stage]
            if isinstance(it, dict) and "128KB" in it.get("versions", [])
        )
        print("  %-6s %3d/%3d  (128KB-only %d, 64KB-effective %d)"
              % (stage, got, want, n128, got - n128))
        if got != want:
            ok = False
    if not ok:
        print("ERROR: 池大小与设计不符", file=sys.stderr)
        return 1

    out = os.path.join(os.path.dirname(os.path.abspath(__file__)), "pool.json")
    with open(out, "w", encoding="utf-8") as f:
        json.dump(pool, f, ensure_ascii=False, indent=2)
        f.write("\n")
    print("wrote %s (%d bytes)" % (out, os.path.getsize(out)))
    return 0


if __name__ == "__main__":
    sys.exit(main())
