# LightLaunchpad 项目结构

本文说明当前仓库中每个主要目录和文件的用途。项目主线已经切到原生 Win32 版本，发布主程序为 `LightLaunchpad.NativeUi.exe`。

## 根目录

| 路径 | 作用 |
| --- | --- |
| `.gitignore` | Git 忽略规则，排除构建输出、本地缓存和临时文件。 |
| `global.json` | 固定 .NET SDK 版本选择。 |
| `NuGet.config` | NuGet 包源配置。 |
| `LightLaunchpad.sln` | Visual Studio / dotnet 解决方案入口。 |
| `README.md` | GitHub 首页说明，面向用户介绍轻量启动台能力。 |
| `PROJECT_STRUCTURE.md` | 当前文件，说明目录和文件职责。 |
| `LICENSE` | MIT 许可证。 |

## 生成目录

| 路径 | 作用 |
| --- | --- |
| `.dotnet/` | 本地 .NET 工具链缓存。 |
| `.superpowers/` | 本地开发辅助记录，不属于产品运行路径。 |
| `artifacts/` | 历史构建输出目录，本轮已清理旧版本；后续仅作临时构建输出。 |
| `release/` | 本地 release 输出目录；最终包由 `tools/package-release.ps1` 生成。 |

## 文档目录

| 路径 | 作用 |
| --- | --- |
| `docs/ai/requirements-and-route.md` | 需求和技术路线记录。 |
| `docs/ai/native-ui-optimization-refactor.md` | 原生 UI 与性能优化路线记录。 |
| `docs/ai/project-standards.md` | 项目开发约定。 |
| `docs/ai/tasks.md` | 任务拆解。 |
| `docs/ai/progress.md` | 阶段进度记录。 |
| `docs/superpowers/specs/*.md` | 早期功能设计规格。 |
| `docs/superpowers/plans/*.md` | 早期实现计划。 |

## 主程序源码

| 路径 | 作用 |
| --- | --- |
| `src/LightLaunchpad.NativeUi/LightLaunchpad.NativeUi.cpp` | 当前主程序。原生 Win32 窗口、热键、托盘、设置、搜索、布局、图标加载、聚焦玻璃渲染和交互都在这里。 |
| `src/LightLaunchpad.NativeUi/LightLaunchpad.NativeUi.rc` | NativeUi 资源脚本，把项目图标嵌入 exe。 |

## Core 共享逻辑

| 路径 | 作用 |
| --- | --- |
| `src/LightLaunchpad.Core/LightLaunchpad.Core.csproj` | Core 类库项目文件。 |
| `src/LightLaunchpad.Core/Activation/LaunchpadActivationContext.cs` | 启动参数和激活上下文。 |
| `src/LightLaunchpad.Core/Hotkeys/HotkeyGesture.cs` | 热键字符串解析与标准化。 |
| `src/LightLaunchpad.Core/Import/StartMenuImportCandidate.cs` | 开始菜单导入候选模型。 |
| `src/LightLaunchpad.Core/Import/StartMenuImporter.cs` | 扫描 Windows 开始菜单应用。 |
| `src/LightLaunchpad.Core/Import/VuiImportCandidate.cs` | 旧 `.vui` 导入候选模型，保留给兼容测试。 |
| `src/LightLaunchpad.Core/Import/VuiImportParser.cs` | 旧 `.vui` 解析器，非当前 UI 暴露功能。 |
| `src/LightLaunchpad.Core/Layout/DragInsertionCalculator.cs` | 拖拽插入位置计算。 |
| `src/LightLaunchpad.Core/Layout/DragTargetSmoother.cs` | 拖拽目标稳定器，降低判定抖动。 |
| `src/LightLaunchpad.Core/Layout/LaunchpadLayout.cs` | 布局根模型。 |
| `src/LightLaunchpad.Core/Layout/LaunchpadLayoutItem.cs` | 布局中的单个应用项。 |
| `src/LightLaunchpad.Core/Layout/LaunchpadRegion.cs` | region 分组模型。 |
| `src/LightLaunchpad.Core/Layout/LaunchpadViewMode.cs` | region 显示模式。 |
| `src/LightLaunchpad.Core/Layout/LayoutService.cs` | 布局读写、合并和迁移。 |
| `src/LightLaunchpad.Core/Search/LaunchItemSearch.cs` | Core 搜索排序逻辑。 |
| `src/LightLaunchpad.Core/Settings/AppLanguages.cs` | 语言常量。 |
| `src/LightLaunchpad.Core/Settings/AppSettingLimits.cs` | 设置值范围。 |
| `src/LightLaunchpad.Core/Settings/AppSettings.cs` | 设置模型。 |
| `src/LightLaunchpad.Core/Settings/AppStoragePaths.cs` | 配置、布局、图标缓存路径。 |
| `src/LightLaunchpad.Core/Settings/LaunchpadDisplayModes.cs` | 启动台 / 聚焦显示模式常量。 |
| `src/LightLaunchpad.Core/Settings/SettingsService.cs` | 设置文件读写。 |
| `src/LightLaunchpad.Core/Shortcuts/InternetShortcutFile.cs` | `.url` 快捷方式解析。 |
| `src/LightLaunchpad.Core/Shortcuts/LaunchFileTypes.cs` | 可启动文件类型判断。 |
| `src/LightLaunchpad.Core/Shortcuts/LaunchItem.cs` | 应用项模型。 |
| `src/LightLaunchpad.Core/Shortcuts/ShortcutRepository.cs` | 启动台文件夹扫描与快捷方式存储。 |

## 旧 WPF 与实验源码

这些文件仍在仓库中用于测试、兼容和参考，但不再作为发布主线。

| 路径 | 作用 |
| --- | --- |
| `src/LightLaunchpad.App/LightLaunchpad.App.csproj` | 旧 WPF 应用项目文件。 |
| `src/LightLaunchpad.App/App.xaml` / `App.xaml.cs` | WPF 应用入口。 |
| `src/LightLaunchpad.App/LaunchpadWindow.xaml` / `.cs` | 旧启动台窗口。 |
| `src/LightLaunchpad.App/SettingsWindow.xaml` / `.cs` | 旧设置窗口。 |
| `src/LightLaunchpad.App/TextInputDialog.xaml` / `.cs` | 旧文本输入对话框。 |
| `src/LightLaunchpad.App/AssemblyInfo.cs` | WPF 程序集属性。 |
| `src/LightLaunchpad.App/HotkeySinkWindow.cs` | 旧热键消息窗口。 |
| `src/LightLaunchpad.App/LaunchpadWindowPlacement.cs` | 旧窗口定位计算。 |
| `src/LightLaunchpad.App/UiText.cs` | 旧 UI 文案。 |
| `src/LightLaunchpad.App/Assets/LightLaunchpad.ico` | 项目 ico，NativeUi 构建和运行都会使用。 |
| `src/LightLaunchpad.App/Assets/LightLaunchpad.svg` | 项目矢量图标源。 |
| `src/LightLaunchpad.App/Assets/LightLaunchpad-256.png` | 256px 图标位图。 |
| `src/LightLaunchpad.App/Services/*.cs` | 旧 WPF 服务层，包括图标、热键、托盘、导入、滚轮、拖拽和启动服务。 |
| `src/LightLaunchpad.App/ViewModels/*.cs` | 旧 WPF MVVM 视图模型。 |
| `src/LightLaunchpad.Agent/LightLaunchpad.Agent.csproj` / `Program.cs` | 早期 agent 路线实验，当前不再推荐。 |
| `src/LightLaunchpad.NativeAgent/LightLaunchpad.NativeAgent.cpp` | 早期 native agent 实验，当前不再推荐。 |

## 测试

| 路径 | 作用 |
| --- | --- |
| `tests/LightLaunchpad.Core.Tests/` | Core 单元测试。覆盖设置、搜索、布局、导入、快捷方式和拖拽计算。 |
| `tests/LightLaunchpad.App.Tests/` | App/NativeUi 源码结构测试和旧 WPF 行为测试。覆盖 NativeUi 渲染路线、发布脚本、窗口定位、图标稳定性和 UI 功能约束。 |
| `tests/*/Program.cs` | 简易测试运行器入口。 |
| `tests/*/TestAssert.cs` | 测试断言工具。 |
| `tests/LightLaunchpad.Core.Tests/TestPaths.cs` | 测试路径辅助。 |

## 工具脚本

| 路径 | 作用 |
| --- | --- |
| `tools/build-native-ui.ps1` | 使用 MinGW-w64 `g++` / `windres` 构建 `LightLaunchpad.NativeUi.exe`。 |
| `tools/package-release.ps1` | 生成当前 NativeUi 发布包。 |
| `tools/build-native-agent.ps1` | 早期 native agent 构建脚本，当前仅保留为历史参考。 |

