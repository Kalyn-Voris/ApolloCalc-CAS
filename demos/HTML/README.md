# ApolloCalc · 单文件图形计算器 Demo

双击 `index.html` 即可在浏览器中运行；默认英文界面，右上角 `[EN | 中文]` 一键切换中英双语，Apollo 引擎已全部内联、离线可用，回车重绘，输入行右侧下拉切换 RAD/DEG 角度制（默认 RAD 弧度制，切换即时重绘）。

Y 轴量程按当前曲线的有效数据点自适应（上下各留 10% 余量，常数函数固定 ±1 边距），因此 DEG 下的 `sin(x)` 或 `y=5` 这类微小/平坦曲线也能清晰显示；X 轴固定为 `[-10, 10]`，水平与竖直网格按 1/2/5 系列自动选取刻度步长，状态栏实时显示真实视野。

沙箱测试：`node demos/HTML/test-angle-mode.mjs`（角度制）、`node demos/HTML/test-auto-scale.mjs`（自适应量程与网格）。

想要本地原生版？试试[桌面版 Demo](../Desktop-Windows/README.md)。*注：Windows 用户首次运行桌面版若遇 SmartScreen 拦截，请点击"更多信息"→"仍要运行"——本软件为无签名的开源软件。*
