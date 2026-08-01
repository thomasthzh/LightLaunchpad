# New Session `/goal` Prompt

Paste the content below into a new Codex session opened at `C:\Users\thoma\Desktop\启动台`.

```text
/goal

在当前工作区 C:\Users\thoma\Desktop\启动台 中，持续完成 LightLaunchpad 的 Apple-first 产品升级，直到 PRD v1.1 与任务清单定义的功能、可靠性、交互、视觉、性能、测试和发布门禁全部真实达成。

开始前必须依次阅读：
1. PRD.md
2. docs/ai/requirements-and-route.md
3. docs/ai/tasks.md
4. docs/ai/project-standards.md
5. docs/ai/progress.md

建议主动使用这些技能：LLP_skill、task-driven-project-execution、systematic-debugging、test-driven-development、verification-before-completion；涉及视觉验收时使用 product-design:audit 与 frontend-testing-debugging，涉及任务并行时使用 subagent-driven-development。以任务文档为准，不依赖旧会话记忆。

产品目标：把 LightLaunchpad 做成能够争夺 Windows 开始菜单用户的原生、轻量、键盘优先启动器。默认高频路径必须是 Alt+D -> 输入 -> Enter；需要挑选时是 Alt+D -> 输入 -> Down -> 方向键 -> Enter。Spotlight 只负责搜索、结果导航和启动；Launchpad 浏览态单击启动，明确进入编辑模式后才支持选择、region、框选、多选和拖拽。

执行要求：
- 先核对 git 状态、当前分支/HEAD、运行中 EXE 的绝对路径与哈希、开机启动路径；不要假设目录名代表版本。
- 保护所有已有未提交内容。不要删除或擅自纳入 LLP_reconstruction/ 与 LightLaunchpad.NativeUi.o，不要还原用户改动。
- 使用 codex/ 前缀的开发分支，先执行 LLP-001；任何时刻只允许一个任务处于 in_progress。
- 逐项执行 docs/ai/tasks.md 的 LLP-001 到 LLP-017。每完成一个任务，立即更新任务状态和 docs/ai/progress.md，记录改动文件、命令、测试结果、性能/截图证据、提交和下一任务。
- 每个任务必须留下可运行的真实 NativeUi EXE。优先纵向交付，不做一次性大重写，不把只拆文件但继续共享 extern 全局状态当成架构完成。
- 行为改动尽可能测试先行。生产 C++ 行为和真实 EXE 才是发布依据；旧 C# Core 和源码字符串测试只能作为历史参考，不能用它们证明托盘、热键、IME、DPI、动画或液态玻璃已正确。
- 先解决版本身份、稳定安装、托盘/热键启动顺序、单实例 IPC、失败反馈和数据安全；再完成后台 Catalog/Shell STA、完整搜索输入、Spotlight/Launchpad 双模式；最后进入 DPI/无障碍、DirectComposition 自适应材质和非阻塞动画。
- 苹果设计哲学的含义是清晰、直接、连续、自适应、克制和可恢复，不是固定蓝白透明度。只使用一个连续主玻璃层，禁止 glass-on-glass、1.16 倍桌面截图放大、搜索栏向上越界采样和 UI 线程 Sleep 动画。
- 动画是正式功能：打开 140-180ms、关闭 120-160ms、焦点 80-120ms、按压 60-90ms；动画首帧后立即接受输入；60Hz 帧时间 P95 <=16.7ms，焦点反馈 P95 <=50ms；减少动态效果时关闭缩放、弹簧、视差和动态折射。
- 每次声称修复前都要运行相应测试、Native 构建和真实 EXE 流程。视觉任务必须在至少 12 种背景、100%/150%/200% DPI、高对比、减少透明和减少动态效果下进行匹配状态截图与像素检查。
- 不修改 Windows 私有 NotifyIconSettings\\IsPromoted。删除必须进入回收站并支持撤销。设置写入、热键切换、Shell 启动和文件操作失败都必须保留旧状态并给出可行动反馈。
- 普通开发期间不要改写用户开机启动项，不要覆盖 %LocalAppData%\Programs\LightLaunchpad 或 C:\TOOL，不要 push、tag 或 release。只有用户明确下达交付命令，并且全部发布门禁通过后，才执行安装、覆盖、推送和 Release。

完成定义：
- docs/ai/tasks.md 中 LLP-001 至 LLP-017 全部为 done，并有客观证据。
- PRD 的 Spotlight、Launchpad、输入、最近/收藏、设置、删除撤销、托盘、热键、DPI、无障碍、材质和动画均由真实 EXE 验证。
- 生产 C++ 单元/集成测试、真实 EXE 冒烟、视觉回归、性能预算和打包检查全部通过。
- 正式 EXE 具有可验证版本/构建身份，稳定安装路径和发布包一致，不包含 WPF、Agent、NativeAgent、Assist 或外置 Assets 依赖。
- 最终更新 docs/ai/progress.md，列出最终提交、测试、性能、截图、包哈希、残余风险和是否已获得用户发布授权。

现在先读取上述文档和实际仓库状态，创建 goal，报告你识别到的当前任务与验证基线，然后直接开始 LLP-001；除非出现破坏性操作、凭据需求或真实产品决策冲突，不要停在计划阶段。
```
