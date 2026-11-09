// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Kalyn-Voris (ApolloCalc-CAS). See LICENSE at the repository root.

namespace ApolloCalc.Desktop;

internal static class Program
{
    [STAThread]
    private static void Main(string[] args)
    {
        // 可选语言覆盖：--lang=en / --lang=zh；缺省跟随系统语言（zh-* → 中文）
        foreach (var a in args)
        {
            if (a.StartsWith("--lang=", StringComparison.OrdinalIgnoreCase))
            {
                Strings.SetOverride(a["--lang=".Length..]);
            }
        }
        ApplicationConfiguration.Initialize();
        Application.Run(new MainForm());
    }
}
