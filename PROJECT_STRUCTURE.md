# LightLaunchpad 项目结构与开发路径

本文是当前仓库的主说明文档。旧的 AI 计划、Superpowers 设计草稿和早期路线文档已经清理；当前主线以 `LightLaunchpad.NativeUi.exe` 为唯一发布版本。

## 当前主线

LightLaunchpad 现在是一个轻量原生 Windows 启动台：

- 主程序：`src/LightLaunchpad.NativeUi/LightLaunchpad.NativeUi.cpp`
- 发布物：`LightLaunchpad.NativeUi.exe`
- 发布包：`release/LightLaunchpad-nativeui-win-x64-<version>.zip`
- 旧 WPF、Agent、NativeAgent 代码保留为历史参考和测试约束，不再作为主发布路径。

## 根目录

| 路径 | 作用 |
| --- | --- |
| `README.md` | GitHub 首页，面向用户说明产品定位、下载、特点和构建方式。 |
| `PROJECT_STRUCTURE.md` | 当前文件，面向开发者说明目录、代码职责和开发路径。 |
| `LightLaunchpad.sln` | .NET 解决方案入口，包含 Core、旧 WPF App、测试项目等。 |
| `global.json` | 固定 .NET SDK 选择。 |
| `NuGet.config` | NuGet 包源配置。 |
| `LICENSE` | MIT 许可证。 |
| `.gitignore` | 排除构建输出、缓存、临时文件和本地 release 产物。 |
| `src/` | 产品源码。 |
| `tests/` | Core、App、NativeUi 源码约束测试。 |
| `tools/` | NativeUi 构建与发布脚本。 |
| `release/` | 本地构建和发布输出目录，不作为源码主线。 |

## 主程序代码

| 文件 | 职责 |
| --- | --- |
| `src/LightLaunchpad.NativeUi/LightLaunchpad.NativeUi.cpp` | 当前主程序。一个原生 Win32 进程内完成托盘、全局热键、启动台/聚焦窗口、设置、搜索、拼音匹配、Tab 补全、方向键启动、region、框选、多选、拖拽排序、图标加载、缓存裁剪、聚焦液态玻璃渲染、透明圆角、打开/关闭动画和内存修剪。 |
| `src/LightLaunchpad.NativeUi/LightLaunchpad.NativeUi.rc` | NativeUi 资源脚本。把 `LightLaunchpad.ico` 嵌入 exe，保证任务栏、文件属性和托盘图标一致。 |

## Core 共享逻辑

这些文件仍是可测试的业务逻辑核心；NativeUi 目前复用了其中的数据格式和部分算法路线。

| 文件 | 职责 |
| --- | --- |
| `src/LightLaunchpad.Core/LightLaunchpad.Core.csproj` | Core 类库项目文件。 |
| `src/LightLaunchpad.Core/Activation/LaunchpadActivationContext.cs` | 旧 hosted UI 激活参数解析与生成，保留给兼容测试。 |
| `src/LightLaunchpad.Core/Hotkeys/HotkeyGesture.cs` | 热键字符串解析、标准化和显示。 |
| `src/LightLaunchpad.Core/Import/StartMenuImportCandidate.cs` | 开始菜单导入候选数据模型。 |
| `src/LightLaunchpad.Core/Import/StartMenuImporter.cs` | 扫描用户和系统开始菜单目录，发现 `.lnk`、`.url` 等启动项。 |
| `src/LightLaunchpad.Core/Import/VuiImportCandidate.cs` | 旧 `.vui` 导入候选模型，仅保留兼容和测试。 |
| `src/LightLaunchpad.Core/Import/VuiImportParser.cs` | 旧 `.vui` 解析器。当前产品 UI 不再暴露 `.vui` 导入。 |
| `src/LightLaunchpad.Core/Layout/DragInsertionCalculator.cs` | 拖拽插入位置计算，避免落点过敏和行间跳动。 |
| `src/LightLaunchpad.Core/Layout/DragTargetSmoother.cs` | 拖拽目标稳定器，让落点在短时间内稳定后再接受。 |
| `src/LightLaunchpad.Core/Layout/LaunchpadLayout.cs` | 布局根模型，包含 region 和项目列表。 |
| `src/LightLaunchpad.Core/Layout/LaunchpadLayoutItem.cs` | 单个布局项，记录来源路径、显示名、region、排序等。 |
| `src/LightLaunchpad.Core/Layout/LaunchpadRegion.cs` | region 分组模型。 |
| `src/LightLaunchpad.Core/Layout/LaunchpadViewMode.cs` | region 显示模式枚举。 |
| `src/LightLaunchpad.Core/Layout/LayoutService.cs` | 布局文件读写、默认布局创建、增量合并和 region 删除迁移。 |
| `src/LightLaunchpad.Core/Search/LaunchItemSearch.cs` | Core 搜索排序逻辑，覆盖中文、英文、拼音和首字母。 |
| `src/LightLaunchpad.Core/Settings/AppLanguages.cs` | 中英文语言常量。 |
| `src/LightLaunchpad.Core/Settings/AppSettingLimits.cs` | 设置范围限制，例如窗口大小、间距、滚轮灵敏度。 |
| `src/LightLaunchpad.Core/Settings/AppSettings.cs` | 设置模型。 |
| `src/LightLaunchpad.Core/Settings/AppStoragePaths.cs` | 配置、布局、图标缓存路径。 |
| `src/LightLaunchpad.Core/Settings/LaunchpadDisplayModes.cs` | `Launchpad` / `Spotlight` 显示模式常量。 |
| `src/LightLaunchpad.Core/Settings/SettingsService.cs` | 设置文件读写、缺省值补全和范围归一化。 |
| `src/LightLaunchpad.Core/Shortcuts/InternetShortcutFile.cs` | `.url` 快捷方式解析，支持 Steam 等图标来源。 |
| `src/LightLaunchpad.Core/Shortcuts/LaunchFileTypes.cs` | 判断文件是否可作为启动项。 |
| `src/LightLaunchpad.Core/Shortcuts/LaunchItem.cs` | 启动项模型。 |
| `src/LightLaunchpad.Core/Shortcuts/ShortcutRepository.cs` | 扫描启动台文件夹、读写启动项、合并新项目。 |

## 旧 WPF 与实验源码

这些文件不是当前发布主线。保留它们是为了迁移参考、行为对照和测试约束；不要再把它们作为新功能入口。

| 文件 | 职责 |
| --- | --- |
| `src/LightLaunchpad.App/LightLaunchpad.App.csproj` | 旧 WPF 应用项目文件。 |
| `src/LightLaunchpad.App/App.xaml` | 旧 WPF 应用资源入口。 |
| `src/LightLaunchpad.App/App.xaml.cs` | 旧 WPF 应用启动逻辑、hosted/standalone 兼容入口。 |
| `src/LightLaunchpad.App/AssemblyInfo.cs` | 旧 WPF 程序集属性。 |
| `src/LightLaunchpad.App/HotkeySinkWindow.cs` | 旧热键消息窗口。 |
| `src/LightLaunchpad.App/LaunchpadWindow.xaml` | 旧启动台 XAML 窗口。 |
| `src/LightLaunchpad.App/LaunchpadWindow.xaml.cs` | 旧启动台交互、搜索、拖拽、region 和图标逻辑。 |
| `src/LightLaunchpad.App/LaunchpadWindowPlacement.cs` | 旧窗口定位计算。 |
| `src/LightLaunchpad.App/SettingsWindow.xaml` | 旧设置窗口 XAML。 |
| `src/LightLaunchpad.App/SettingsWindow.xaml.cs` | 旧设置窗口逻辑。 |
| `src/LightLaunchpad.App/TextInputDialog.xaml` | 旧文本输入对话框 XAML。 |
| `src/LightLaunchpad.App/TextInputDialog.xaml.cs` | 旧文本输入对话框逻辑。 |
| `src/LightLaunchpad.App/UiText.cs` | 旧 WPF 中英文文案辅助。 |
| `src/LightLaunchpad.App/Assets/LightLaunchpad.ico` | 当前仍使用的主图标，NativeUi 构建和 release 包会复制它。 |
| `src/LightLaunchpad.App/Assets/LightLaunchpad.svg` | 图标矢量源。 |
| `src/LightLaunchpad.App/Assets/LightLaunchpad-256.png` | 图标位图源。 |

## 旧 WPF Services

| 文件 | 职责 |
| --- | --- |
| `src/LightLaunchpad.App/Services/DragService.cs` | 旧 WPF 拖拽服务。 |
| `src/LightLaunchpad.App/Services/HostedActivationService.cs` | 旧 hosted UI 激活服务。 |
| `src/LightLaunchpad.App/Services/HotkeyService.cs` | 旧 WPF 全局热键服务。 |
| `src/LightLaunchpad.App/Services/IconCacheService.cs` | 旧图标缓存服务。 |
| `src/LightLaunchpad.App/Services/IconService.cs` | 旧 Shell 图标加载服务。 |
| `src/LightLaunchpad.App/Services/IconSourceResolver.cs` | 旧图标来源解析。 |
| `src/LightLaunchpad.App/Services/IconUpScaler.cs` | 旧图标放大处理。 |
| `src/LightLaunchpad.App/Services/InteractiveElementHitTest.cs` | 旧 WPF 交互元素命中测试。 |
| `src/LightLaunchpad.App/Services/LauncherService.cs` | 旧启动服务。 |
| `src/LightLaunchpad.App/Services/ShellShortcutService.cs` | 旧 Shell 快捷方式操作。 |
| `src/LightLaunchpad.App/Services/ShortcutWatcher.cs` | 旧快捷方式文件监听。 |
| `src/LightLaunchpad.App/Services/StartMenuImportService.cs` | 旧开始菜单导入服务。 |
| `src/LightLaunchpad.App/Services/TrayService.cs` | 旧托盘服务。 |
| `src/LightLaunchpad.App/Services/VuiImportService.cs` | 旧 `.vui` 导入服务，当前产品已移除入口。 |
| `src/LightLaunchpad.App/Services/WheelScrollCalculator.cs` | 旧滚轮灵敏度计算。 |

## 旧 WPF ViewModels

| 文件 | 职责 |
| --- | --- |
| `src/LightLaunchpad.App/ViewModels/LaunchItemViewModel.cs` | 旧 WPF 单个启动项视图模型。 |
| `src/LightLaunchpad.App/ViewModels/LaunchpadRegionViewModel.cs` | 旧 WPF region 视图模型。 |
| `src/LightLaunchpad.App/ViewModels/LaunchpadViewModel.cs` | 旧 WPF 主视图模型。 |

## Agent 实验源码

| 文件 | 职责 |
| --- | --- |
| `src/LightLaunchpad.Agent/LightLaunchpad.Agent.csproj` | 早期 .NET agent 项目文件。 |
| `src/LightLaunchpad.Agent/Program.cs` | 早期 agent 路线，用于启动 hosted UI。当前不推荐继续维护。 |
| `src/LightLaunchpad.NativeAgent/LightLaunchpad.NativeAgent.cpp` | 早期 native agent 路线。当前不推荐继续维护。 |

## 测试代码

| 目录或文件 | 职责 |
| --- | --- |
| `tests/LightLaunchpad.Core.Tests/` | Core 单元测试，覆盖设置、搜索、布局、导入、快捷方式、拖拽计算。 |
| `tests/LightLaunchpad.App.Tests/` | 旧 WPF 行为测试、NativeUi 源码结构测试、构建和发布脚本约束。 |
| `tests/LightLaunchpad.Core.Tests/Program.cs` | Core 测试运行器。 |
| `tests/LightLaunchpad.App.Tests/Program.cs` | App/NativeUi 测试运行器。 |
| `tests/*/TestAssert.cs` | 简易断言工具。 |
| `tests/LightLaunchpad.Core.Tests/TestPaths.cs` | 测试路径辅助。 |

## 工具脚本

| 文件 | 职责 |
| --- | --- |
| `tools/build-native-ui.ps1` | 用 MinGW-w64 `g++` 和 `windres` 构建 `LightLaunchpad.NativeUi.exe`，静态链接运行时，避免 `libwinpthread-1.dll` 缺失。 |
| `tools/package-release.ps1` | 构建 NativeUi 发布目录和 zip，只包含主程序和必要资源，不再打包 `LightLaunchpad.App.exe`。 |
| `tools/build-native-agent.ps1` | 早期 native agent 构建脚本，仅保留历史参考。 |

## 开发路径

1. 需求先落到 NativeUi 主线。除非明确要求，否则不新增 WPF 或 agent 路线。
2. 先阅读 `LightLaunchpad.NativeUi.cpp` 中相关区域，确认现有模式，再做小范围修改。
3. UI/交互问题先做可回退分支实验，尤其是玻璃、圆角、动画、拖拽、搜索等视觉逻辑。
4. 对复杂 bug 采用分层剥离：一次只关一层背景、边界、系统材质或绘制层，让用户判断真实视觉差异。
5. 可复用算法优先放在 Core；但热路径、渲染路径和低内存路径优先保持 NativeUi 单进程、低依赖。
6. 每次用户可见变化后构建 NativeUi，并启动本地 exe 做人工视觉确认。
7. 最后运行 Core 测试、App 测试、NativeUi 构建、打包和 `git diff --check`。

## Debug 路径

1. 不在视觉问题上连续猜测。先复现、隔离、编号每一层。
2. 对聚焦圆角问题，当前结论是：系统 Acrylic / BlurBehind 会在角落引入错误矩形材质；DWM 圆角与当前 layered 路线也会复发。最终路线是系统 Acrylic 禁用、自绘玻璃、圆角外 color-key 透明。
3. `SetWindowRgn(region)` 可以裁剪外形，但边缘锯齿明显；不要作为聚焦最终外边界。
4. 如果用户明确说“由我判断”，每轮只改一层、启动软件、等待反馈。
5. 测试负责锁住路线，视觉负责最终判定。

## 重构路径

1. 不做“大而全”重构。先找热点：内存、图标、搜索、拖拽、玻璃渲染。
2. 文件过大时，优先抽出稳定算法或数据模型；不要把 Win32 消息、绘制状态拆得过早。
3. WPF/Agent 代码只作参考，不把旧路线重新拉回主产品。
4. 低内存优先级高于架构漂亮：减少后台进程、延迟加载图标、隐藏时修剪资源。

## 发布路径

1. 在功能分支提交。
2. 用户要求直接合并时，切回 `main`，更新 `origin/main`，合并功能分支。
3. 运行完整验证。
4. 执行 `tools/package-release.ps1` 生成 zip。
5. 用 GitHub CLI 推送 `main`，并创建 GitHub release，附上 NativeUi zip。
