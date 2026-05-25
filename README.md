# LightLaunchpad

轻量、优雅、美观的 Windows 启动台。

LightLaunchpad 现在以 `LightLaunchpad.NativeUi.exe` 为主版本。它走原生 Win32 / Direct2D / DirectWrite 路线，不依赖常驻 WPF 外壳，后台保持轻，热键唤起快，聚焦窗口使用自绘玻璃而不是系统 Acrylic 的矩形层。

## 亮点

- 低后台占用：隐藏时释放图标、渲染缓冲和 Direct2D 资源。
- 液态玻璃聚焦：圆角由玻璃本身承担，边缘柔化，背景透明可见。
- 内容不透明：玻璃保持通透，图标和文字保持清晰、不发灰、不重影。
- 快速搜索：支持中文、英文、拼音、首字母、Tab 补全和方向键选择。
- 启动台整理：支持 region、框选、多选、拖拽排序和稳定落点。
- 原生设置：窗口大小、APP 大小、间距、滚轮灵敏度、语言和热键。

## 使用

下载或解压发布包后运行：

```text
LightLaunchpad.NativeUi.exe
```

默认热键为 `Alt+D`。托盘图标可打开启动台、刷新、设置和退出。

## 数据位置

```text
%AppData%\LightLaunchpad
Documents\LightLaunchpad\icons
```

## 构建

需要 Windows、.NET SDK 8、MinGW-w64 `g++` / `windres`。

```powershell
dotnet restore LightLaunchpad.sln
dotnet run --project tests\LightLaunchpad.Core.Tests\LightLaunchpad.Core.Tests.csproj
dotnet run --project tests\LightLaunchpad.App.Tests\LightLaunchpad.App.Tests.csproj
.\tools\build-native-ui.ps1 -OutputDirectory release\native-ui-devcheck
.\tools\package-release.ps1
```

## License

MIT
