# LightLaunchpad

LightLaunchpad 是一个面向 Windows 的轻量启动台。它把 macOS「启动台」和「聚焦」的核心体验迁移到 Windows：快速呼出、搜索应用、键盘选择、鼠标拖拽整理，以及低后台内存占用。

当前项目已经进入原生 Native UI 重构路线。推荐日常使用 `LightLaunchpad.NativeUi.exe`，它以 Win32 原生窗口和本地绘制为主，目标是更低内存、更快响应、更稳定的图标渲染；`LightLaunchpad.App.exe` 仍保留为 WPF 备用界面和设置/迁移补充。

## 下载

请在 [GitHub Releases](https://github.com/thomasthzh/LightLaunchpad/releases) 下载最新版 Windows 压缩包。当前推荐版本为 `V1.5`：

- 解压发布包。
- 运行 `LightLaunchpad.NativeUi.exe` 启动原生启动台。
- 如需使用旧版 WPF 设置或备用界面，可运行 `LightLaunchpad.App.exe`。

发布包会包含原生 UI 所需的运行文件，并静态链接 MinGW C/C++ 运行时，不需要用户额外安装开发工具链。

## 核心特点

### 轻量后台

- 原生 Win32 UI 路线，减少 WPF 常驻带来的内存压力。
- 常驻后台以热键、托盘、配置和图标索引为主，避免保留沉重的界面状态。
- 当前优化目标：10 MB 为理想值，15 MB 可接受，20 MB 为上限。
- 最近版本的本地探测中，原生界面常见工作集约 11-12 MB，私有内存低于 2 MB。

### 启动台与聚焦

- 支持「启动台」和「聚焦」两种显示方式。
- 启动台适合全局浏览和整理应用。
- 聚焦模式使用无边框悬浮窗口，打开后自动聚焦搜索框。
- 聚焦窗口支持玻璃化背景、柔和边缘和圆角观感。
- 点击聚焦窗口外部可快速关闭。
- 聚焦窗口大小、应用大小、应用间距和滚轮灵敏度可以在设置中调整。

### 搜索与键盘操作

- 打开后默认进入搜索输入，适合直接键盘启动应用。
- 支持更好的排名搜索，搜索结果会根据名称和匹配位置排序。
- 支持 `Tab` 补全搜索项。
- 支持方向键在应用图标之间移动选择。
- 支持回车启动当前选中应用。
- 清空搜索后会恢复完整应用列表，避免搜索状态残留。

### 图标显示

- 使用 Windows Shell 图标能力提取应用图标。
- 支持高分辨率图标缓存，减少模糊和锯齿。
- 图标会按格子大小自动适配，避免不同来源图标大小不一致。
- 对透明边距较大的图标进行更合理的视觉填充，让应用在窗格中更统一。
- 图标缓存默认保存在 `Documents\LightLaunchpad\icons`。

### 应用整理

- 支持导入 `.lnk`、`.url`、`.exe` 等可启动项目。
- 支持导入 Windows 开始菜单应用。
- 支持单击选择、双击启动。
- 支持框选多个图标，框选样式保持轻量，不使用强烈反色。
- 支持批量拖拽已选图标。
- 支持拖拽排序，并在拖动时显示插入预览。
- 拖拽图标时周边图标会进行预览避让，减少突兀跳动。
- 支持右键菜单：启动、重命名、打开所在位置、删除。
- 删除图标会从启动台数据中移除，不再自动回到「未分类」。

### Region 分组

- 支持创建、重命名、删除 region。
- 支持把应用移动到不同 region。
- 支持批量修改 region。
- 支持拖拽 region 调整顺序。
- 当所有图标都有分类时，会自动隐藏不必要的「未分类」区域。
- 可在内联区域视图和标签式区域视图之间切换。

### 设置能力

- 支持中英文界面切换。
- 支持选择显示方式：启动台或聚焦。
- 支持设置聚焦窗口大小。
- 支持设置应用图标大小。
- 支持设置应用间距。
- 支持设置鼠标滚轮灵敏度。
- 支持设置全局热键。
- 支持设置是否随系统启动。

## 使用方式

1. 下载并解压 GitHub Release 中的 Windows zip 包。
2. 运行 `LightLaunchpad.NativeUi.exe`。
3. 使用默认热键呼出启动台或聚焦窗口。
4. 直接输入应用名称进行搜索。
5. 使用方向键选择应用，按回车启动。
6. 使用鼠标拖拽图标或 region 调整布局。
7. 通过托盘或设置界面调整语言、显示模式、图标大小、间距、滚轮灵敏度等选项。

用户数据默认保存在：

- 配置和布局：`%AppData%\LightLaunchpad`
- 图标缓存：`Documents\LightLaunchpad\icons`

## 项目结构

- `src\LightLaunchpad.NativeUi`：原生 Win32 启动台界面，负责低内存、高流畅度的主要体验。
- `src\LightLaunchpad.App`：WPF 备用界面和设置补充。
- `src\LightLaunchpad.Core`：配置、应用数据、图标缓存、导入、排序等核心逻辑。
- `tests`：核心逻辑和应用层测试。
- `tools`：构建、打包和发布辅助脚本。
- `release`：本地生成的发布包目录。

## 构建

构建环境：

- Windows
- .NET SDK 8
- MinGW-w64 `g++`，用于构建原生 UI
- GitHub CLI，可用于发布和推送流程

还原和构建：

```powershell
dotnet restore LightLaunchpad.sln
dotnet build LightLaunchpad.sln -c Release
```

运行测试：

```powershell
dotnet run --project tests\LightLaunchpad.Core.Tests\LightLaunchpad.Core.Tests.csproj
dotnet run --project tests\LightLaunchpad.App.Tests\LightLaunchpad.App.Tests.csproj
```

构建 WPF 备用界面：

```powershell
dotnet publish src\LightLaunchpad.App\LightLaunchpad.App.csproj `
  -c Release `
  -r win-x64 `
  --self-contained true `
  -p:PublishSingleFile=true `
  -p:EnableCompressionInSingleFile=true `
  -p:IncludeNativeLibrariesForSelfExtract=true `
  -o artifacts\publish-win-x64
```

构建原生 UI：

```powershell
.\tools\build-native-ui.ps1 -OutputDirectory release\native-ui-probe
```

生成本地发布包：

```powershell
.\tools\package-release.ps1
```

发布包会写入：

```text
release\LightLaunchpad-nativeui-win-x64-<version>
release\LightLaunchpad-nativeui-win-x64-<version>.zip
```

## 优化方向

LightLaunchpad 的长期方向是继续减少后台成本，并把主要交互迁移到原生 UI：

- 继续压缩常驻进程内存，减少 WPF 依赖。
- 优化图标缓存和图标解码路径，提高高清图标稳定性。
- 改进搜索索引，让应用数量增多后仍保持即时响应。
- 完善拖拽、框选、region 管理和键盘操作。
- 将设置界面逐步迁移到更轻量的实现。
- 保持发布包开箱即用，不依赖用户安装开发运行时。

## 许可证

MIT
