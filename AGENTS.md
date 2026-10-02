# Toy3d 协作规范

## 先读什么

- 对话、评审和交付使用中文，标识符、命令及必要术语保留原文。
- 从 `document/index.md` 按功能选择文档，再核对公共接口、调用方和测试；只读任务涉及章节，不默认遍历全部文档。
- 日常开发不需要读取 OpenSpec；只有用户明确选择该工作流时才使用对应 skills。旧 proposal/spec/task 和施工记录不构成当前规范。
- 全仓规则以本文件为准，模块规则以功能文档为准；接口与测试说明实际实现。发现冲突报告证据，不能把未实现设计当成 API，也不能把实现缺陷改写成合理规范。
- 文档只沉淀职责、边界、稳定行为、真实用例和验证入口；一个主题一个主入口，不保留重复总体方案、接入台账、验证流水账或会话记忆。历史从 Git 追溯。
- 新增通用能力或改变所有权、并发、错误、持久化格式等核心 contract 时，先在主文档形成简短方案并确认，再实现；普通局部修复无需提案目录。方案覆盖用例/非目标、目录/target、接口分层、生命周期、线程、错误、平台/安全、验证及迁移删除条件。

## 目录与边界

| 目录 | 职责 |
| --- | --- |
| `engine/core/` | runtime/editor/tools 共享的第一方基础设施及资产 DTO |
| `engine/runtime/` | config、platform、input、gamescene、rendercore、renderscene、drivers |
| `engine/runtime/drivers/rhi/` | 公共 RHI；各 API 后端独立目录 |
| `engine/editor/` | Editor 入口、面板、资产编辑器和界面 resources |
| `engine/tools/` | 离线 compiler、codegen、import/build 工具 |
| `engine/shader/`、`engine/asset/`、`engine/config/` | 引擎 shader、内置资产、默认配置 |
| `engine/build/` | 受版本管理的平台部署输入和 plist 模板 |
| `project/` | 验证工程 .toy、源码侧 asset/config/shader、src 原生模块与项目测试 |
| `document/` | 按功能维护的知识与规范 |
| `.codex/skills/` | 仓库级任务方法，不复制模块知识 |
| 根 `build/`、`bin/` | 构建与部署产物，不提交 |

- runtime 代码放职责最近的模块，不建笼统的 runtime/core。工具不得依赖 runtime/editor 来复用能力；共享实现放 core，业务策略留调用方。
- 新增文件、日志、进程、时间、任务、序列化、缓存、哈希、ID、分配器或容器前先搜索现有实现。通用能力归入 Core 中职责最近的功能目录，不再封装语义重复的系统，也不按每个类或小功能新建 target。
- 目录表达功能，target 表达独立依赖、构建选项或部署边界。共享库统一为 Toy3dCore、Toy3dShaderFormat、Toy3dAssets；资产处理复用 Toy3dAssetPipeline，Editor 共用代码放 Toy3dEditorCore。新增库须有不能被现有模块承载的稳定边界；不保留旧路径转发头或旧 target 别名。
- 共享服务优先接口注入，由 composition root 持有；不新增不可替换全局单例。现有受控 active 入口见模块文档，不构成扩大全局状态的理由。
- Shader include 白名单、ShaderMap key、RHI resource state、pass 调度留各业务模块。重复基础设施停止扩展，设计共享入口，按可独立验证的小批次迁移并删除旧入口，不长期双轨。
- Editor 写源码侧 project/asset，禁止写 bin 部署副本；资产子目录由使用者组织。engine/build 不是根构建目录。除升级依赖外不修改 engine/thirdparty。

## C++ 与接口

- 第一方 C++17、UTF-8、四空格缩进。文件/函数/变量 snake_case，类型/target PascalCase，宏 UPPER_SNAKE_CASE；只整理直接涉及区域。
- 第一方 C/C++ 格式以根 `.clang-format` 为准：函数（含构造函数、getter、头文件内联函数）、lambda 和控制流块使用 Allman 大括号，`{`、`}` 各占一行，禁止单行函数体或执行块；`if/else/for/while/do` 的受控语句必须加大括号，预处理器附近的自动补括号需人工核对。无行为构造/析构仍用 `= default`。
- 使用 clang-format 15+；日常运行 `./scripts/format-cpp.ps1 -Changed`，交付前必须通过 `./scripts/format-cpp.ps1 -Changed -Check`。脚本覆盖已暂存、未暂存和未跟踪的新文件，排除第三方、生成代码及 build/bin；不加 `-Changed` 扫描全部第一方文件。格式整理只改变排版和控制流括号，不顺带修改业务逻辑。
- 头文件 `#pragma once`、可独立包含、无 using namespace；cpp 先对应头，再标准库、第三方、项目头。多态基类虚析构、重写 override、单参数构造默认 explicit、无行为构造/析构 = default。
- 第一方 C++ 平台判断先显式包含 `platform/platform_defines.h`，统一用 `#if WITH_WIN` 等数值判断；原生 OS/架构宏只在该入口检测，禁止使用 `WITH_WIN64` 或依赖 PCH、runtime/generated/defines.h、全局编译定义。OS 与 CPU 架构分开，架构用 `TOY3D_ARCH_X64/TOY3D_ARCH_ARM64`；系统能力宏仍在具体实现检查，宏为真不代表模块已支持该平台。定义和最小示例见 `document/core.md`。
- 初始化所有值和原生句柄，禁止 C 风格转换。RAII 管资源，独占 unique_ptr，确有共享所有权才 shared_ptr；新代码不直接 new/delete。
- 新类型必须表达稳定的领域、所有权、生命周期、同步或错误语义。禁止只为访问控制、模板或第三方 API 增加 Key/Token/Enabler/Storage 伪概念；先简化 factory/private constructor/智能指针完整创建链。不可替代例外限 cpp 并解释原因。
- 优先普通函数、重载和显式分支；固定少量 variant 类型优先 get_if。visitor、泛型 lambda、SFINAE、tag dispatch、复杂 traits 只在实质减少复杂度时使用。
- C++17 新特性每个用途或紧邻同用途组写选择理由注释，不复述语法；至少覆盖 variant、optional、string_view、filesystem、from_chars、clamp、shared_mutex、byte、size、inline variable、structured binding、if constexpr、fold expression、CTAD。
- 能 const 的成员函数用 const；复杂只读参数 const&；枚举 enum class，仅需遍历/数量上界才设 Max。边界/槽数从 Max 或具名常量推导。
- 检查并诊断平台、图形 API、文件操作失败；未实现明确报不支持，不允许空操作成功。注释解释设计、生命周期、同步。
- 一个 cpp 多类时，每类首个实现前三行标题：两侧 `// --------------------------------------------------------------------------`，中间 `// 类名: 简短职责`；不重复每个成员。

## 渲染基线

- 公共 RHI 面向 Vulkan/D3D11/D3D12；D3D11 为 FL 11_0 / SM 5.0，不支持 D3D10。移动端 Vulkan ES3.1 profile = Vulkan 1.1 / SPIR-V 1.3 / 最多四个 bound descriptor sets；Cook/runtime 验证 required capabilities/limits，不随桌面设备抬高基线。
- 上层只依赖公共 RHI，无 Vk*/ID3D* 类型或后端判断；native 转换集中后端，枚举不按数值强转。差异用 capabilities/limits/format support/profile 表达，GPU 使用结束前不能销毁资源。
- 参考 UE4.27 通用术语/职责，保持 Toy3d PascalCase/snake_case/RHI 命名；不引入 UE 类型前缀、宏、反射、对象或兼容系统，语义等价才借名。
- 坐标完整基线在 `document/math.md`：厘米、left-handed、column-vector、column-major、0..1 reversed-Z、公共 CCW。逻辑 Binding Group 与 Vulkan sets 映射在 `document/rhi.md`，不能混为一谈。

## CMake 与验证

- 根文件声明 cmake_minimum_required/project，不复制历史重复。第一方 target 用 target_compile_features(... cxx_std_17) 并关闭扩展；配置绑定具体 target，正确区分 PRIVATE/PUBLIC/INTERFACE。
- 变量 TOY3D_UPPER_SNAKE_CASE，路径加双引号；平台顺序 WIN32、APPLE、ANDROID、其他 UNIX。后端独立选项/条件源码，配置检查 SDK/头/库。新源码优先显式 target_sources，glob 必须 CONFIGURE_DEPENDS。
- 生成头写 build，target include 使用；部署资源根通过 Toy3dRuntime PRIVATE 编译定义提供，不在源码目录生成配置头。资源操作用 `${CMAKE_COMMAND} -E`，构建后命令明确 POST_BUILD，禁止源码内构建。
- CMake 修改重新配置并构建受影响目标，只迁移直接相关旧配置。C++ 主 agent 分析/设计/实现/最终复查；完成后必须交 sub-agent 使用 verify-toy3d-build 独立构建/测试，再据结果修复。
- 文档/skill 检查链接、结构、事实、示例与接口，不构建 C++。验证结果写交付/PR，不混入长期文档；未运行不声称通过。

```powershell
./build_win.bat Debug
cmake -S . -B build -G "Visual Studio 17 2022" -A x64 -DBUILD_TESTING=ON
cmake --build build --config Debug --target Toy3dEditor
ctest --test-dir build -C Debug --output-on-failure
```

Windows 第二参数可选 Toy3dEditor；默认 Debug Editor，无参数暂停/显式参数不暂停，失败非零。macOS 用 `./build_macos.sh Debug`（默认 Xcode，sandbox/CI Unix Makefiles）。配置支持 Debug/Release/RelWithDebInfo/MinSizeRel；细分测试 target 从功能文档和 CMake 查。

## 交付

- 提交标题简短聚焦；PR 写行为变化、模块、实际验证、相关 issue，界面/渲染变化附截图或录屏。
- 不提交本机 IDE 配置、产物或临时资源；Vulkan SDK/validation 配置修改核对平台分支。
- 知识沉淀同步审查代码；规范冲突、不够通用或鲁棒性问题列位置、触发、影响和建议，与用户确认迭代范围，不顺带扩大实现改动。
