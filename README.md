# LightLaunchpad

轻量、优雅、美观的 Windows 启动台。

LightLaunchpad 用原生 Win32 绘制启动台和聚焦窗口，后台占用低，打开迅速，交互干净。

## 下载

在 [GitHub Releases](https://github.com/thomasthzh/LightLaunchpad/releases) 下载最新版 zip，解压后运行：

```text
LightLaunchpad.NativeUi.exe
```

## 特点

- 低内存后台：目标 10 MB，15 MB 内可接受，20 MB 为上限。
- 启动台 / 聚焦：支持全屏浏览和无边框玻璃聚焦窗口。
- 快速搜索：支持中文、英文、拼音和首字母。
- 键盘启动：方向键选择，回车启动，Tab 补全。
- 图标清晰：使用 Windows Shell 高分辨率图标并缓存。
- 整理顺手：支持 region、框选、多选、拖拽排序和绕行动画。
- 设置简洁：窗口大小、图标大小、间距、滚轮灵敏度、语言、热键。

## 构建

需要 Windows、.NET SDK 8、MinGW-w64 `g++` / `windres`。

```powershell
dotnet restore LightLaunchpad.sln
dotnet build LightLaunchpad.sln -c Release
dotnet run --project tests\LightLaunchpad.Core.Tests\LightLaunchpad.Core.Tests.csproj
dotnet run --project tests\LightLaunchpad.App.Tests\LightLaunchpad.App.Tests.csproj
.\tools\package-release.ps1
```

发布包输出到：

```text
release\LightLaunchpad-nativeui-win-x64-<version>.zip
```

## 数据位置

```text
%AppData%\LightLaunchpad
Documents\LightLaunchpad\icons
```

## License

MIT
