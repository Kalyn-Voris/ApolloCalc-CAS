/* ApolloCalc · core_integrate.js — 符号积分
   (v2.4.0 功能 7,JS 侧;对齐 C/128KB-ProTEXT/src/ac_pro_integrate.c B2)

   职责:积分表与 C 侧逐项对齐(128 项,kind 全 family:常数/幂/倒数/
   代数核/基本函数/函数幂/x^n·f(x)/二次核 QUAD_L·QUAD_K·XQUAD/倒数
   函数族/乘积恒等式/ln 核,anti 模板串与 kind/func/pow 三元组逐字搬运);
   分部积分对齐 C 侧固定两层(parts_left 2→1→0 关死,无同形自调用);
   错误码语义复刻 AC_PRO_INT_*(0/−1..−5 同值,返回值即错误码)。

   管线(与 C 逐段对应)
   parseExpression(C 侧自带 pro_int 词法器;JS 复用内核解析器,接受/
   拒绝差异见下)→ toRPN → RPN→IR 翻译(构造期归一)→ integrate 驱动
   (显式栈三阶段:线性展开/128 项表匹配/固定两层分部/多项式归一)
   → 预算复核(cap 256,分部 448)→ pro_int_emit 同款渲染。

   构造期归一(C pro_int_add/sub/mul/div/neg/pow 逐条移植,全迭代):
   add/sub:常数折叠、0±x、右侧 ADD/SUB/NEG 子树摊平成左结合扁平链
     (项表收集 + 符号翻转循环,消 C 的 add/sub 尾递归);
   mul:负号外提(奇偶计数)、常数×常数折叠、常数头左收(≤1e6 钳制)、
     常数因子外提(wrap 栈回灌,消 C R5..R8 递归)、常数×和差分配展开
     (工作表,消 C R9/R10 递归);
   div:常数分母折叠/连除合并(尾环);pow:e∈[0,128]、u^0→1、u^1→u、
     常数底循环乘(≤1e6 钳制)。输出形态与 C 同为左结合扁平链。

   与 C 的文法边界(如实成文)
   输入经内核 parseExpression(能力超集),翻译期对齐 C 拒绝项:
   非整常量(小数)→ EPARSE、π/裸 e → EPARSE、abs/atan2 → EPARSE、
   负指数(x^-1)→ EPARSE、指数 >96(非模板)→ EDEGREE、常量 >1e6
   → EDEGREE —— 共享用例上与 C 同输入同码。
   已知超集偏差(JS 更宽,对拍用例避开):空白分隔数字("2 3")按
   内核隐式乘法接受(C EPARSE);e^(完整表达式) 按标准优先级读
   (C 的 e^ 只吃一个 pow 层,如 "e^(2x)" C 读作 exp(2)·x、JS 读作
   exp(2x));x^2^3 右结合读成 (x^2)^3(C 文法止于单指数 → EPARSE)。
   模板解析走同一 toRPN + RPN→IR;模板词法器仅认模板子集:整常量、
   x/u/a/b/n/m、16 个函数名、e^、四则与幂括号;隐式乘法注入与 C
   pro_int 词法器同则(NUM 后 NUM 不注入 → EPARSE)。

   预算(C 同值)
   表匹配路径 ≤ 256 节点;分部固定两层每层 ≤ 96,总计 448 ≤ 池 512;
   超限 EBUDGET;池满同 EBUDGET。渲染缓冲 768 字符(C PRO_INT_OUTBUF
   同值)超限 → EOVER。

   错误码(= AC_PRO_INT_*)
   0 OK / −1 EPARSE(语法/空输入/非法字符/负指数/小数/π/裸 e)/
   −2 ETABLE(表+两层分部+多项式归一全未命中)/ −3 EBUDGET /
   −4 EOVER(渲染上限,常规输入不可达,防御位)/
   −5 EDEGREE(指数 >96、常量 >1e6)。失败时 getString() 恒 ""。

   零 GC:28 组 TypedArray 加载期一次分配(allocCount=28,明细见声明);
   全路径无对象/数组字面量、无递归(归一化/匹配/线性判定/多项式归一/
   驱动/渲染全部显式栈或工作表)、热路径零字符串操作;积分表 128 条
   anti 模板存单条 \x01 分隔常量串 + 偏移表,kind/func/pow 存常量串
   加载期解析(等价 C 的 static const .rdata),加载期断言恰 128 项。
   依赖:parseExpression/toRPN/_rpnToken/_evalError、funcName(渲染名)。
   导出三通道:Node require / 浏览器 <script> 顶层 var / 内核拼接 ESM。
 */

var ApolloIntegrate = (function () {
  "use strict";

  /* 常量(C 同值) */
  var POOL_NODES = 512;
  var BUDGET_TABLE = 256;
  var BUDGET_PARTS = 96;
  var PARTS_LAYERS = 2;
  var BUDGET_TOTAL = BUDGET_TABLE + BUDGET_PARTS * PARTS_LAYERS;   /* 448 */
  var MAX_POW_IN = 20;
  var MAX_POW_PARSE = 96;
  var MAX_POW_BUILD = 128;
  var MAX_CONST = 1000000;
  var OUTBUF = 768;
  var POLY_MAX = 16;

  /* IR 操作码(C PRO_INT_OP_* 同值) */
  var OP_CONST = 0;
  var OP_VAR = 1;
  var OP_ADD = 2;
  var OP_SUB = 3;
  var OP_MUL = 4;
  var OP_DIV = 5;
  var OP_POW = 6;
  var OP_NEG = 7;
  var OP_FUNC = 8;

  /* 模板变量 id(C PRO_INT_V_* 同值) */
  var V_X = 0;
  var V_U = 1;
  var V_A = 2;
  var V_B = 3;
  var V_N = 4;
  var V_M = 5;

  /* 表 kind(C PRO_INT_K_* 同值) */
  var T_CONST = 0;
  var T_XN = 1;
  var T_INV = 2;
  var T_INV2 = 3;
  var T_ATAN_KER = 4;
  var T_ASIN_KER = 5;
  var T_ASINH_KER = 6;
  var T_ACOSH_KER = 7;
  var T_ATANH_KER = 8;
  var T_CIRCLE = 9;
  var T_HYPER = 10;
  var T_FUNC = 11;
  var T_FUNC_POW = 12;
  var T_XN_FUNC = 13;
  var T_QUAD_L = 14;
  var T_QUAD_K = 15;
  var T_XQUAD = 16;
  var T_FUNC_INV = 17;
  var T_FUNC_DIV = 18;
  var T_FUNC_INVP = 19;
  var T_FUNC_FUNC = 20;
  var T_SECTAN = 21;
  var T_FUNCOVERFP = 22;
  var T_EXP_POW = 23;
  var T_SIN2COS2 = 24;
  var T_LN_OVER_X = 25;
  var T_ONE_OVER_ULN = 26;

  /* 错误码(AC_PRO_INT_* 同值) */
  var OK = 0;
  var EPARSE = -1;
  var ETABLE = -2;
  var EBUDGET = -3;
  var EOVER = -4;
  var EDEGREE = -5;

  /* 池(加载期一次分配;allocCount = 28) */
  var _nOp = new Int32Array(POOL_NODES);
  var _nA = new Int32Array(POOL_NODES);
  var _nB = new Int32Array(POOL_NODES);
  var _nIval = new Float64Array(POOL_NODES);       /* IR ival(常数积可达 1e12,Float64 精确) */
  var _wN = new Int32Array(2048);                  /* 驱动栈:节点 */
  var _wMeta = new Int32Array(2048);               /* 驱动栈:phase|parts<<2|inner<<4 */
  var _dmap = new Int32Array(POOL_NODES);          /* 输入节点 → 积分结果节点 */
  var _opd = new Int32Array(POOL_NODES + 2);       /* RPN→IR 操作数栈(−2 = e 底哨兵) */
  var _tplTok = new Int32Array(96);                /* 模板 token 流 */
  var _tplNum = new Float64Array(32);              /* 模板整常量池 */
  var _outBuf = new Int32Array(OUTBUF);            /* 渲染缓冲(charCode) */
  var _emitStack = new Int32Array(2048);           /* 渲染栈:idx|mp<<16 或 -(char+1) */
  var _digits = new Int32Array(24);                /* putn 位数字垫 */
  var _linA = new Float64Array(POOL_NODES);        /* 线性判定斜率 memo */
  var _linB = new Float64Array(POOL_NODES);        /* 线性判定截距 memo */
  var _linStamp = new Int32Array(POOL_NODES);      /* memo 时戳(正=ok 负=fail) */
  var _linStack = new Int32Array(2048);            /* 线性判定栈(node|phase<<9) */
  var _polyPool = new Int32Array(64 * (POLY_MAX + 1));  /* 多项式向量池(64×17) */
  var _polySlot = new Int32Array(POOL_NODES);      /* 节点 → 池槽 */
  var _polyFree = new Int32Array(64);              /* 槽空闲表(LIFO) */
  var _polyStack = new Int32Array(4096);           /* 多项式归一栈(node|phase<<9|slot<<10) */
  var _distC = new Int32Array(64);                 /* 分配工作表:常数节点 */
  var _distT = new Int32Array(64);                 /* 分配工作表:项 */
  var _distS = new Float64Array(64);               /* 分配工作表:符号 */
  var _mWraps = new Int32Array(64);                /* mul 常数外提 wrap 栈 */
  var _addT = new Int32Array(POOL_NODES);          /* add/sub 项表 */
  var _addS = new Float64Array(POOL_NODES);        /* add/sub 项符号(±1) */
  var _tKFP = new Int32Array(384);                 /* 积分表 kind/func/pow(128×3) */

  var _allocCount = 28;
  var _count = 0;
  var _xvar = -1;
  var _overflow = 0;
  var _partsUsed = 0;
  var _binding = 0;
  var _bdU = -1;
  var _bdA = 0;
  var _bdB = 0;
  var _bdN = 0;
  var _bdM = 0;
  var _linStampCur = 0;
  var _wTop = 0;             /* 驱动栈顶(tryPartsPush 与 drive 共用) */
  var _result = "";
  var _la = 0;
  var _lb = 0;
  var _outLen = 0;
  var _xpN = 0;            /* isXpow 出参 */
  var _sqArg = 0;          /* isSqrtOf 出参 */
  var _lfU = 0; var _lfA = 0; var _lfB = 0; var _lfF = 0;   /* isLinFunc 出参 */
  var _dqU = 0; var _dqA = 0; var _dqB = 0; var _dqC = 0; var _dqS = 0;  /* decompQuad 出参 */

  function allocCount() { return _allocCount; }
  function getString() { return _result; }

  /* 积分表(128 项,C s_pro_int_table 逐项搬运) */

  /* kind/func/pow 三元组,顺序与 C 完全一致(条目 1..128) */
  var _T_SPEC =
    "0,0,0;" +                                   /* 1 常数 */
    "1,0,1;1,0,2;1,0,3;1,0,4;1,0,5;1,0,6;1,0,7;1,0,8;1,0,-1;" +   /* 2..10 x^n */
    "2,0,0;3,0,0;4,0,0;5,0,0;6,0,0;7,0,0;8,0,0;9,0,0;10,0,0;" +   /* 11..19 */
    "11,1,0;11,2,0;11,3,0;11,11,0;11,9,0;11,10,0;11,8,0;11,13,0;11,14,0;11,15,0;" +
    "11,4,0;11,5,0;11,6,0;11,16,0;11,17,0;11,18,0;" +             /* 20..35 FUNC */
    "12,1,2;12,2,2;12,3,2;12,1,3;12,2,3;12,13,2;12,14,2;12,15,2;" +  /* 36..43 FUNC_POW */
    "13,1,1;13,2,1;13,11,1;13,9,1;13,13,1;13,14,1;13,6,1;13,8,1;" +  /* 44..51 XN_FUNC */
    "13,1,2;13,2,2;13,11,2;13,9,2;13,13,2;13,14,2;13,11,3;13,11,4;" + /* 52..59 */
    "13,1,3;13,2,3;13,9,3;13,13,3;13,14,3;" +                     /* 60..64 */
    "14,0,0;14,0,1;14,0,2;14,0,3;14,0,4;14,0,5;14,0,6;14,0,7;" +  /* 65..72 QUAD_L */
    "15,0,0;15,0,1;15,0,2;15,0,3;15,0,4;15,0,5;15,0,6;15,0,7;15,0,8;" + /* 73..81 QUAD_K */
    "16,0,0;16,0,1;16,0,2;" +                                     /* 82..84 XQUAD */
    "17,2,0;17,1,0;" +                                            /* 85..86 FUNC_INV */
    "18,2,1;18,13,14;" +                                          /* 87..88 FUNC_DIV */
    "19,2,2;19,1,2;19,3,2;19,14,2;19,13,2;19,15,2;" +             /* 89..94 FUNC_INVP */
    "20,1,2;20,13,14;" +                                          /* 95..96 FUNC_FUNC */
    "21,2,3;21,1,2;" +                                            /* 97..98 SECTAN */
    "22,1,2;22,2,1;22,13,14;22,14,13;" +                          /* 99..102 FUNCOVERFP */
    "23,11,2;" +                                                  /* 103 EXP_POW */
    "24,1,2;" +                                                   /* 104 SIN2COS2 */
    "25,9,0;26,9,0;" +                                            /* 105..106 ln 核 */
    "12,3,3;" +                                                   /* 107 tan³ */
    "12,15,3;12,1,4;12,2,4;12,1,5;12,2,5;12,13,3;12,14,3;12,13,4;12,14,4;12,9,2;" + /* 108..117 */
    "13,1,4;13,2,4;13,11,5;13,9,4;13,9,5;13,13,4;13,14,4;13,6,2;13,6,3;13,8,2;" +   /* 118..127 */
    "13,8,3";                                                     /* 128 x³·sqrt(x) */

  /* anti 模板串(\x01 分隔;与 _T_SPEC 一一对应,内容逐字 = C 表) */
  var _T_ANTI =
    "n*u" +
    "\x01u^2/2\x01u^3/3\x01u^4/4\x01u^5/5\x01u^6/6\x01u^7/7\x01u^8/8\x01u^9/9\x01u^(n+1)/(n+1)" +
    "\x01ln(u)/a\x01-1/u\x01atan(u)\x01asin(u)\x01asinh(u)\x01acosh(u)\x01atanh(u)" +
    "\x01(u*sqrt(1-u^2)+asin(u))/2\x01(u*sqrt(u^2+1)+asinh(u))/2" +
    "\x01-cos(u)/a\x01sin(u)/a\x01-ln(cos(u))/a\x01exp(u)/a\x01(u*ln(u)-u)/a" +
    "\x01(u*log(u)-u/ln(10))/a\x012*u*sqrt(u)/(3*a)\x01cosh(u)/a\x01sinh(u)/a\x01ln(cosh(u))/a" +
    "\x01(u*asin(u)+sqrt(1-u^2))/a\x01(u*acos(u)-sqrt(1-u^2))/a\x01(u*atan(u)-ln(1+u^2)/2)/a" +
    "\x01(u*asinh(u)-sqrt(1+u^2))/a\x01(u*acosh(u)-sqrt(u^2-1))/a\x01(u*atanh(u)+ln(1-u^2)/2)/a" +
    "\x01u/(2*a)-sin(2*u)/(4*a)\x01u/(2*a)+sin(2*u)/(4*a)\x01tan(u)/a-u/a" +
    "\x01(-cos(u)+cos(u)^3/3)/a\x01(sin(u)-sin(u)^3/3)/a" +
    "\x01sinh(2*u)/(4*a)-u/(2*a)\x01sinh(2*u)/(4*a)+u/(2*a)\x01u/a-tanh(u)/a" +
    "\x01-x*cos(x)+sin(x)\x01x*sin(x)+cos(x)\x01exp(x)*(x-1)\x01x^2*ln(x)/2-x^2/4" +
    "\x01x*cosh(x)-sinh(x)\x01x*sinh(x)-cosh(x)\x01((x^2+1)*atan(x)-x)/2\x012*x^2*sqrt(x)/5" +
    "\x01-x^2*cos(x)+2*x*sin(x)+2*cos(x)\x01x^2*sin(x)+2*x*cos(x)-2*sin(x)" +
    "\x01exp(x)*(x^2-2*x+2)\x01x^3*ln(x)/3-x^3/9" +
    "\x01x^2*cosh(x)-2*x*sinh(x)+2*cosh(x)\x01x^2*sinh(x)-2*x*cosh(x)+2*sinh(x)" +
    "\x01exp(x)*(x^3-3*x^2+6*x-6)\x01exp(x)*(x^4-4*x^3+12*x^2-24*x+24)" +
    "\x01-x^3*cos(x)+3*x^2*sin(x)+6*x*cos(x)-6*sin(x)" +
    "\x01x^3*sin(x)+3*x^2*cos(x)-6*x*sin(x)-6*cos(x)" +
    "\x01x^4*ln(x)/4-x^4/16" +
    "\x01x^3*cosh(x)-3*x^2*sinh(x)+6*x*cosh(x)-6*sinh(x)" +
    "\x01x^3*sinh(x)-3*x^2*cosh(x)+6*x*sinh(x)-6*cosh(x)" +
    "\x01atan(u)/a\x01atanh(u)/a\x01asin(u)/a\x01asinh(u)/a\x01acosh(u)/a" +
    "\x01(u*sqrt(1-u^2)+asin(u))/(2*a)\x01(u*sqrt(1+u^2)+asinh(u))/(2*a)\x01(u*sqrt(u^2-1)-acosh(u))/(2*a)" +
    "\x01atan(u/n)/(a*n)\x01atanh(u/n)/(a*n)\x01asin(u/n)/a\x01asinh(u/n)/a\x01acosh(u/n)/a" +
    "\x01(u*sqrt(m-u^2)+m*asin(u/n))/(2*a)\x01(u*sqrt(u^2+m)+m*asinh(u/n))/(2*a)" +
    "\x01(u*sqrt(u^2-m)-m*acosh(u/n))/(2*a)\x01ln((u-n)/(u+n))/(2*a*n)" +
    "\x01ln(u^2+m)/(2*a)\x01ln(u^2-m)/(2*a)\x01-ln(m-u^2)/(2*a)" +
    "\x01ln(1/cos(u)+sin(u)/cos(u))/a\x01-ln(1/sin(u)+cos(u)/sin(u))/a" +
    "\x01ln(sin(u))/a\x01ln(sinh(u))/a" +
    "\x01sin(u)/cos(u)/a\x01-cos(u)/sin(u)/a\x01-cos(u)/sin(u)/a-u/a" +
    "\x01sinh(u)/cosh(u)/a\x01-cosh(u)/sinh(u)/a\x01u/a-cosh(u)/sinh(u)/a" +
    "\x01sin(u)^2/(2*a)\x01sinh(u)^2/(2*a)" +
    "\x011/cos(u)/a\x01-1/sin(u)/a\x011/cos(u)/a\x01-1/sin(u)/a\x01-1/cosh(u)/a\x01-1/sinh(u)/a" +
    "\x01exp(n*u)/(n*a)\x01u/(8*a)-sin(4*u)/(32*a)\x01ln(u)^2/(2*a)\x01ln(ln(u))/a" +
    "\x01tan(u)^2/(2*a)+ln(cos(u))/a" +
    "\x01tanh(u)^2/(2*a)-ln(cosh(u))/a" +
    "\x013*u/(8*a)-sin(2*u)/(4*a)+sin(4*u)/(32*a)" +
    "\x013*u/(8*a)+sin(2*u)/(4*a)+sin(4*u)/(32*a)" +
    "\x01-cos(u)/a+2*cos(u)^3/(3*a)-cos(u)^5/(5*a)" +
    "\x01sin(u)/a-2*sin(u)^3/(3*a)+sin(u)^5/(5*a)" +
    "\x01cosh(u)^3/(3*a)-cosh(u)/a\x01sinh(u)/a+sinh(u)^3/(3*a)" +
    "\x01sinh(4*u)/(32*a)-sinh(2*u)/(4*a)+3*u/(8*a)" +
    "\x01sinh(4*u)/(32*a)+sinh(2*u)/(4*a)+3*u/(8*a)" +
    "\x01(u*ln(u)^2-2*u*ln(u)+2*u)/a" +
    "\x01-x^4*cos(x)+4*x^3*sin(x)+12*x^2*cos(x)-24*x*sin(x)-24*cos(x)" +
    "\x01x^4*sin(x)+4*x^3*cos(x)-12*x^2*sin(x)-24*x*cos(x)+24*sin(x)" +
    "\x01exp(x)*(x^5-5*x^4+20*x^3-60*x^2+120*x-120)" +
    "\x01x^5*ln(x)/5-x^5/25\x01x^6*ln(x)/6-x^6/36" +
    "\x01x^4*cosh(x)-4*x^3*sinh(x)+12*x^2*cosh(x)-24*x*sinh(x)+24*cosh(x)" +
    "\x01x^4*sinh(x)-4*x^3*cosh(x)+12*x^2*sinh(x)-24*x*cosh(x)+24*sinh(x)" +
    "\x01((2*x^3-x^2-1)*atan(x)+x)/6\x01((6*x^4-2*x^3+x^2+1)*atan(x)-x)/24" +
    "\x012*x^3*sqrt(x)/7\x012*x^4*sqrt(x)/9";

  var _T_OFFS = new Int32Array(129);   /* anti 偏移表(129 = 128 项 + 1 尾哨兵槽) */
  (function _initTable() {
    var i = 0;
    var k = 0;
    var v = 0;
    var n = _T_SPEC.length;
    var cnt = 0;
    /* kind/func/pow 解析 */
    i = 0;
    for (k = 0; k < 128; k++) {
      v = 0;
      while (i < n && _T_SPEC.charCodeAt(i) >= 48 && _T_SPEC.charCodeAt(i) <= 57) {
        v = v * 10 + (_T_SPEC.charCodeAt(i) - 48);
        i++;
      }
      _tKFP[k * 3] = _T_SPEC.charCodeAt(i) === 45 ? -v : v;   /* '-' 负号 */
      if (_T_SPEC.charCodeAt(i) === 45) { i++; }
      i++;                                                    /* ',' 或 ';' */
      v = 0;
      while (i < n && _T_SPEC.charCodeAt(i) >= 48 && _T_SPEC.charCodeAt(i) <= 57) {
        v = v * 10 + (_T_SPEC.charCodeAt(i) - 48);
        i++;
      }
      _tKFP[k * 3 + 1] = v;
      i++;
      v = 0;
      var negf = 0;
      if (_T_SPEC.charCodeAt(i) === 45) { negf = 1; i++; }
      while (i < n && _T_SPEC.charCodeAt(i) >= 48 && _T_SPEC.charCodeAt(i) <= 57) {
        v = v * 10 + (_T_SPEC.charCodeAt(i) - 48);
        i++;
      }
      _tKFP[k * 3 + 2] = negf === 1 ? -v : v;
      if (i < n && _T_SPEC.charCodeAt(i) === 59) { i++; }     /* ';' */
    }
    /* anti 偏移 */
    var m = _T_ANTI.length;
    cnt = 0;
    _T_OFFS[0] = 0;
    for (i = 0; i < m; i++) {
      if (_T_ANTI.charCodeAt(i) === 1) {
        cnt++;
        if (cnt > 128) { throw new Error("integrate table >128 entries"); }
        _T_OFFS[cnt] = i + 1;
      }
    }
    cnt++;
    if (cnt !== 128) { throw new Error("integrate table " + cnt + " != 128 entries"); }
  })();

  /* IR 基元 */

  function isConst(i) {
    return i >= 0 && i < _count && _nOp[i] === OP_CONST;
  }
  function put(op, a, b, ival) {
    if (_overflow !== 0) { return -1; }
    if (_count >= POOL_NODES) { _overflow = 1; return -1; }
    _nOp[_count] = op;
    _nA[_count] = a;
    _nB[_count] = b;
    _nIval[_count] = ival;
    _count++;
    return _count - 1;
  }
  function constN(v) { return put(OP_CONST, -1, -1, v); }
  function varX() {
    if (_xvar >= 0) { return _xvar; }
    _xvar = put(OP_VAR, -1, -1, V_X);
    return _xvar;
  }
  function ineg(a) {
    if (a < 0) { return -1; }
    if (isConst(a)) { return constN(-_nIval[a]); }
    if (_nOp[a] === OP_NEG) { return _nA[a]; }
    return put(OP_NEG, a, -1, 0);
  }
  function ipow(base, e) {
    var v = 1;
    var k = 0;
    var ok = 1;
    if (base < 0) { return -1; }
    if (e < 0 || e > MAX_POW_BUILD) { return -1; }
    if (e === 0) { return constN(1); }
    if (e === 1) { return base; }
    if (isConst(base)) {
      for (k = 0; k < e; k++) {
        v = v * _nIval[base];
        if (v > MAX_CONST || v < -MAX_CONST) { ok = 0; break; }
      }
      if (ok === 1) { return constN(v); }
    }
    return put(OP_POW, base, -1, e);
  }
  function ifunc(fid, arg) {
    if (arg < 0) { return -1; }
    return put(OP_FUNC, arg, -1, fid);
  }
  function xpow(e) { return ipow(varX(), e); }

  /* add/sub:项表摊平(消 C 尾递归)
   * a ⊕s b 的右操作数子树(ADD/SUB/NEG)展平成带符号项表,再左结合
   * 折叠 —— 与 C pro_int_add/sub 的旋转恒等式逐条等价:
   *   a+(x±y)=(a±x)±y、a−(x±y)=(a∓x)±y、a±(−x)=a∓x。 */

  function iaddsub(a, b, s0) {
    var sp = 0;
    var idx = 0;
    var cap = _addT.length;
    var e = 0;
    var s = 0;
    var acc = 0;
    var i = 0;
    var sgn = s0;

    if (a < 0 || b < 0) { return -1; }
    /* 项表 [0..idx):节点;收集栈 [top..cap):双端复用 _addT/_addS。
     * 种子 b 压入栈侧(idx 从 0 起,专归项表) */
    var top = cap;
    top--;
    _addT[top] = b;
    _addS[top] = sgn;
    while (top < cap) {
      e = _addT[top];                                    /* 弹出:先读后进 */
      s = _addS[top];
      top++;
      if (_nOp[e] === OP_ADD) {
        if (idx + 2 > top) { return put(sgn > 0 ? OP_ADD : OP_SUB, a, b, 0); }
        top--;
        _addT[top] = _nB[e];
        _addS[top] = s;
        top--;
        _addT[top] = _nA[e];
        _addS[top] = s;
      } else if (_nOp[e] === OP_SUB) {
        if (idx + 2 > top) { return put(sgn > 0 ? OP_ADD : OP_SUB, a, b, 0); }
        top--;
        _addT[top] = _nB[e];
        _addS[top] = -s;
        top--;
        _addT[top] = _nA[e];
        _addS[top] = s;
      } else if (_nOp[e] === OP_NEG) {
        if (idx + 1 > top) { return put(sgn > 0 ? OP_ADD : OP_SUB, a, b, 0); }
        top--;
        _addT[top] = _nA[e];
        _addS[top] = -s;
      } else {
        if (idx >= top) { return put(sgn > 0 ? OP_ADD : OP_SUB, a, b, 0); }
        _addT[idx] = e;
        _addS[idx] = s;
        idx++;
      }
    }
    /* 左结合折叠;首项与 a 合并 */
    acc = a;
    for (i = 0; i < idx; i++) {
      e = _addT[i];
      s = _addS[i];
      if (isConst(acc) && isConst(e)) {
        acc = constN(_nIval[acc] + s * _nIval[e]);
        if (acc < 0) { return -1; }
        continue;
      }
      if (isConst(e) && _nIval[e] === 0) { continue; }       /* ±0 */
      if (isConst(acc) && _nIval[acc] === 0 && s > 0) { acc = e; continue; }
      acc = put(s > 0 ? OP_ADD : OP_SUB, acc, e, 0);
      if (acc < 0) { return -1; }
    }
    return acc;
  }
  function iadd(a, b) { return iaddsub(a, b, 1); }
  function isub(a, b) { return iaddsub(a, b, -1); }

  /* mul(C pro_int_mul 全规则;wrap 栈 + 分配工作表) */

  /* 常数 cnode × 项 t(t 非 ADD/SUB/NEG —— 分配工作表已消化) */
  function imulConstLeaf(cnode, t) {
    var c = _nIval[cnode];
    var p = 0;
    for (;;) {
      if (_nOp[t] === OP_CONST) { return constN(c * _nIval[t]); }
      if (_nOp[t] === OP_MUL && isConst(_nA[t])) {
        p = c * _nIval[_nA[t]];
        if (p > MAX_CONST || p < -MAX_CONST) { break; }
        c = p;
        t = _nB[t];
        continue;
      }
      break;
    }
    if (c === 1) { return t; }
    return put(OP_MUL, constN(c), t, 0);
  }

  function imul(a0, b0) {
    var a = a0;
    var b = b0;
    var neg = 0;
    var wrapsLen = 0;
    var dLen = 0;
    var acc = -1;
    var cnode = 0;
    var t = 0;
    var m = 0;
    var dc = 0;
    var dt = 0;
    var ds = 0;
    var dist = 0;

    for (;;) {
      /* 归一化(C 规则按原次序;退出时 t 恒定形) */
      dist = 0;
      for (;;) {
        t = -1;
        if (a < 0 || b < 0) { return -1; }
        if (_nOp[a] === OP_NEG) { neg = 1 - neg; a = _nA[a]; continue; }
        if (_nOp[b] === OP_NEG) { neg = 1 - neg; b = _nA[b]; continue; }
        if (isConst(a) && isConst(b)) {
          t = constN(_nIval[a] * _nIval[b]);
          if (t < 0) { return -1; }
          break;
        }
        if (isConst(a) && _nIval[a] === 0) { t = constN(0); if (t < 0) { return -1; } break; }
        if (isConst(b) && _nIval[b] === 0) { t = constN(0); if (t < 0) { return -1; } break; }
        if (isConst(a) && _nIval[a] === 1) { t = b; break; }
        if (isConst(b) && _nIval[b] === 1) { t = a; break; }
        if (isConst(a) && _nOp[b] === OP_MUL && isConst(_nA[b])) {
          var p = _nIval[a] * _nIval[_nA[b]];
          if (p <= MAX_CONST && p >= -MAX_CONST) {
            a = constN(p);
            if (a < 0) { return -1; }
            b = _nB[b];
            continue;
          }
          break;                                           /* C:超界落 plain MUL */
        }
        if (isConst(b) && _nOp[a] === OP_MUL && isConst(_nA[a])) {
          var p2 = _nIval[b] * _nIval[_nA[a]];
          if (p2 <= MAX_CONST && p2 >= -MAX_CONST) {
            b = constN(p2);
            if (b < 0) { return -1; }
            a = _nB[a];
            continue;
          }
          break;
        }
        /* 常数因子外提(C R5..R8;wrap 栈回灌代替递归) */
        if (_nOp[a] === OP_MUL && isConst(_nA[a])) {
          if (wrapsLen < _mWraps.length) {
            _mWraps[wrapsLen] = _nA[a];
            wrapsLen++;
            a = _nB[a];
            continue;
          }
          break;
        }
        if (_nOp[a] === OP_MUL && isConst(_nB[a])) {
          if (wrapsLen < _mWraps.length) {
            _mWraps[wrapsLen] = _nB[a];
            wrapsLen++;
            a = _nA[a];
            continue;
          }
          break;
        }
        if (_nOp[b] === OP_MUL && isConst(_nA[b])) {
          if (wrapsLen < _mWraps.length) {
            _mWraps[wrapsLen] = _nA[b];
            wrapsLen++;
            b = _nB[b];
            continue;
          }
          break;
        }
        if (_nOp[b] === OP_MUL && isConst(_nB[b])) {
          if (wrapsLen < _mWraps.length) {
            _mWraps[wrapsLen] = _nB[b];
            wrapsLen++;
            b = _nA[b];
            continue;
          }
          break;
        }
        /* 常数×和差分配(C R9/R10;工作表展开) */
        if (isConst(a) && (_nOp[b] === OP_ADD || _nOp[b] === OP_SUB)) {
          cnode = a; t = b; dist = 1; break;
        }
        if (isConst(b) && (_nOp[a] === OP_ADD || _nOp[a] === OP_SUB)) {
          cnode = b; t = a; dist = 1; break;
        }
        break;                                             /* plain MUL */
      }

      if (t < 0) {
        t = put(OP_MUL, a, b, 0);                          /* plain MUL 定形 */
        if (t < 0) { return -1; }
      }

      /* 分配展开(dist=1 时 t = 和差节点) */
      if (dist === 1) {
        _distC[0] = cnode;
        _distT[0] = t;
        _distS[0] = 1;
        dLen = 1;
        acc = -1;
        while (dLen > 0) {
          dLen--;
          dc = _distC[dLen];
          dt = _distT[dLen];
          ds = _distS[dLen];
          if (_nOp[dt] === OP_ADD || _nOp[dt] === OP_SUB) {
            /* 先压右后压左 → LIFO 弹出序 = 左→右(C 的项序);
             * 工作表满 = 预算耗尽(C 同输入在池上溢出 → EBUDGET 同码) */
            if (dLen + 2 > _distC.length) { _overflow = 1; return -1; }
            _distC[dLen] = dc; _distT[dLen] = _nB[dt];
            _distS[dLen] = (_nOp[dt] === OP_SUB ? -ds : ds); dLen++;
            _distC[dLen] = dc; _distT[dLen] = _nA[dt]; _distS[dLen] = ds; dLen++;
            continue;
          }
          if (_nOp[dt] === OP_NEG) {
            if (dLen + 1 > _distC.length) { return -1; }
            _distC[dLen] = dc; _distT[dLen] = _nA[dt]; _distS[dLen] = -ds; dLen++;
            continue;
          }
          m = imulConstLeaf(dc, dt);
          if (m < 0) { return -1; }
          if (ds < 0) { m = ineg(m); if (m < 0) { return -1; } }
          acc = acc < 0 ? m : iaddsub(acc, m, 1);
          if (acc < 0) { return -1; }
        }
        t = acc;
      }

      /* wrap 回灌(外提的常数因子乘回已定形的 t) */
      if (wrapsLen > 0) {
        wrapsLen--;
        a = _mWraps[wrapsLen];
        b = t;
        continue;                                          /* 重进归一(t 非和差,不再分配) */
      }
      return neg !== 0 ? (t < 0 ? -1 : ineg(t)) : t;
    }
  }

  function idiv(a, b) {
    var d = 0;
    var d1 = 0;
    for (;;) {
      if (a < 0 || b < 0) { return -1; }
      if (isConst(b)) {
        d = _nIval[b];
        if (d === 0) { return -1; }
        if (d === 1) { return a; }
        if (isConst(a) && (_nIval[a] % d) === 0) { return constN(_nIval[a] / d); }
        if (_nOp[a] === OP_DIV && isConst(_nB[a])) {
          d1 = _nIval[_nB[a]];
          if (d1 !== 0 && d1 * d <= MAX_CONST) {
            b = constN(d1 * d);
            if (b < 0) { return -1; }
            a = _nA[a];
            continue;
          }
        }
      }
      break;
    }
    return put(OP_DIV, a, b, 0);
  }

  /* 模板词法器(C pro_int_next 子集) */

  /* 函数名表(最长前缀优先;16 个,无 abs/atan2 —— 与 C s_pro_int_fns 一致) */
  function tplMatchFunc(s, i, end) {
    if (i + 5 <= end && s.charCodeAt(i) === 97) {          /* a… */
      if (s.charCodeAt(i + 1) === 115 && s.charCodeAt(i + 2) === 105 && s.charCodeAt(i + 3) === 110 && s.charCodeAt(i + 4) === 104) { return 16 | (5 << 10); }  /* asinh */
      if (s.charCodeAt(i + 1) === 99 && s.charCodeAt(i + 2) === 111 && s.charCodeAt(i + 3) === 115 && s.charCodeAt(i + 4) === 104) { return 17 | (5 << 10); }  /* acosh */
      if (s.charCodeAt(i + 1) === 116 && s.charCodeAt(i + 2) === 97 && s.charCodeAt(i + 3) === 110 && s.charCodeAt(i + 4) === 104) { return 18 | (5 << 10); }  /* atanh */
    }
    if (i + 4 <= end && s.charCodeAt(i) === 97) {
      if (s.charCodeAt(i + 1) === 115 && s.charCodeAt(i + 2) === 105 && s.charCodeAt(i + 3) === 110) { return 4 | (4 << 10); }   /* asin */
      if (s.charCodeAt(i + 1) === 99 && s.charCodeAt(i + 2) === 111 && s.charCodeAt(i + 3) === 115) { return 5 | (4 << 10); }    /* acos */
      if (s.charCodeAt(i + 1) === 116 && s.charCodeAt(i + 2) === 97 && s.charCodeAt(i + 3) === 110) { return 6 | (4 << 10); }    /* atan */
    }
    if (i + 4 <= end && s.charCodeAt(i) === 115 && s.charCodeAt(i + 1) === 105 && s.charCodeAt(i + 2) === 110 && s.charCodeAt(i + 3) === 104) { return 13 | (4 << 10); }  /* sinh */
    if (i + 4 <= end && s.charCodeAt(i) === 99 && s.charCodeAt(i + 1) === 111 && s.charCodeAt(i + 2) === 115 && s.charCodeAt(i + 3) === 104) { return 14 | (4 << 10); }   /* cosh */
    if (i + 4 <= end && s.charCodeAt(i) === 116 && s.charCodeAt(i + 1) === 97 && s.charCodeAt(i + 2) === 110 && s.charCodeAt(i + 3) === 104) { return 15 | (4 << 10); }   /* tanh */
    if (i + 3 <= end && s.charCodeAt(i) === 115 && s.charCodeAt(i + 1) === 105 && s.charCodeAt(i + 2) === 110) { return 1 | (3 << 10); }   /* sin */
    if (i + 3 <= end && s.charCodeAt(i) === 99 && s.charCodeAt(i + 1) === 111 && s.charCodeAt(i + 2) === 115) { return 2 | (3 << 10); }    /* cos */
    if (i + 3 <= end && s.charCodeAt(i) === 116 && s.charCodeAt(i + 1) === 97 && s.charCodeAt(i + 2) === 110) { return 3 | (3 << 10); }    /* tan */
    if (i + 3 <= end && s.charCodeAt(i) === 101 && s.charCodeAt(i + 1) === 120 && s.charCodeAt(i + 2) === 112) { return 11 | (3 << 10); }   /* exp */
    if (i + 3 <= end && s.charCodeAt(i) === 108 && s.charCodeAt(i + 1) === 111 && s.charCodeAt(i + 2) === 103) { return 10 | (3 << 10); }   /* log */
    if (i + 2 <= end && s.charCodeAt(i) === 108 && s.charCodeAt(i + 1) === 110) { return 9 | (2 << 10); }                                  /* ln */
    if (i + 4 <= end && s.charCodeAt(i) === 115 && s.charCodeAt(i + 1) === 113 && s.charCodeAt(i + 2) === 114 && s.charCodeAt(i + 3) === 116) { return 8 | (4 << 10); }  /* sqrt */
    return 0;
  }

  /* 模板串 → token 流(kernel 编码);隐式乘法注入同 C 词法器规则。
   * 返回 token 数;失败 −1(EPARSE)/ −5(EDEGREE)。 */
  function lexTemplate(s, from, to) {
    var i = from;
    var cnt = 0;
    var numN = 0;
    var prev = -1;
    var cc = 0;
    var v = 0;
    var mf = 0;
    var vid = 0;

    while (i < to) {
      cc = s.charCodeAt(i);
      if (cc === 32 || cc === 9 || cc === 13 || cc === 10) { i++; continue; }
      /* 隐式乘法:cur ∈ {VAR,FUNC,LP} 且 prev ∈ {NUM,VAR,RP} */
      if (cnt > 0 && prev >= 0) {
        var curIsMulTarget = (cc === 40) ? 1 : 0;        /* '(' */
        if (cc >= 97 && cc <= 122) { curIsMulTarget = 1; }
        if (curIsMulTarget === 1 && (prev === 1 || prev === 2 || prev === 6)) {
          if (cnt >= _tplTok.length) { return -1; }
          _tplTok[cnt] = (4 << 8) | 2;
          cnt++;
        }
      }
      if (cc >= 48 && cc <= 57) {                        /* 整常量 */
        v = 0;
        while (i < to) {
          cc = s.charCodeAt(i);
          if (cc < 48 || cc > 57) { break; }
          v = v * 10 + (cc - 48);
          if (v > MAX_CONST) { return -5; }
          i++;
        }
        if (i < to && s.charCodeAt(i) === 46) { return -1; }
        if (numN >= _tplNum.length) { return -1; }
        _tplNum[numN] = v;
        if (cnt >= _tplTok.length) { return -1; }
        _tplTok[cnt] = (1 << 8) | numN;
        cnt++;
        numN++;
        prev = 1;
        continue;
      }
      if (cc >= 97 && cc <= 122) {                       /* 函数名先于变量 */
        mf = tplMatchFunc(s, i, to);
        if (mf > 0) {
          var fl = mf >> 10;
          var fid = mf & 1023;
          i += fl;
          if (cnt >= _tplTok.length) { return -1; }
          _tplTok[cnt] = (7 << 8) | fid;
          cnt++;
          prev = 7;
          continue;
        }
        vid = -1;
        if (cc === 120) { vid = V_X; }
        else if (cc === 117) { vid = V_U; }
        else if (cc === 97) { vid = V_A; }
        else if (cc === 98) { vid = V_B; }
        else if (cc === 110) { vid = V_N; }
        else if (cc === 109) { vid = V_M; }
        else if (cc === 101) {                            /* 'e' → T_CONST(C_E) */
          if (cnt >= _tplTok.length) { return -1; }
          _tplTok[cnt] = (3 << 8) | 0;
          cnt++;
          i++;
          prev = 3;
          continue;
        }
        if (vid >= 0) {
          if (cnt >= _tplTok.length) { return -1; }
          _tplTok[cnt] = (2 << 8) | vid;
          cnt++;
          i++;
          prev = 2;
          continue;
        }
        return -1;
      }
      i++;
      if (cc === 43) { if (cnt >= _tplTok.length) { return -1; } _tplTok[cnt] = (4 << 8) | 0; cnt++; prev = 4; continue; }
      if (cc === 45) {
        if (cnt >= _tplTok.length) { return -1; }
        if (prev === -1 || prev === 4 || prev === 5 || prev === 10) { _tplTok[cnt] = 10 << 8; cnt++; prev = 10; }
        else { _tplTok[cnt] = (4 << 8) | 1; cnt++; prev = 4; }
        continue;
      }
      if (cc === 42) { if (cnt >= _tplTok.length) { return -1; } _tplTok[cnt] = (4 << 8) | 2; cnt++; prev = 4; continue; }
      if (cc === 47) { if (cnt >= _tplTok.length) { return -1; } _tplTok[cnt] = (4 << 8) | 3; cnt++; prev = 4; continue; }
      if (cc === 94) { if (cnt >= _tplTok.length) { return -1; } _tplTok[cnt] = (4 << 8) | 4; cnt++; prev = 4; continue; }
      if (cc === 40) { if (cnt >= _tplTok.length) { return -1; } _tplTok[cnt] = 5 << 8; cnt++; prev = 5; continue; }
      if (cc === 41) { if (cnt >= _tplTok.length) { return -1; } _tplTok[cnt] = 6 << 8; cnt++; prev = 6; continue; }
      return -1;
    }
    return cnt;
  }

  /* RPN → IR(主输入与模板共用;bind 代换) */

  /* 返回根;失败 −1 且置 _errCode。E 底哨兵 −2:e^x → exp(x)。 */
  var _errCode = OK;

  function translateRPN(rpn, len, numSrc) {
    var sp = 0;
    var i = 0;
    var w = 0;
    var t = 0;
    var p = 0;
    var a = 0;
    var b = 0;
    var nd = 0;
    var ev = 0;

    for (i = 0; i < len; i++) {
      w = rpn[i];
      t = w >> 8;
      p = w & 255;
      if (t === 1) {                                     /* T_NUM */
        if (p >= numSrc.length) { _errCode = EPARSE; return -1; }
        ev = numSrc[p];
        if (ev !== Math.floor(ev)) { _errCode = EPARSE; return -1; }
        if (ev > MAX_CONST || ev < -MAX_CONST) { _errCode = EDEGREE; return -1; }
        nd = constN(ev);
        if (nd < 0) { _errCode = _overflow !== 0 ? EBUDGET : EPARSE; return -1; }
        _opd[sp] = nd; sp++;
        continue;
      }
      if (t === 2) {                                     /* T_VAR */
        if (_binding === 0) {
          nd = varX();
        } else {
          if (p === V_U) { nd = _bdU; }
          else if (p === V_A) { nd = constN(_bdA); }
          else if (p === V_B) { nd = constN(_bdB); }
          else if (p === V_M) { nd = constN(_bdM); }
          else { nd = constN(_bdN); }                    /* V_N 与 V_X 之外全部落 n */
          if (p === V_X) { nd = varX(); }
        }
        if (nd < 0) { _errCode = _overflow !== 0 ? EBUDGET : EPARSE; return -1; }
        _opd[sp] = nd; sp++;
        continue;
      }
      if (t === 3) {                                     /* T_CONST:仅 e 底(C_E) */
        if (p !== 0) { _errCode = EPARSE; return -1; }   /* π → EPARSE(C 无) */
        _opd[sp] = -2; sp++;
        continue;
      }
      if (t === 10) {                                    /* T_UMINUS */
        if (sp < 1) { _errCode = EPARSE; return -1; }
        sp--;
        a = _opd[sp];
        nd = ineg(a);
        if (nd < 0) { _errCode = _overflow !== 0 ? EBUDGET : EPARSE; return -1; }
        _opd[sp] = nd; sp++;
        continue;
      }
      if (t === 4) {                                     /* T_OP */
        if (sp < 2) { _errCode = EPARSE; return -1; }
        sp--;
        b = _opd[sp];
        sp--;
        a = _opd[sp];
        if (p === 4) {                                   /* OP_POW:e^x 特判 */
          if (a === -2) {
            nd = ifunc(11, b);                           /* F_EXP */
          } else if (b === -2) {
            _errCode = EPARSE; return -1;
          } else {
            if (!isConst(b)) { _errCode = EPARSE; return -1; }
            ev = _nIval[b];
            if (ev < 0 || ev !== Math.floor(ev)) { _errCode = EPARSE; return -1; }
            if (_binding === 0 && ev > MAX_POW_PARSE) { _errCode = EDEGREE; return -1; }
            nd = ipow(a, ev);
          }
          if (nd < 0) { _errCode = _overflow !== 0 ? EBUDGET : EPARSE; return -1; }
          _opd[sp] = nd; sp++;
          continue;
        }
        if (a === -2 || b === -2) { _errCode = EPARSE; return -1; }
        if (p === 0) { nd = iadd(a, b); }
        else if (p === 1) { nd = isub(a, b); }
        else if (p === 2) { nd = imul(a, b); }
        else { nd = idiv(a, b); }
        if (nd < 0) { _errCode = _overflow !== 0 ? EBUDGET : EPARSE; return -1; }
        _opd[sp] = nd; sp++;
        continue;
      }
      if (t === 7) {                                     /* T_FUNC(仅模板) */
        if (p === 7 || p === 12) { _errCode = EPARSE; return -1; }   /* atan2/abs */
        if (sp < 1) { _errCode = EPARSE; return -1; }
        sp--;
        a = _opd[sp];
        if (a === -2) { _errCode = EPARSE; return -1; }
        nd = ifunc(p, a);
        if (nd < 0) { _errCode = _overflow !== 0 ? EBUDGET : EPARSE; return -1; }
        _opd[sp] = nd; sp++;
        continue;
      }
      _errCode = EPARSE;
      return -1;
    }
    if (sp !== 1 || _opd[0] === -2) { _errCode = EPARSE; return -1; }
    return _opd[0];
  }

  /* 匹配辅助(C 同款) */

  function isOne(i) { return isConst(i) && _nIval[i] === 1; }

  function isXpow(i) {
    _xpN = 0;
    if (i < 0 || i >= _count) { return 0; }
    if (_nOp[i] === OP_VAR && _nIval[i] === V_X) { _xpN = 1; return 1; }
    if (_nOp[i] === OP_POW) {
      var base = _nA[i];
      if (base >= 0 && _nOp[base] === OP_VAR && _nIval[base] === V_X) {
        _xpN = _nIval[i];
        return 1;
      }
    }
    return 0;
  }

  function isX2(i) { return isXpow(i) === 1 && _xpN === 2; }

  function isSqrtOf(i) {
    _sqArg = -1;
    if (i < 0 || i >= _count) { return 0; }
    if (_nOp[i] !== OP_FUNC) { return 0; }
    if (_nIval[i] !== 8) { return 0; }                   /* F_SQRT */
    _sqArg = _nA[i];
    return 1;
  }

  /* 线性判定:u = a·x + b(后序 memo,时戳免清池;整除检查与 C 同款) */
  function ilinear(r) {
    var sp = 0;
    var e = 0;
    var nd = 0;
    var phase = 0;
    var a1 = 0;
    var b1 = 0;
    var a2 = 0;
    var b2 = 0;
    var d = 0;

    _linStampCur++;
    _linStack[sp] = r | (1 << 9);
    sp++;
    _linStack[sp] = r;
    sp++;
    while (sp > 0) {
      sp--;
      e = _linStack[sp];
      nd = e & 511;
      phase = e >> 9;
      if (phase === 0) {
        var op = _nOp[nd];
        if (op === OP_CONST) {                          /* 叶:就地落 memo */
          _linA[nd] = 0; _linB[nd] = _nIval[nd]; _linStamp[nd] = _linStampCur;
          continue;
        }
        if (op === OP_VAR) {
          if (_nIval[nd] !== V_X) { _linStamp[nd] = -_linStampCur; }
          else { _linA[nd] = 1; _linB[nd] = 0; _linStamp[nd] = _linStampCur; }
          continue;
        }
        if (op !== OP_NEG && op !== OP_ADD && op !== OP_SUB &&
            op !== OP_MUL && op !== OP_DIV) {
          _linStamp[nd] = -_linStampCur;
          continue;
        }
        if (sp + 3 >= _linStack.length) { return 0; }
        _linStack[sp] = nd | (1 << 9); sp++;
        _linStack[sp] = _nB[nd]; sp++;
        _linStack[sp] = _nA[nd]; sp++;
        continue;
      }
      /* phase 1:子 memo 就绪则算本节点 */
      var op2 = _nOp[nd];
      if (op2 === OP_CONST) {
        _linA[nd] = 0; _linB[nd] = _nIval[nd]; _linStamp[nd] = _linStampCur;
      } else if (op2 === OP_VAR) {
        if (_nIval[nd] !== V_X) { _linStamp[nd] = -_linStampCur; }
        else { _linA[nd] = 1; _linB[nd] = 0; _linStamp[nd] = _linStampCur; }
      } else {
        var la = _nA[nd];
        var lb = _nB[nd];
        if (_linStamp[la] !== _linStampCur || _linStamp[lb] !== _linStampCur) {
          _linStamp[nd] = -_linStampCur;
          continue;
        }
        a1 = _linA[la]; b1 = _linB[la];
        a2 = _linA[lb]; b2 = _linB[lb];
        if (op2 === OP_NEG) {
          _linA[nd] = -a1; _linB[nd] = -b1; _linStamp[nd] = _linStampCur;
        } else if (op2 === OP_ADD) {
          _linA[nd] = a1 + a2; _linB[nd] = b1 + b2; _linStamp[nd] = _linStampCur;
        } else if (op2 === OP_SUB) {
          _linA[nd] = a1 - a2; _linB[nd] = b1 - b2; _linStamp[nd] = _linStampCur;
        } else if (op2 === OP_MUL) {
          if (isConst(la)) {
            _linA[nd] = _nIval[la] * a2; _linB[nd] = _nIval[la] * b2;
            _linStamp[nd] = _linStampCur;
          } else if (isConst(lb)) {
            _linA[nd] = _nIval[lb] * a1; _linB[nd] = _nIval[lb] * b1;
            _linStamp[nd] = _linStampCur;
          } else { _linStamp[nd] = -_linStampCur; }
        } else if (op2 === OP_DIV) {
          if (!isConst(lb)) { _linStamp[nd] = -_linStampCur; }
          else {
            d = _nIval[lb];
            if (d === 0 || (a1 % d) !== 0 || (b1 % d) !== 0) { _linStamp[nd] = -_linStampCur; }
            else { _linA[nd] = a1 / d; _linB[nd] = b1 / d; _linStamp[nd] = _linStampCur; }
          }
        } else { _linStamp[nd] = -_linStampCur; }
      }
    }
    if (_linStamp[r] !== _linStampCur || _linStamp[r] < 0) { return 0; }
    _la = _linA[r];
    _lb = _linB[r];
    return 1;
  }

  function isLinFunc(i) {
    _lfU = -1; _lfA = 0; _lfB = 0; _lfF = 0;
    if (i < 0 || i >= _count) { return 0; }
    if (_nOp[i] !== OP_FUNC) { return 0; }
    if (ilinear(_nA[i]) !== 1) { return 0; }
    if (_la === 0) { return 0; }
    _lfU = _nA[i];
    _lfA = _la;
    _lfB = _lb;
    _lfF = _nIval[i];
    return 1;
  }

  function sqrtPerfect(m) {
    var k = 2;
    if (m < 4) { return 0; }
    while (k * k <= m) {
      if (k * k === m) { return k; }
      k++;
    }
    return 0;
  }

  /* u² 判定:POW(lin, 2) 且斜率非零 */
  function isUSq(i) {
    _dqU = -1; _dqA = 0; _dqB = 0;
    if (i < 0 || i >= _count) { return 0; }
    if (_nOp[i] !== OP_POW || _nIval[i] !== 2) { return 0; }
    if (ilinear(_nA[i]) !== 1) { return 0; }
    if (_la === 0) { return 0; }
    _dqU = _nA[i];
    _dqA = _la;
    _dqB = _lb;
    return 1;
  }

  /* 二次二项 Q = ±u² + c 分解(C pro_int_decomp_quad) */
  function decompQuad(q) {
    var c = 0;
    _dqU = -1; _dqA = 0; _dqB = 0; _dqC = 0; _dqS = 0;
    if (q < 0 || q >= _count) { return 0; }
    if (_nOp[q] !== OP_ADD && _nOp[q] !== OP_SUB) { return 0; }
    if (isUSq(_nA[q]) === 1) {
      if (!isConst(_nB[q])) { return 0; }
      c = _nIval[_nB[q]];
      if (c === 0) { return 0; }
      _dqS = 1;
      _dqC = _nOp[q] === OP_ADD ? c : -c;
      return 1;
    }
    if (isUSq(_nB[q]) !== 1) { return 0; }
    if (!isConst(_nA[q])) { return 0; }
    c = _nIval[_nA[q]];
    if (c === 0) { return 0; }
    _dqS = _nOp[q] === OP_ADD ? 1 : -1;
    _dqC = c;
    return 1;
  }

  /* 表匹配(C pro_int_match_entry 逐 case 移植)
   * 命中 1(bind 出参已填),未命中 0,池满 −1。 */

  function matchEntry(k, r) {
    var kind = _tKFP[k * 3];
    var efunc = _tKFP[k * 3 + 1];
    var epow = _tKFP[k * 3 + 2];
    var n = 0;
    var a = 0;
    var b = 0;
    var u = -1;
    var fid = 0;
    var den = 0;
    var arg = 0;
    var k1 = 0;
    var shape = 0;
    var form = 0;
    var q = -1;
    var u2 = -1;
    var u2s = 0;
    var c = 0;
    var ok = 0;
    var kk = 0;
    var m = 0;
    var a2 = 0;
    var b2 = 0;
    var fnode = 0;
    var xnode = 0;
    var u1 = 0;
    var f1 = 0;
    var f2 = 0;
    var base = 0;
    var p1 = 0;
    var p2 = 0;
    var s = 0;
    var t2 = 0;
    var mulnode = 0;

    _bdU = -1; _bdA = 1; _bdB = 0; _bdN = 0; _bdM = 0;
    if (r < 0 || r >= _count) { return 0; }

    if (kind === T_CONST) {
      if (_nOp[r] !== OP_CONST) { return 0; }
      u = varX();
      if (u < 0) { return -1; }
      _bdU = u;
      _bdN = _nIval[r];
      return 1;
    }
    if (kind === T_XN) {
      if (isXpow(r) !== 1) { return 0; }
      n = _xpN;
      if (epow >= 0 && n !== epow) { return 0; }
      u = varX();
      if (u < 0) { return -1; }
      _bdU = u;
      _bdN = n;
      return 1;
    }
    if (kind === T_INV) {
      if (_nOp[r] !== OP_DIV) { return 0; }
      if (!isOne(_nA[r])) { return 0; }
      if (ilinear(_nB[r]) !== 1) { return 0; }
      if (_la === 0) { return 0; }
      _bdU = _nB[r];
      _bdA = _la;
      _bdB = _lb;
      return 1;
    }
    if (kind === T_INV2) {
      if (_nOp[r] !== OP_DIV) { return 0; }
      if (!isOne(_nA[r])) { return 0; }
      den = _nB[r];
      if (_nOp[den] !== OP_POW) { return 0; }
      if (_nIval[den] !== 2) { return 0; }
      if (ilinear(_nA[den]) !== 1) { return 0; }
      if (_la !== 1 || _lb !== 0) { return 0; }
      _bdU = _nA[den];
      return 1;
    }
    if (kind === T_ATAN_KER || kind === T_ATANH_KER) {
      if (_nOp[r] !== OP_DIV) { return 0; }
      if (!isOne(_nA[r])) { return 0; }
      den = _nB[r];
      if (_nOp[den] !== OP_ADD && _nOp[den] !== OP_SUB) { return 0; }
      k1 = _nOp[den] === OP_ADD ? 1 : 0;
      if (kind === T_ATAN_KER && k1 === 0) { return 0; }
      if (kind === T_ATANH_KER && k1 === 1) { return 0; }
      if (!isOne(_nA[den])) { return 0; }
      if (isX2(_nB[den]) !== 1) { return 0; }
      u = varX();
      if (u < 0) { return -1; }
      _bdU = u;
      return 1;
    }
    if (kind === T_ASIN_KER || kind === T_ASINH_KER || kind === T_ACOSH_KER) {
      if (_nOp[r] !== OP_DIV) { return 0; }
      if (!isOne(_nA[r])) { return 0; }
      if (isSqrtOf(_nB[r]) !== 1) { return 0; }
      arg = _sqArg;
      if (_nOp[arg] !== OP_ADD && _nOp[arg] !== OP_SUB) { return 0; }
      k1 = _nOp[arg] === OP_ADD ? 1 : 0;
      if (kind !== T_ACOSH_KER) {
        if (!isOne(_nA[arg])) { return 0; }
        if (isX2(_nB[arg]) !== 1) { return 0; }
        if (kind === T_ASIN_KER && k1 === 1) { return 0; }
        if (kind === T_ASINH_KER && k1 === 0) { return 0; }
      } else {
        if (k1 === 1) { return 0; }
        if (isX2(_nA[arg]) !== 1) { return 0; }
        if (!isOne(_nB[arg])) { return 0; }
      }
      u = varX();
      if (u < 0) { return -1; }
      _bdU = u;
      return 1;
    }
    if (kind === T_CIRCLE || kind === T_HYPER) {
      if (isSqrtOf(r) !== 1) { return 0; }
      arg = _sqArg;
      if (_nOp[arg] !== OP_ADD && _nOp[arg] !== OP_SUB) { return 0; }
      k1 = _nOp[arg] === OP_ADD ? 1 : 0;
      if (kind === T_CIRCLE && k1 === 1) { return 0; }
      if (kind === T_HYPER && k1 === 0) { return 0; }
      if (!isOne(_nA[arg])) { return 0; }
      if (isX2(_nB[arg]) !== 1) { return 0; }
      u = varX();
      if (u < 0) { return -1; }
      _bdU = u;
      return 1;
    }
    if (kind === T_FUNC) {
      if (isLinFunc(r) !== 1) { return 0; }
      if (_lfF !== efunc) { return 0; }
      _bdU = _lfU; _bdA = _lfA; _bdB = _lfB;
      return 1;
    }
    if (kind === T_FUNC_POW) {
      if (_nOp[r] !== OP_POW) { return 0; }
      if (_nIval[r] !== epow) { return 0; }
      base = _nA[r];
      if (isLinFunc(base) !== 1) { return 0; }
      if (_lfF !== efunc) { return 0; }
      _bdU = _lfU; _bdA = _lfA; _bdB = _lfB;
      _bdN = _nIval[r];
      return 1;
    }
    if (kind === T_XN_FUNC) {
      if (_nOp[r] !== OP_MUL) { return 0; }
      if (_nOp[_nA[r]] === OP_FUNC) { fnode = _nA[r]; xnode = _nB[r]; }
      else if (_nOp[_nB[r]] === OP_FUNC) { fnode = _nB[r]; xnode = _nA[r]; }
      else { return 0; }
      if (_nIval[fnode] !== efunc) { return 0; }
      if (_nOp[_nA[fnode]] !== OP_VAR) { return 0; }
      if (_nIval[_nA[fnode]] !== V_X) { return 0; }
      if (isXpow(xnode) !== 1) { return 0; }
      m = _xpN;
      if (epow >= 0 && m !== epow) { return 0; }
      _bdU = _nA[fnode];
      _bdN = m;
      return 1;
    }
    if (kind === T_QUAD_L || kind === T_QUAD_K) {
      shape = epow;
      if (kind === T_QUAD_L) {
        if (shape < 0 || shape > 7) { return 0; }
        form = shape <= 1 ? 0 : shape <= 4 ? 1 : 2;
      } else {
        if (shape < 0 || shape > 8) { return 0; }
        form = shape === 8 ? 0 : shape <= 1 ? 0 : shape <= 4 ? 1 : 2;
      }
      if (form === 0) {
        if (_nOp[r] !== OP_DIV) { return 0; }
        if (!isOne(_nA[r])) { return 0; }
        q = _nB[r];
      } else if (form === 1) {
        if (_nOp[r] !== OP_DIV) { return 0; }
        if (!isOne(_nA[r])) { return 0; }
        if (isSqrtOf(_nB[r]) !== 1) { return 0; }
        q = _sqArg;
      } else {
        if (isSqrtOf(r) !== 1) { return 0; }
        q = _sqArg;
      }
      if (decompQuad(q) !== 1) { return 0; }
      u2 = _dqU; a = _dqA; b = _dqB; c = _dqC; u2s = _dqS;
      if (kind === T_QUAD_L) {
        if (shape === 0) { ok = u2s > 0 && c === 1 ? 1 : 0; }
        else if (shape === 1) { ok = u2s < 0 && c === 1 ? 1 : 0; }
        else if (shape === 2) { ok = u2s < 0 && c === 1 ? 1 : 0; }
        else if (shape === 3) { ok = u2s > 0 && c === 1 ? 1 : 0; }
        else if (shape === 4) { ok = u2s > 0 && c === -1 ? 1 : 0; }
        else if (shape === 5) { ok = u2s < 0 && c === 1 ? 1 : 0; }
        else if (shape === 6) { ok = u2s > 0 && c === 1 ? 1 : 0; }
        else if (shape === 7) { ok = u2s > 0 && c === -1 ? 1 : 0; }
        if (ok === 0) { return 0; }
        _bdU = u2; _bdA = a; _bdB = b;
        return 1;
      }
      kk = sqrtPerfect(c > 0 ? c : -c);
      if (kk === 0) { return 0; }
      if (shape === 0) { ok = u2s > 0 && c > 0 ? 1 : 0; }
      else if (shape === 1) { ok = u2s < 0 && c > 0 ? 1 : 0; }
      else if (shape === 2) { ok = u2s < 0 && c > 0 ? 1 : 0; }
      else if (shape === 3) { ok = u2s > 0 && c > 0 ? 1 : 0; }
      else if (shape === 4) { ok = u2s > 0 && c < 0 ? 1 : 0; }
      else if (shape === 5) { ok = u2s < 0 && c > 0 ? 1 : 0; }
      else if (shape === 6) { ok = u2s > 0 && c > 0 ? 1 : 0; }
      else if (shape === 7) { ok = u2s > 0 && c < 0 ? 1 : 0; }
      else if (shape === 8) { ok = u2s > 0 && c < 0 ? 1 : 0; }
      if (ok === 0) { return 0; }
      _bdU = u2; _bdA = a; _bdB = b;
      _bdN = kk;
      _bdM = kk * kk;
      return 1;
    }
    if (kind === T_XQUAD) {
      shape = epow;
      if (shape < 0 || shape > 2) { return 0; }
      if (_nOp[r] !== OP_DIV) { return 0; }
      if (decompQuad(_nB[r]) !== 1) { return 0; }
      u2 = _dqU; a = _dqA; b = _dqB; c = _dqC; u2s = _dqS;
      if (ilinear(_nA[r]) !== 1) { return 0; }
      a2 = _la; b2 = _lb;
      if (a2 === 0 || a2 !== a || b2 !== b) { return 0; }
      if (shape === 0 && !(u2s > 0 && c > 0)) { return 0; }
      if (shape === 1 && !(u2s > 0 && c < 0)) { return 0; }
      if (shape === 2 && !(u2s < 0 && c > 0)) { return 0; }
      _bdU = _nA[r]; _bdA = a; _bdB = b;
      _bdM = c > 0 ? c : -c;
      return 1;
    }
    if (kind === T_FUNC_INV) {
      if (_nOp[r] !== OP_DIV) { return 0; }
      if (!isOne(_nA[r])) { return 0; }
      if (isLinFunc(_nB[r]) !== 1) { return 0; }
      if (_lfF !== efunc) { return 0; }
      _bdU = _lfU; _bdA = _lfA; _bdB = _lfB;
      return 1;
    }
    if (kind === T_FUNC_INVP) {
      if (_nOp[r] !== OP_DIV) { return 0; }
      if (!isOne(_nA[r])) { return 0; }
      base = _nB[r];
      if (base < 0 || base >= _count) { return 0; }
      if (_nOp[base] !== OP_POW || _nIval[base] !== 2) { return 0; }
      if (isLinFunc(_nA[base]) !== 1) { return 0; }
      if (_lfF !== efunc) { return 0; }
      _bdU = _lfU; _bdA = _lfA; _bdB = _lfB;
      return 1;
    }
    if (kind === T_FUNC_DIV) {
      if (_nOp[r] !== OP_DIV) { return 0; }
      if (isLinFunc(_nA[r]) !== 1) { return 0; }
      u1 = _lfU; a = _lfA; b = _lfB; f1 = _lfF;
      if (isLinFunc(_nB[r]) !== 1) { return 0; }
      if (f1 !== efunc || _lfF !== epow) { return 0; }
      if (a !== _lfA || b !== _lfB) { return 0; }
      _bdU = u1; _bdA = a; _bdB = b;
      return 1;
    }
    if (kind === T_FUNC_FUNC) {
      if (_nOp[r] !== OP_MUL) { return 0; }
      if (isLinFunc(_nA[r]) !== 1) { return 0; }
      u1 = _lfU; a = _lfA; b = _lfB; f1 = _lfF;
      if (isLinFunc(_nB[r]) !== 1) { return 0; }
      if (a !== _lfA || b !== _lfB) { return 0; }
      if (f1 === efunc && _lfF === epow) { _bdU = u1; }
      else if (f1 === epow && _lfF === efunc) { _bdU = _lfU; }
      else { return 0; }
      _bdA = a; _bdB = b;
      return 1;
    }
    if (kind === T_SECTAN) {
      var inv = -1;
      var gnode = -1;
      if (_nOp[r] !== OP_MUL) { return 0; }
      if (_nOp[_nA[r]] === OP_DIV) { inv = _nA[r]; gnode = _nB[r]; }
      else if (_nOp[_nB[r]] === OP_DIV) { inv = _nB[r]; gnode = _nA[r]; }
      else { return 0; }
      if (!isOne(_nA[inv])) { return 0; }
      if (isLinFunc(_nB[inv]) !== 1) { return 0; }
      if (_lfF !== efunc) { return 0; }
      u1 = _lfU; a = _lfA; b = _lfB;
      if (isLinFunc(gnode) !== 1) { return 0; }
      if (_lfF !== epow) { return 0; }
      if (a !== _lfA || b !== _lfB) { return 0; }
      _bdU = u1; _bdA = a; _bdB = b;
      return 1;
    }
    if (kind === T_FUNCOVERFP) {
      if (_nOp[r] !== OP_DIV) { return 0; }
      if (isLinFunc(_nA[r]) !== 1) { return 0; }
      u1 = _lfU; a = _lfA; b = _lfB; f1 = _lfF;
      base = _nB[r];
      if (base < 0 || base >= _count) { return 0; }
      if (_nOp[base] !== OP_POW || _nIval[base] !== 2) { return 0; }
      if (isLinFunc(_nA[base]) !== 1) { return 0; }
      if (f1 !== efunc || _lfF !== epow) { return 0; }
      if (a !== _lfA || b !== _lfB) { return 0; }
      _bdU = u1; _bdA = a; _bdB = b;
      return 1;
    }
    if (kind === T_EXP_POW) {
      if (_nOp[r] !== OP_POW) { return 0; }
      if (_nIval[r] < 2) { return 0; }
      if (isLinFunc(_nA[r]) !== 1) { return 0; }
      if (_lfF !== 11) { return 0; }                     /* F_EXP */
      _bdU = _lfU; _bdA = _lfA; _bdB = _lfB;
      _bdN = _nIval[r];
      return 1;
    }
    if (kind === T_SIN2COS2) {
      if (_nOp[r] !== OP_MUL) { return 0; }
      p1 = _nA[r];
      p2 = _nB[r];
      if (_nOp[p1] !== OP_POW || _nIval[p1] !== 2) { return 0; }
      if (_nOp[p2] !== OP_POW || _nIval[p2] !== 2) { return 0; }
      if (isLinFunc(_nA[p1]) !== 1) { return 0; }
      u1 = _lfU; a = _lfA; b = _lfB; f1 = _lfF;
      if (isLinFunc(_nA[p2]) !== 1) { return 0; }
      if (a !== _lfA || b !== _lfB) { return 0; }
      if (f1 === 1 && _lfF === 2) { _bdU = u1; }         /* sin·cos */
      else if (f1 === 2 && _lfF === 1) { _bdU = _lfU; }
      else { return 0; }
      _bdA = a; _bdB = b;
      return 1;
    }
    if (kind === T_LN_OVER_X) {
      if (_nOp[r] !== OP_DIV) { return 0; }
      if (isLinFunc(_nA[r]) !== 1) { return 0; }
      if (_lfF !== 9) { return 0; }                      /* F_LN */
      u = _lfU; a = _lfA; b = _lfB;
      if (ilinear(_nB[r]) !== 1) { return 0; }
      if (a !== _la || b !== _lb) { return 0; }
      _bdU = u; _bdA = a; _bdB = b;
      return 1;
    }
    if (kind === T_ONE_OVER_ULN) {
      if (_nOp[r] !== OP_DIV) { return 0; }
      if (!isOne(_nA[r])) { return 0; }
      mulnode = _nB[r];
      if (mulnode < 0 || mulnode >= _count) { return 0; }
      if (_nOp[mulnode] !== OP_MUL) { return 0; }
      s = _nA[mulnode];
      t2 = _nB[mulnode];
      var hit = 0;
      if (isLinFunc(t2) === 1 && _lfF === 9 && ilinear(s) === 1 &&
          _lfA === _la && _lfB === _lb) {
        _bdU = s; _bdA = _la; _bdB = _lb; hit = 1;
      } else if (isLinFunc(s) === 1 && _lfF === 9 && ilinear(t2) === 1 &&
                 _lfA === _la && _lfB === _lb) {
        _bdU = t2; _bdA = _la; _bdB = _lb; hit = 1;
      }
      return hit;
    }
    return 0;
  }

  /* 多项式归一(C pro_int_poly_fetch/poly_integrate) */

  function polyAlloc() {
    if (_pfFreeTop <= 0) { return -1; }
    _pfFreeTop--;
    return _polyFree[_pfFreeTop];
  }
  function polyRelease(sl) {
    _polyFree[_pfFreeTop] = sl;
    _pfFreeTop++;
  }
  var _pfFreeTop = 0;

  function polyZero(sl) {
    var i = 0;
    for (i = 0; i <= POLY_MAX; i++) { _polyPool[sl * 17 + i] = 0; }
  }
  function polyCopy(ds, sl) {
    var i = 0;
    for (i = 0; i <= POLY_MAX; i++) { _polyPool[ds * 17 + i] = _polyPool[sl * 17 + i]; }
  }
  /* dst = s1 ⊗ s2;越界(>1e6)→ 0 */
  function polyMul(ds, s1, s2) {
    var i = 0;
    var j = 0;
    var v = 0;
    for (i = 0; i <= POLY_MAX; i++) { _polyPool[ds * 17 + i] = 0; }
    for (i = 0; i <= POLY_MAX; i++) {
      if (_polyPool[s1 * 17 + i] === 0) { continue; }
      for (j = 0; i + j <= POLY_MAX; j++) {
        if (_polyPool[s2 * 17 + j] === 0) { continue; }
        v = _polyPool[ds * 17 + i + j] + _polyPool[s1 * 17 + i] * _polyPool[s2 * 17 + j];
        if (v > MAX_CONST || v < -MAX_CONST) { return 0; }
        _polyPool[ds * 17 + i + j] = v;
      }
    }
    return 1;
  }

  /* 节点 → 多项式(升幂 Int32);失败 −1。栈元素 node|phase<<9|slot<<10。 */
  function polyFetch(r) {
    var sp = 0;
    var e = 0;
    var nd = 0;
    var phase = 0;
    var slot = 0;
    var sl1 = 0;
    var sl2 = 0;
    var slA = 0;
    var slT = 0;
    var i = 0;
    var d = 0;
    var kk = 0;
    var op = 0;
    var v = 0;

    _pfFreeTop = 0;
    for (i = 0; i < 64; i++) { _polyFree[i] = 63 - i; }
    _pfFreeTop = 64;

    _polyStack[sp] = r;                                /* 根走 phase0 正常分配槽 */
    sp++;
    while (sp > 0) {
      sp--;
      e = _polyStack[sp];
      nd = e & 511;
      phase = (e >> 9) & 1;
      slot = (e >> 10) & 1023;
      op = _nOp[nd];
      if (phase === 0) {
        if (op === OP_CONST || op === OP_VAR) {
          slot = polyAlloc();
          if (slot < 0) { return -1; }
          polyZero(slot);
          if (op === OP_CONST) { _polyPool[slot * 17] = _nIval[nd]; }
          else { _polyPool[slot * 17 + 1] = 1; }
          _polySlot[nd] = slot;
          continue;
        }
        if (sp + 1 >= _polyStack.length) { return -1; }
        slot = polyAlloc();
        if (slot < 0) { return -1; }
        polyZero(slot);
        _polySlot[nd] = slot;                          /* phase1 子读取依赖此记录 */
        _polyStack[sp] = nd | (1 << 9) | (slot << 10); sp++;
        if (op === OP_NEG) { _polyStack[sp] = _nA[nd]; sp++; }
        else if (op === OP_ADD || op === OP_SUB || op === OP_MUL || op === OP_DIV) {
          _polyStack[sp] = _nB[nd]; sp++;
          _polyStack[sp] = _nA[nd]; sp++;
        } else if (op === OP_POW) {
          _polyStack[sp] = _nA[nd]; sp++;
        } else { return -1; }
        continue;
      }
      /* phase 1 */
      if (op === OP_CONST) { continue; }                 /* phase0 已落 */
      if (op === OP_VAR) { continue; }
      if (op === OP_NEG) {
        sl1 = _polySlot[_nA[nd]];
        for (i = 0; i <= POLY_MAX; i++) {
          _polyPool[slot * 17 + i] = -_polyPool[sl1 * 17 + i];
        }
        polyRelease(sl1);
        continue;
      }
      if (op === OP_ADD || op === OP_SUB) {
        sl1 = _polySlot[_nA[nd]];
        sl2 = _polySlot[_nB[nd]];
        v = op === OP_ADD ? 1 : -1;
        for (i = 0; i <= POLY_MAX; i++) {
          _polyPool[slot * 17 + i] = _polyPool[sl1 * 17 + i] + v * _polyPool[sl2 * 17 + i];
        }
        polyRelease(sl1);
        polyRelease(sl2);
        continue;
      }
      if (op === OP_MUL) {
        sl1 = _polySlot[_nA[nd]];
        sl2 = _polySlot[_nB[nd]];
        if (polyMul(slot, sl1, sl2) !== 1) { return -1; }
        polyRelease(sl1);
        polyRelease(sl2);
        continue;
      }
      if (op === OP_DIV) {
        if (!isConst(_nB[nd])) { return -1; }
        d = _nIval[_nB[nd]];
        if (d === 0) { return -1; }
        sl1 = _polySlot[_nA[nd]];
        for (i = 0; i <= POLY_MAX; i++) {
          v = _polyPool[sl1 * 17 + i];
          if (v % d !== 0) { return -1; }
          _polyPool[slot * 17 + i] = v / d;
        }
        polyRelease(sl1);
        continue;
      }
      if (op === OP_POW) {
        kk = _nIval[nd];
        if (kk < 0 || kk > POLY_MAX) { return -1; }
        sl1 = _polySlot[_nA[nd]];
        slA = polyAlloc();
        slT = polyAlloc();
        if (slA < 0 || slT < 0) { return -1; }
        polyZero(slA);
        _polyPool[slA * 17] = 1;
        for (i = 0; i < kk; i++) {
          if (polyMul(slT, slA, sl1) !== 1) { return -1; }
          polyCopy(slA, slT);
        }
        polyCopy(slot, slA);
        polyRelease(slA);
        polyRelease(slT);
        polyRelease(sl1);
        continue;
      }
      return -1;
    }
    return _polySlot[r];
  }

  /* 逐项积分;命中 1(out 写 _pfOut),非多项式 0 */
  var _pfOut = -1;
  function polyIntegrate(r) {
    var slot = polyFetch(r);
    var k = 0;
    var coef = 0;
    var d = 0;
    var sum = -1;
    var pw = 0;
    var term = 0;
    var num = 0;
    if (slot < 0) { return 0; }
    for (k = POLY_MAX; k >= 0; k--) {
      coef = _polyPool[slot * 17 + k];
      d = k + 1;
      if (coef === 0) { continue; }
      pw = xpow(d);
      if (pw < 0) { return 0; }
      if (coef % d === 0) {
        term = imul(constN(coef / d), pw);
      } else {
        num = imul(constN(coef), pw);
        if (num < 0) { return 0; }
        term = idiv(num, constN(d));
      }
      if (term < 0) { return 0; }
      sum = sum < 0 ? term : iadd(sum, term);
      if (sum < 0) { return 0; }
    }
    if (sum < 0) { sum = constN(0); }
    if (sum < 0) { return 0; }
    _pfOut = sum;
    return 1;
  }

  /* 分部积分(C pro_int_try_parts,固定两层) */

  /* r = MUL(x^n, f) / MUL(f, x^n),f ∈ {EXP,COS,SIN,SINH,COSH} 且实参=x。
   * 命中:构造 inner = (n−1) 次幂 × 反函数,压 (r, phase2, inner) 续体,
   * 返回 1;未命中 0;预算 −1。 */
  function tryPartsPush(r, parts) {
    var fnode = 0;
    var xnode = 0;
    var n = 0;
    var fid = 0;
    var vsign = 0;
    var fa = 0;
    var inner = 0;

    if (_nOp[r] !== OP_MUL) { return 0; }
    if (_nOp[_nA[r]] === OP_FUNC) { fnode = _nA[r]; xnode = _nB[r]; }
    else if (_nOp[_nB[r]] === OP_FUNC) { fnode = _nB[r]; xnode = _nA[r]; }
    else { return 0; }
    if (_nOp[_nA[fnode]] !== OP_VAR) { return 0; }
    if (_nIval[_nA[fnode]] !== V_X) { return 0; }
    if (isXpow(xnode) !== 1) { return 0; }
    n = _xpN;
    if (n < 1 || n > MAX_POW_IN) { return 0; }
    fid = _nIval[fnode];
    if (fid === 11) { vsign = 1; fa = 11; }              /* EXP → EXP */
    else if (fid === 2) { vsign = 1; fa = 1; }           /* COS → SIN */
    else if (fid === 1) { vsign = -1; fa = 2; }          /* SIN → COS */
    else if (fid === 13) { vsign = 1; fa = 14; }         /* SINH → COSH */
    else if (fid === 14) { vsign = 1; fa = 13; }         /* COSH → SINH */
    else { return 0; }

    _partsUsed = 1;
    var px = xpow(n - 1);
    if (px < 0) { return -1; }
    var fa2 = ifunc(fa, varX());
    if (fa2 < 0) { return -1; }
    inner = imul(px, fa2);
    if (inner < 0) { return -1; }
    /* 压续体:phase2 项在前(后出),inner 求积分项在后(先出) */
    if (_wTop + 2 > _wN.length) { return -1; }
    _wN[_wTop] = r;
    _wMeta[_wTop] = 2 | (parts << 2) | (inner << 4);
    _wTop++;
    _wN[_wTop] = inner;
    _wMeta[_wTop] = 0 | ((parts - 1) << 2);
    _wTop++;
    return 1;
  }

  /* 驱动(C pro_int_integrate_rec 显式栈化) */

  function drive(root) {
    var e = 0;
    var r = 0;
    var phase = 0;
    var parts = 0;
    var inner = 0;
    var op = 0;
    var la = 0;
    var ra = 0;
    var res = 0;
    var d = 0;
    var c = 0;
    var k = 0;
    var hit = 0;
    var savedCount = 0;
    var savedXvar = 0;
    var antiRoot = -1;
    var lexrc = 0;
    var rpnLen = 0;
    var fnode = 0;
    var fid = 0;
    var vsign = 0;
    var fa = 0;
    var n = 0;
    var term1 = 0;
    var term2 = 0;

    for (k = 0; k < _count; k++) { _dmap[k] = -1; }
    _wTop = 0;
    _wN[_wTop] = root;
    _wMeta[_wTop] = 0 | (PARTS_LAYERS << 2);
    _wTop++;
    while (_wTop > 0) {
      _wTop--;
      r = _wN[_wTop];
      e = _wMeta[_wTop];
      phase = e & 3;
      parts = (e >> 2) & 3;
      inner = (e >> 4) & 1023;
      if (r < 0 || r >= _count) { return EPARSE; }
      if (_overflow !== 0) { return EBUDGET; }
      op = _nOp[r];

      if (phase === 2) {
        /* 分部续体:inner_anti 已就绪,重组 −x^n·f + n·∫ 或 x^n·f − n·∫;
         * 内层未命中(−1)→ 整体未命中,转 phase3 走多项式归一(C 同款:
         * try_parts 内层失败 → 整体未命中 → 落 poly) */
        var innerAnti = _dmap[inner];
        if (innerAnti < 0) {
          if (sp + 1 >= _wN.length) { return EBUDGET; }
          _wN[sp] = r;
          _wMeta[sp] = 3 | (parts << 2);
          sp++;
          continue;
        }
        if (_nOp[r] !== OP_MUL) { return EPARSE; }
        if (_nOp[_nA[r]] === OP_FUNC) { fnode = _nA[r]; }
        else if (_nOp[_nB[r]] === OP_FUNC) { fnode = _nB[r]; }
        else { return EPARSE; }
        fid = _nIval[fnode];
        if (fid === 11) { vsign = 1; fa = 11; }
        else if (fid === 2) { vsign = 1; fa = 1; }
        else if (fid === 1) { vsign = -1; fa = 2; }
        else if (fid === 13) { vsign = 1; fa = 14; }
        else if (fid === 14) { vsign = 1; fa = 13; }
        else { return EPARSE; }
        var xx = _nA[r] === fnode ? _nB[r] : _nA[r];
        if (isXpow(xx) !== 1) { return EPARSE; }
        n = _xpN;
        var t1p = xpow(n);
        var t1f = ifunc(fa, varX());
        if (t1p < 0 || t1f < 0) { return EBUDGET; }
        term1 = imul(t1p, t1f);
        if (term1 < 0) { return EBUDGET; }
        term2 = imul(constN(n), innerAnti);
        if (term2 < 0) { return EBUDGET; }
        if (vsign < 0) {
          var nt1 = ineg(term1);
          if (nt1 < 0) { return EBUDGET; }
          res = iadd(nt1, term2);
        } else {
          res = isub(term1, term2);
        }
        if (res < 0) { return EBUDGET; }
        _dmap[r] = res;
        continue;
      }

      if (phase === 3) {
        /* 分部未命中后的多项式归一(C 同款次序) */
        if (polyIntegrate(r) === 1) {
          _dmap[r] = _pfOut;
        } else {
          return ETABLE;
        }
        continue;
      }

      if (phase === 1) {
        if (op === OP_ADD || op === OP_SUB) {
          la = _dmap[_nA[r]];
          ra = _dmap[_nB[r]];
          if (la < 0 || ra < 0) { return ETABLE; }
          res = op === OP_ADD ? iadd(la, ra) : isub(la, ra);
        } else if (op === OP_NEG) {
          la = _dmap[_nA[r]];
          if (la < 0) { return ETABLE; }
          res = ineg(la);
        } else if (op === OP_MUL) {
          if (isConst(_nA[r])) {
            c = _nIval[_nA[r]];
            la = _dmap[_nB[r]];
          } else {
            c = _nIval[_nB[r]];
            la = _dmap[_nA[r]];
          }
          if (la < 0) { return ETABLE; }
          if (_nOp[la] === OP_DIV && isConst(_nB[la])) {
            d = _nIval[_nB[la]];
            if (d !== 0 && c % d === 0) {
              res = imul(constN(c / d), _nA[la]);
              if (res < 0) { return EBUDGET; }
              _dmap[r] = res;
              continue;
            }
          }
          res = imul(constN(c), la);
        } else {                                           /* OP_DIV 常数分母 */
          la = _dmap[_nA[r]];
          if (la < 0) { return ETABLE; }
          d = _nIval[_nB[r]];
          res = idiv(la, constN(d));
        }
        if (res < 0) { return EBUDGET; }
        _dmap[r] = res;
        continue;
      }

      /* phase 0:展开 */
      if (op === OP_ADD || op === OP_SUB) {
        if (_wTop + 3 >= _wN.length) { return EBUDGET; }
        _wN[_wTop] = r; _wMeta[_wTop] = 1 | (parts << 2); _wTop++;
        _wN[_wTop] = _nB[r]; _wMeta[_wTop] = 0 | (parts << 2); _wTop++;
        _wN[_wTop] = _nA[r]; _wMeta[_wTop] = 0 | (parts << 2); _wTop++;
        continue;
      }
      if (op === OP_NEG) {
        if (_wTop + 2 >= _wN.length) { return EBUDGET; }
        _wN[_wTop] = r; _wMeta[_wTop] = 1 | (parts << 2); _wTop++;
        _wN[_wTop] = _nA[r]; _wMeta[_wTop] = 0 | (parts << 2); _wTop++;
        continue;
      }
      if (op === OP_MUL && (isConst(_nA[r]) || isConst(_nB[r]))) {
        if (_wTop + 2 >= _wN.length) { return EBUDGET; }
        _wN[_wTop] = r; _wMeta[_wTop] = 1 | (parts << 2); _wTop++;
        _wN[_wTop] = isConst(_nA[r]) ? _nB[r] : _nA[r];
        _wMeta[_wTop] = 0 | (parts << 2); _wTop++;
        continue;
      }
      if (op === OP_DIV && isConst(_nB[r])) {
        if (_nIval[_nB[r]] === 0) { return EPARSE; }
        if (_wTop + 2 >= _wN.length) { return EBUDGET; }
        _wN[_wTop] = r; _wMeta[_wTop] = 1 | (parts << 2); _wTop++;
        _wN[_wTop] = _nA[r]; _wMeta[_wTop] = 0 | (parts << 2); _wTop++;
        continue;
      }

      /* 叶:128 项表 */
      hit = 0;
      for (k = 0; k < 128; k++) {
        var mh = matchEntry(k, r);
        if (mh < 0) { return EBUDGET; }
        if (mh === 1) {
          savedCount = _count;
          savedXvar = _xvar;
          _binding = 1;
          lexrc = lexTemplate(_T_ANTI, _T_OFFS[k], k < 127 ? _T_OFFS[k + 1] - 1 : _T_ANTI.length);
          _binding = 0;
          if (lexrc < 0) {
            _count = savedCount;
            _xvar = savedXvar;
            _overflow = 0;
            _errCode = lexrc === -5 ? EDEGREE : EPARSE;
            return _errCode;
          }
          _evalError = 0;
          rpnLen = toRPN(_tplTok, lexrc, _rpnToken);
          if (_evalError !== 0 || rpnLen <= 0) {
            _count = savedCount;
            _xvar = savedXvar;
            _overflow = 0;
            return EPARSE;
          }
          _binding = 1;
          antiRoot = translateRPN(_rpnToken, rpnLen, _tplNum);
          _binding = 0;
          if (antiRoot < 0) {
            var ec = _errCode;
            _count = savedCount;
            _xvar = savedXvar;
            _overflow = 0;
            return ec === EBUDGET ? EBUDGET : EPARSE;
          }
          _dmap[r] = antiRoot;
          hit = 1;
          break;
        }
      }
      if (hit === 1) { continue; }

      /* 固定两层分部 */
      if (parts > 0) {
        var pp = tryPartsPush(r, parts);
        if (pp < 0) { return EBUDGET; }
        if (pp === 1) { continue; }
      }

      /* 多项式归一 */
      if (polyIntegrate(r) === 1) {
        _dmap[r] = _pfOut;
        continue;
      }

      return ETABLE;
    }
    return _dmap[root];
  }

  /* 渲染(C pro_int_emit 显式栈化) */

  function emitPrec(i) {
    var op = _nOp[i];
    if (op === OP_ADD || op === OP_SUB) { return 1; }
    if (op === OP_MUL || op === OP_DIV || op === OP_NEG) { return 2; }
    if (op === OP_POW) { return 3; }
    return 4;
  }
  function pushCC(ch) {
    if (_outLen < OUTBUF) { _outBuf[_outLen] = ch; }
    _outLen++;
  }
  function putn(v) {
    var n = 0;
    var m = v;
    var neg = 0;
    if (m < 0) { neg = 1; m = -m; }
    if (m === 0) { _digits[0] = 0; n = 1; }
    else {
      while (m > 0) {
        _digits[n] = m % 10;
        n++;
        m = (m - (m % 10)) / 10;
      }
    }
    if (neg === 1) { pushCC(45); }
    while (n > 0) { n--; pushCC(48 + _digits[n]); }
  }

  function renderEmit(root) {
    var sp = 0;
    var e = 0;
    var nd = 0;
    var mp = 0;
    var paren = 0;
    var op = 0;
    var right = 0;
    var rp = 0;
    var name = "";
    var kk = 0;

    _outLen = 0;
    _emitStack[sp] = root;
    sp++;
    while (sp > 0) {
      sp--;
      e = _emitStack[sp];
      if (e < 0) {
        if (e <= -65536) { putn(_nIval[-e - 65536]); }   /* POW 指数字面量 */
        else { pushCC(-e - 1); }
        continue;
      }
      nd = e & 65535;
      mp = e >> 16;
      paren = emitPrec(nd) < mp ? 1 : 0;
      if (paren === 1) { pushCC(40); }
      if (paren === 1) { _emitStack[sp] = -42; sp++; }   /* ')' */
      if (sp + 8 >= _emitStack.length) { return EOVER; }
      op = _nOp[nd];
      if (op === OP_CONST) { putn(_nIval[nd]); }
      else if (op === OP_VAR) { pushCC(120); }
      else if (op === OP_NEG) {
        _emitStack[sp] = (_nA[nd] & 65535) | (2 << 16); sp++;
        _emitStack[sp] = -46; sp++;                      /* '-' */
      } else if (op === OP_ADD || op === OP_SUB) {
        right = _nB[nd];
        if (op === OP_ADD && right >= 0 && _nOp[right] === OP_NEG) {
          _emitStack[sp] = (_nA[right] & 65535) | (2 << 16); sp++;
          _emitStack[sp] = -46; sp++;                    /* '-' */
          _emitStack[sp] = (_nA[nd] & 65535) | (1 << 16); sp++;
        } else {
          _emitStack[sp] = (right & 65535) | (2 << 16); sp++;
          _emitStack[sp] = op === OP_ADD ? -44 : -46; sp++;
          _emitStack[sp] = (_nA[nd] & 65535) | (1 << 16); sp++;
        }
      } else if (op === OP_MUL) {
        right = _nB[nd];
        rp = (right >= 0 && _nOp[right] === OP_MUL) ? 2 : 3;
        if (right >= 0 && _nOp[right] === OP_DIV) {
          /* a*(b/c) 摊平 a*b/c */
          _emitStack[sp] = (_nB[right] & 65535) | (3 << 16); sp++;
          _emitStack[sp] = -48; sp++;                    /* '/' */
          _emitStack[sp] = (_nA[right] & 65535) | (2 << 16); sp++;
        } else {
          _emitStack[sp] = (right & 65535) | (rp << 16); sp++;
        }
        _emitStack[sp] = -43; sp++;                      /* '*' */
        _emitStack[sp] = (_nA[nd] & 65535) | (2 << 16); sp++;
      } else if (op === OP_DIV) {
        _emitStack[sp] = (_nB[nd] & 65535) | (3 << 16); sp++;
        _emitStack[sp] = -48; sp++;                      /* '/' */
        _emitStack[sp] = (_nA[nd] & 65535) | (2 << 16); sp++;
      } else if (op === OP_POW) {
        /* 出栈序:底(4) / '^' / putn(ival) —— putn 条目 = -(65536+节点) */
        _emitStack[sp] = -(65536 + nd); sp++;
        _emitStack[sp] = -95; sp++;                      /* '^' */
        _emitStack[sp] = (_nA[nd] & 65535) | (4 << 16); sp++;
      } else if (op === OP_FUNC) {
        name = funcName(_nIval[nd]);
        _emitStack[sp] = -42; sp++;                      /* ')' */
        _emitStack[sp] = (_nA[nd] & 65535) | (1 << 16); sp++;
        _emitStack[sp] = -41; sp++;                      /* '(' */
        for (kk = name.length - 1; kk >= 0; kk--) {
          _emitStack[sp] = -(name.charCodeAt(kk) + 1);
          sp++;
        }
      } else {
        return EPARSE;
      }
    }
    if (_outLen > OUTBUF) { return EOVER; }
    return OK;
  }

  /* 主入口 */

  function integrate(expr) {
    var rc = 0;
    var n = 0;
    var root = -1;
    var anti = -1;
    var cap = 0;

    _result = "";
    _count = 0;
    _xvar = -1;
    _overflow = 0;
    _partsUsed = 0;
    _errCode = OK;

    if (typeof expr !== "string" || expr.length === 0) { return EPARSE; }
    rc = parseExpression(expr);
    if (rc !== 0) { return EPARSE; }

    _evalError = 0;
    n = toRPN(tokenPool, tokenCount, _rpnToken);
    if (_evalError !== 0 || n <= 0) { return EPARSE; }

    _binding = 0;
    root = translateRPN(_rpnToken, n, numberPool);
    if (root < 0) {
      return _overflow !== 0 ? EBUDGET : (_errCode !== OK ? _errCode : EPARSE);
    }

    anti = drive(root);
    if (anti < 0) { return anti; }
    if (anti < 0 || anti >= _count) { return ETABLE; }

    cap = _partsUsed !== 0 ? BUDGET_TOTAL : BUDGET_TABLE;
    if (_count > cap || _count > POOL_NODES) { return EBUDGET; }

    var rrc = renderEmit(anti);
    if (rrc !== OK) { return rrc; }
    var s = "";
    var i = 0;
    for (i = 0; i < _outLen; i++) { s = s + String.fromCharCode(_outBuf[i]); }
    _result = s;
    return OK;
  }

  function reset() {
    _count = 0;
    _xvar = -1;
    _overflow = 0;
    _partsUsed = 0;
    _binding = 0;
    _result = "";
    _errCode = OK;
    return OK;
  }

  return {
    integrate: integrate,
    getString: getString,
    reset: reset,
    allocCount: allocCount
  };
})();

if (typeof module === "object" && module && module.exports) {
  module.exports = ApolloIntegrate;
}
