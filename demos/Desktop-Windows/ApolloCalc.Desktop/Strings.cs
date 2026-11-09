// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Kalyn-Voris (ApolloCalc-CAS). See LICENSE at the repository root.

using System.Globalization;

namespace ApolloCalc.Desktop;

/// <summary>
/// Demo 级 UI 文案字典：默认英文；系统语言为 zh-* 时自动显示中文。
/// 语言在进程启动时解析一次（切换语言 = 重启应用），--lang=en / --lang=zh
/// 可显式覆盖（见 Program.Main）—— 对 Demo 而言「跟随系统语言」最优雅，
/// 显式覆盖只是给测试与特殊偏好的一个小后门。
/// </summary>
internal static class Strings
{
    private static bool? _zhOverride;   // null = 跟随系统语言

    public static void SetOverride(string? lang) =>
        _zhOverride = lang?.Trim().ToLowerInvariant() switch
        {
            "zh" or "zh-cn" or "zh-hans" => true,
            "en" or "en-us" => false,
            _ => null,
        };

    // 任务约定：zh 系统显示中文，其余一律英文（zh-TW / zh-HK 同归中文）
    private static bool UseChinese =>
        _zhOverride
        ?? (CultureInfo.CurrentUICulture.Name.StartsWith("zh", StringComparison.OrdinalIgnoreCase)
            || CultureInfo.CurrentCulture.Name.StartsWith("zh", StringComparison.OrdinalIgnoreCase));

    private static readonly Dictionary<string, string> English = new()
    {
        ["Title"] = "ApolloCalc Desktop (C99 Engine × C# WinForms Demo)",
        ["Plot"] = "Plot",
        ["Radians"] = "Radians (RAD)",
        ["StatusEmpty"] = "Please enter an expression.",
        ["StatusInvalid"] = "Parse failed, or f has no valid points on [{0}, {1}]: {2}",
        ["StatusPlot"] = "{0}    ·    y ∈ [{1}, {2}]    ·    {3}/{4} valid samples",
        ["PaintHint"] = "Enter f(x), then click Plot",
        ["DllMissingTitle"] = "ApolloCalc Desktop",
        ["DllMissing"] = "ApolloCalc.dll was not found.\n" +
                         "Run c_bridge\\build.bat to build the bridge DLL first, then rebuild with dotnet build.",
        // 法务文本刻意不翻译：两语言逐字一致（与 HTML demo 页脚同源）
        ["License"] = "© 2026 Kalyn-Voris. Licensed under the MIT License.",
    };

    private static readonly Dictionary<string, string> Chinese = new()
    {
        ["Title"] = "ApolloCalc 桌面版（C99 内核 × C# WinForms 演示）",
        ["Plot"] = "绘图",
        ["Radians"] = "弧度制 (RAD)",
        ["StatusEmpty"] = "请输入表达式。",
        ["StatusInvalid"] = "解析失败，或 f 在 [{0}, {1}] 上无有效点：{2}",
        ["StatusPlot"] = "{0}    ·    y ∈ [{1}, {2}]    ·    {3}/{4} 个采样点有效",
        ["PaintHint"] = "输入 f(x) 后点击绘图",
        ["DllMissingTitle"] = "ApolloCalc 桌面版",
        ["DllMissing"] = "找不到 ApolloCalc.dll。\n" +
                         "请先运行 c_bridge\\build.bat 编译桥接 DLL，再重新 dotnet build。",
        ["License"] = "© 2026 Kalyn-Voris. Licensed under the MIT License.",
    };

    /// <summary>取当前语言的文案；缺键时回退英文（两表应同键同步维护）。</summary>
    public static string T(string key)
    {
        var dict = UseChinese ? Chinese : English;
        return dict.TryGetValue(key, out var s) ? s : English[key];
    }
}
