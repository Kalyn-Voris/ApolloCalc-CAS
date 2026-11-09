// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Kalyn-Voris (ApolloCalc-CAS). See LICENSE at the repository root.

using System.Globalization;
using System.Runtime.InteropServices;
using System.Windows.Forms;

namespace ApolloCalc.Desktop;

/// <summary>
/// 主窗体：输入 f(x)，点击 Plot 后由 C99 引擎（ApolloCalc.dll，见
/// ../c_bridge/ac_gui_bridge.c）在 [-10, 10] 上按 0.1 步长采样，
/// 再用 GDI+ 绘制曲线。所有求值都走 C 内核，C# 侧只做采样与绘制。
/// </summary>
public sealed class MainForm : Form
{
    // ---- P/Invoke：与 c_bridge/ac_gui_bridge.h 的导出签名逐字对齐 ----
    [DllImport("ApolloCalc.dll", CharSet = CharSet.Ansi)]
    private static extern double ac_gui_eval(string expr, double x);

    [DllImport("ApolloCalc.dll", CharSet = CharSet.Ansi)]
    private static extern void ac_gui_set_angle_mode(int deg);

    private const double XMin = -10.0;
    private const double XMax = 10.0;
    private const double Step = 0.1;
    private const int SampleCount = 201;   // (XMax - XMin) / Step + 1
    private const int PlotMargin = 46;     // 绘图区四周留白（像素，含刻度文字）

    private readonly TextBox _input = new();
    private readonly Button _plotButton = new();
    private readonly CheckBox _radMode = new();
    private readonly PictureBox _canvas = new();
    private readonly Label _status = new();
    private readonly Label _license = new();

    // 最近一次采样结果（Paint 时按当前窗口尺寸重新映射，窗口缩放免重采样）
    private double[] _ys = Array.Empty<double>();
    private bool _hasData;
    private double _yMin, _yMax;

    public MainForm()
    {
        Text = Strings.T("Title");
        ClientSize = new Size(860, 560);
        MinimumSize = new Size(560, 430);
        Font = new Font("Segoe UI", 9.5f);

        var top = new Panel { Dock = DockStyle.Top, Height = 84 };

        var inputLabel = new Label
        {
            Text = "f(x) =",
            AutoSize = true,
            Location = new Point(12, 13),
        };

        _input.Location = new Point(58, 9);
        _input.Size = new Size(600, 27);
        _input.Text = "sin(x)*exp(-x/5)";

        _plotButton.Text = Strings.T("Plot");
        _plotButton.Location = new Point(670, 8);
        _plotButton.Size = new Size(90, 29);
        _plotButton.Click += PlotButton_Click;

        // 角度制开关：核心默认 DEG（镜像 JS angleMode="DEG"），勾选切到弧度制
        _radMode.Text = Strings.T("Radians");
        _radMode.AutoSize = true;
        _radMode.Location = new Point(772, 12);
        _radMode.CheckedChanged += RadMode_CheckedChanged;

        _status.AutoSize = true;
        _status.ForeColor = Color.DimGray;
        _status.Location = new Point(12, 50);

        top.Controls.AddRange(new Control[] { inputLabel, _input, _plotButton, _radMode, _status });

        _canvas.Dock = DockStyle.Fill;
        _canvas.BackColor = Color.White;
        _canvas.BorderStyle = BorderStyle.FixedSingle;
        _canvas.Paint += Canvas_Paint;
        _canvas.Resize += static (sender, _) => ((PictureBox)sender!).Invalidate();

        // 底部版权栏：法律文本不喧宾夺主（小号、暗灰、右对齐）
        var bottomBar = new Panel { Dock = DockStyle.Bottom, Height = 26, Padding = new Padding(10, 0, 12, 4) };
        _license.Dock = DockStyle.Fill;
        _license.TextAlign = ContentAlignment.MiddleRight;
        _license.ForeColor = Color.FromArgb(140, 149, 159);   // #8c959f
        _license.Font = new Font("Segoe UI", 8.5f);
        _license.Text = Strings.T("License");
        bottomBar.Controls.Add(_license);

        Controls.Add(_canvas);
        Controls.Add(bottomBar);
        Controls.Add(top);

        AcceptButton = _plotButton;   // 输入框里按 Enter 直接绘图

        Load += MainForm_Load;
    }

    private void MainForm_Load(object? sender, EventArgs e)
    {
        try
        {
            // 与核心默认值显式同步（g_angle_deg 初始即 1，这里只是把
            // 复选框状态和内核状态对齐成一处真相）
            ac_gui_set_angle_mode(_radMode.Checked ? 0 : 1);
            // 探针调用：DLL 缺失 / 入口不符时尽早给出可读错误
            _ = ac_gui_eval("0", 0.0);
            Plot();
        }
        catch (DllNotFoundException)
        {
            MessageBox.Show(
                Strings.T("DllMissing"),
                Strings.T("DllMissingTitle"),
                MessageBoxButtons.OK, MessageBoxIcon.Warning);
        }
    }

    private void PlotButton_Click(object? sender, EventArgs e) => Plot();

    /// <summary>切换角度/弧度制；已有表达式时立即重绘。</summary>
    private void RadMode_CheckedChanged(object? sender, EventArgs e)
    {
        ac_gui_set_angle_mode(_radMode.Checked ? 0 : 1);
        if (_input.Text.Trim().Length > 0)
        {
            Plot();
        }
    }

    /// <summary>在 [-10,10] 上以 0.1 步长采样；非法点（解析/求值失败）记为 NaN。</summary>
    private void Plot()
    {
        string expr = _input.Text.Trim();
        if (expr.Length == 0)
        {
            _status.Text = Strings.T("StatusEmpty");
            return;
        }

        var ys = new double[SampleCount];
        int finite = 0;
        double lo = double.PositiveInfinity, hi = double.NegativeInfinity;

        for (int i = 0; i < SampleCount; i++)
        {
            double x = XMin + Step * i;
            double y = ac_gui_eval(expr, x);
            if (double.IsFinite(y))
            {
                ys[i] = y;
                finite++;
                if (y < lo) lo = y;
                if (y > hi) hi = y;
            }
            else
            {
                ys[i] = double.NaN;
            }
        }

        if (finite == 0)
        {
            _hasData = false;
            _ys = ys;
            _status.Text = string.Format(Strings.T("StatusInvalid"), XMin, XMax, expr);
            _canvas.Invalidate();
            return;
        }

        // y 自适应量程：10% 边距；常数曲线（lo == hi）人为展开避免除零
        if (hi - lo < 1e-12)
        {
            double span = Math.Max(1.0, Math.Abs(lo) * 0.2);
            lo -= span;
            hi += span;
        }
        double pad = 0.1 * (hi - lo);
        _yMin = lo - pad;
        _yMax = hi + pad;
        _ys = ys;
        _hasData = true;
        _status.Text = string.Format(Strings.T("StatusPlot"),
            expr,
            lo.ToString("G6", CultureInfo.InvariantCulture),
            hi.ToString("G6", CultureInfo.InvariantCulture),
            finite, SampleCount);
        _canvas.Invalidate();
    }

    private void Canvas_Paint(object? sender, PaintEventArgs e)
    {
        Graphics g = e.Graphics;
        g.SmoothingMode = System.Drawing.Drawing2D.SmoothingMode.AntiAlias;
        g.Clear(Color.White);

        int w = _canvas.ClientSize.Width;
        int h = _canvas.ClientSize.Height;
        if (w <= 2 * PlotMargin || h <= 2 * PlotMargin)
        {
            return;
        }
        var plot = new RectangleF(PlotMargin, PlotMargin, w - 2 * PlotMargin, h - 2 * PlotMargin);

        if (!_hasData)
        {
            using var hintFont = new Font(Font, FontStyle.Italic);
            using var hintFormat = new StringFormat
            {
                Alignment = StringAlignment.Center,
                LineAlignment = StringAlignment.Center,
            };
            g.DrawString(Strings.T("PaintHint"), hintFont, Brushes.Silver, plot, hintFormat);
            return;
        }

        using var gridPen = new Pen(Color.FromArgb(233, 236, 239));
        using var axisPen = new Pen(Color.FromArgb(140, 146, 152));
        using var textBrush = new SolidBrush(Color.FromArgb(122, 128, 134));
        using var curvePen = new Pen(Color.FromArgb(24, 100, 210), 2f);
        using var tickFont = new Font("Consolas", 8f);

        float MapX(double x) => plot.Left + (float)((x - XMin) / (XMax - XMin) * plot.Width);
        float MapY(double y) => plot.Bottom - (float)((y - _yMin) / (_yMax - _yMin) * plot.Height);

        // 网格：x 方向固定 2 单位；y 方向取 1-2-5×10^k 的"好看"步长
        double yStep = NiceStep((_yMax - _yMin) / 8);

        for (double gx = XMin; gx <= XMax + 1e-9; gx += 2.0)
        {
            float px = MapX(gx);
            g.DrawLine(gridPen, px, plot.Top, px, plot.Bottom);
            g.DrawString(gx.ToString("0", CultureInfo.InvariantCulture), tickFont,
                textBrush, px - 8, plot.Bottom + 3);
        }

        for (double gy = Math.Ceiling(_yMin / yStep) * yStep; gy <= _yMax + yStep * 0.5; gy += yStep)
        {
            float py = MapY(gy);
            g.DrawLine(gridPen, plot.Left, py, plot.Right, py);
            g.DrawString(gy.ToString("G4", CultureInfo.InvariantCulture), tickFont,
                textBrush, plot.Left - 38, py - 6);
        }

        // 坐标轴（x = 0 / y = 0）
        float x0 = MapX(0);
        if (x0 >= plot.Left && x0 <= plot.Right)
        {
            g.DrawLine(axisPen, x0, plot.Top, x0, plot.Bottom);
        }
        float y0 = MapY(0);
        if (y0 >= plot.Top && y0 <= plot.Bottom)
        {
            g.DrawLine(axisPen, plot.Left, y0, plot.Right, y0);
        }

        // 曲线：NaN 处断笔（除零、定义域外的采样点不连线）
        bool penDown = false;
        PointF prev = default;
        for (int i = 0; i < _ys.Length; i++)
        {
            double y = _ys[i];
            if (double.IsNaN(y))
            {
                penDown = false;
                continue;
            }
            var p = new PointF(MapX(XMin + Step * i), MapY(y));
            if (penDown)
            {
                g.DrawLine(curvePen, prev, p);
            }
            else
            {
                g.DrawEllipse(curvePen, p.X - 1, p.Y - 1, 2, 2);
            }
            prev = p;
            penDown = true;
        }
    }

    /// <summary>把任意步长取整到 1-2-5×10^k 序列，让网格刻度可读。</summary>
    private static double NiceStep(double raw)
    {
        if (raw <= 0 || !double.IsFinite(raw))
        {
            return 1.0;
        }
        double mag = Math.Pow(10, Math.Floor(Math.Log10(raw)));
        double r = raw / mag;
        return (r < 1.5 ? 1 : r < 3.5 ? 2 : r < 7.5 ? 5 : 10) * mag;
    }
}
