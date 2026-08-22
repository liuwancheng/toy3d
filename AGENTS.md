# Repository Guidelines

## 项目结构与模块组织

这是一个 C++17/CMake 3D 引擎项目。主要目录如下：

```text
toy3d/
├── engine/
│   ├── core/                    runtime、editor 与 tools 共享的第一方基础设施
│   ├── runtime/                 第一方运行时代码
│   │   ├── config/              runtime 配置与命令行策略
│   │   ├── drivers/
│   │   │   ├── rhi/             跨图形 API 的公共 RHI
│   │   │   └── vulkan/          Vulkan 后端
│   │   ├── gamescene/           游戏场景
│   │   ├── input/               runtime 输入模型与映射
│   │   ├── platform/            win、mac、android 等平台实现
│   │   ├── renderscene/         上层渲染场景与 pass
│   │   └── generated/           历史生成头文件位置，待迁移
│   ├── editor/                  Toy3dEditor 入口与编辑器代码
│   ├── tools/
│   │   └── shader_compiler/     shader compiler 前端、CLI 与测试
│   ├── shader/                  shader 源码及其构建规则
│   ├── asset/                   运行资源
│   ├── template/                配置文件模板
│   └── thirdparty/              第三方依赖
├── document/                    架构与设计文档
├── .codex/skills/               仓库级 Codex skills
├── build/                       CMake 构建目录，不提交
└── bin/                         可执行文件和复制后的运行资源，不提交
```

现有公共基础设施的 CMake target、公共头文件和最小调用方式统一索引在 `document/core-module-usage-index.md`。新增文件、日志、线程、任务等通用能力前先查该索引及对应专项设计，避免重复实现。

新增运行时代码放入 `engine/runtime/` 中职责最接近的模块，不再建立笼统的 `engine/runtime/core/`。需要同时被 runtime、editor 或 tools 使用的第一方基础设施放入 `engine/core/`，不得为了复用而让工具反向依赖 `engine/runtime/`。RHI 公共类型和行为放在 `drivers/rhi/`，图形 API 实现放在各自独立后端目录；renderscene 不得依赖后端类型。离线工具放在 `engine/tools/`，不得反向依赖 editor。shader 源文件与 shader compiler 实现分开管理。除升级依赖外不要修改 `engine/thirdparty/`。

### 文档读取与规范优先级

- `document/index.md` 是设计文档状态和规范入口的唯一索引。AI 开始架构、实现或审查任务时，应先按任务涉及的模块读取索引中标记为 `Active` 的对应文档，不得默认遍历全部 `document/`。
- `document/archive/` 只保存历史方案、旧施工台账和已被替代的说明，不构成当前实现约束。除非用户明确要求追溯历史、比较旧方案或恢复背景，否则 AI 不得读取、引用或依据该目录内容作出设计和实现决定。
- 文档发生冲突时，优先级依次为：`AGENTS.md`、`document/index.md` 指向的 `Active` 专项设计、当前公共接口与测试、`Draft` 文档。`Historical` 与 `Superseded` 文档没有规范效力。
- 施工进度、验证流水账和阶段性评审记录不得混入长期 contract；完成后应归档或压缩为设计文档中的简短状态摘要。

## 基础设施与模块边界规范

- 新增文件系统、日志、进程、线程/任务、时间、配置、序列化、缓存、哈希、ID、内存分配或通用容器等能力前，必须先搜索并盘点现有实现，判断它属于业务策略、模块内机制还是跨模块基础设施。禁止业务模块为完成局部闭环而再封装一套语义重复的通用系统。
- 已判定为文件、日志、进程、任务、时间等通用系统的能力，即使当前只有一个调用模块，也必须放入 `engine/core/` 的独立第一方目标；当前调用方数量不是允许业务模块自建通用系统的理由。仅与领域模型紧密耦合、无法形成独立稳定 contract 的机制才留在业务模块。业务模块依赖共享接口并保留自身策略层，不得让共享基础设施反向依赖 Shader、RenderScene、RHI backend、editor 或其他具体业务。
- 实现通用系统前必须先形成设计方案并写入 `document/`：至少说明用例与非目标、目录和 CMake target、接口与实现分层、所有权和生命周期、线程模型、错误模型、平台差异、安全边界、测试矩阵、迁移顺序及删除旧实现的条件。方案未确认前不得先在业务模块中落临时正式接口。
- 通用系统优先采用接口注入和 composition root 持有，不在 library core 中新增不可替换的全局单例。平台或第三方细节只存在于具体实现；调用方不得绕过共享接口直接使用另一套实现。确有性能或平台原因需要例外时，必须在设计文档中记录理由和收敛路径。
- 抽离通用能力不等于把业务规则下沉。Shader include 白名单、ShaderMap key、RHI resource state、RenderScene pass 调度等领域策略仍属于对应模块，只组合使用共享文件、任务、哈希等基础能力。
- 修改已有模块时若发现重复基础设施，应先停止扩展重复实现，补齐共享方案并按可独立验证的小批次迁移；不得一次性重写所有调用方，也不得在新旧两套正式入口之间长期双轨运行。

## 构建、测试与开发命令

- `cmake -S . -B build -G "Visual Studio 17 2022" -A x64 -DBUILD_TESTING=ON`：推荐的 Windows 配置命令。
- `cmake --build build --config Debug --target Toy3dEditor`：构建 Debug 编辑器及其运行时依赖。
- `cmake --build build --config Debug --target Toy3dShaderCompilerFrontendTests`：构建 shader compiler 前端测试。
- `ctest --test-dir build -C Debug --output-on-failure`：运行已登记的测试并输出失败诊断。
- `./build_macos.sh Debug`：在 macOS 上配置并构建 `Toy3dEditor`；默认使用 Xcode，在 sandbox/CI 中使用 Unix Makefiles。也可传入 `Release`、`RelWithDebInfo` 或 `MinSizeRel`。

`build_win.bat` 仍是未指定 `-S/-B` 的历史脚本，在修正前不得作为推荐入口。构建产物和复制后的运行资源会进入 `bin/`。项目禁止 CMake 源码内构建。

## C++ 编码规范

- 第一方代码统一使用 C++17，不使用更高版本特性。新增代码放入职责最接近的模块；除依赖升级外不得修改 `engine/thirdparty/`。
- 源码、CMake、配置和文档文件统一使用 UTF-8 编码。
- 使用四空格缩进，不使用 Tab。文件名、函数和变量使用 snake_case；类、结构体和 CMake 目标使用 PascalCase；宏使用 UPPER_SNAKE_CASE。
- 修改旧代码时只整理直接涉及的区域，禁止产生无关格式化改动。
- 头文件使用 `#pragma once` 并保证能够独立包含；禁止在头文件中使用 `using namespace`。`.cpp` 首先包含对应头文件，再包含标准库、第三方库和项目头文件。
- 多态基类必须有虚析构函数，重写函数必须使用 `override`。单参数构造函数默认使用 `explicit`，无自定义行为的构造和析构使用 `= default`。
- 所有变量、结构体和图形 API 句柄必须初始化。禁止 C 风格类型转换，使用明确的 C++ 类型转换。
- 使用 RAII 管理资源；独占所有权使用 `std::unique_ptr`，仅在确有共享所有权时使用 `std::shared_ptr`。新代码禁止直接使用 `new` 和 `delete`。
- 新增类型、命名层或辅助对象前，必须确认它表达稳定的领域、所有权、生命周期、同步或错误语义。禁止仅为绕过 C++ 访问控制、模板实例化、第三方 API 形状或其他语言机制而引入 `Key`、`Token`、`Enabler`、`Storage` 等伪概念；若没有更简单的标准写法且确需例外，必须限制在实现文件内并用注释说明不可替代的原因，不得泄漏到公共接口。
- 当 factory、private constructor、智能指针或模板访问限制等实现约束互相冲突时，必须先重新检查这些约束是否确有必要，优先采用直接的值构造、移动、组合或简化后的创建路径。修复此类问题时必须复查完整创建链，禁止只用另一个机制性 wrapper 替换原 wrapper；代码复查必须逐项判断新增名词是否具有长期架构含义。
- C++17 特性必须优先采用团队常见、可直接顺序阅读的写法，不得仅因代码更短而选用偏门语法。`std::variant` 分支较少且类型集合固定时优先使用显式 `std::get_if`；只有 visitor 能实质减少重复且行为仍清晰时才使用 `std::visit`。泛型 lambda、SFINAE、tag dispatch 或复杂 type traits 仅在普通函数、显式重载或直接分支明显更差时使用；代码审查时必须以可读性而非技巧性为准。
- 使用 C++17 新增特性时，每个使用点或紧邻且用途相同的一组使用点必须有注释，说明该特性在此处解决的具体问题以及选择它的理由；注释不得只复述语法。此要求至少覆盖 `std::variant`、`std::optional`、`std::string_view`、`std::filesystem`、`std::from_chars`、`std::clamp`、`std::shared_mutex`、`std::byte`、`std::size`、inline variable、structured binding、`if constexpr`、fold expression 和类模板参数推导；重复使用同一特性但用途不同必须分别说明。
- 能声明为 `const` 的成员函数必须声明为 `const`；只读复杂参数使用 `const&`。枚举类型使用 `enum class`；仅当枚举需要遍历或作为数量上界时，才在末尾定义 `Max`。常量优先使用 `constexpr`。数组长度、循环边界及资源槽位数量等必须从对应的 `Max` 或具名常量推导，禁止使用硬编码数值。
- 平台 API、图形 API 和文件操作的返回结果必须检查并记录错误，禁止静默忽略失败。
- 注释应说明设计原因、资源生命周期或同步约束，不要重复描述代码本身。

## 渲染器与 RHI 规范

- 项目以跨图形 API 渲染器为目标，RHI 需要支持 Vulkan、DirectX 11 和 DirectX 12。D3D11 基线为 Feature Level 11_0、Shader Model 5.0；不支持 D3D10、Feature Level 10.x 或 Shader Model 4。整体设计参考 Unreal Engine 4.27，但不照搬不适合本项目的复杂机制。
- 第一方渲染、RHI、RenderCore 与 Shader 系统在语义等价时，命名优先采用 UE4.27 中容易识别的通用术语，降低架构沟通和源码阅读成本，例如 `Device`、`CommandContext`、`PipelineState`、`RenderPass`、`ShaderMap`、`ShaderMapEntry`、`ShaderCode` 与 `ShaderCodeLibrary`。借用术语时必须保持 Toy3d 的实际职责边界并在设计文档中定义，不得仅因名称相似照搬 UE 实现；语义不等价时使用能准确描述 Toy3d 行为的名称。
- UE 风格只用于架构术语和职责划分，不引入 `F`/`E`/`T`/`I` 类型前缀、UE 宏、反射系统、对象系统或历史兼容层；代码标识符继续遵守本项目的 PascalCase、snake_case 和 `RHI` 前缀规范。
- 上层渲染代码只能依赖 RHI 公共接口，禁止直接引用 `Vk*`、`ID3D11*`、`ID3D12*` 等后端类型。
- 各图形 API 使用独立后端目录。后端专用类型、宏和头文件不得泄漏到公共 RHI 接口。
- RHI 公共接口表达通用渲染语义，不得围绕单一图形 API 设计。API 特有能力通过 capability 查询或后端扩展提供，禁止在上层散布后端判断。
- RHI 资源必须明确所有权、创建者、销毁者和生命周期；GPU 使用结束前不得销毁资源。
- 命令提交、资源状态转换、同步和描述符管理由对应后端实现，上层只提供必要的语义信息。
- 公共枚举到后端枚举的转换必须集中在后端实现中，禁止依赖枚举数值相同进行强制转换。
- 修改 RHI 公共接口时必须同时评估 Vulkan、DirectX 11、DirectX 12 和移动端 Vulkan profile 的可实现性，不得为单一后端破坏公共抽象。平台差异通过 capability、limits、format support 和 profile 表达，禁止在上层散布图形 API 或操作系统判断。
- 默认移动端基线为 `VulkanPortable v1`（Vulkan 1.1、SPIR-V 1.3、最多四个 bound descriptor sets）。Cook 与 runtime 都要验证 required capabilities/limits；不得按当前桌面 GPU 自动抬高基线。
- 全引擎坐标和矩阵约定固定为 left-handed、+X right、+Y up、+Z forward、1 unit=1 meter、column-vector、column-major storage、HLSL `mul(matrix, vector)`。clip depth 为 0..1 reversed-Z：near=1、far=0、clear=0.0、默认 `GreaterEqual`；公共 front face 为 CounterClockwise。Vulkan 由 backend 使用 negative viewport height 处理 Y 并修正 native front face，Shader 禁止手写平台翻转。
- Shader 的 Global、View、Pass、Material、Object 是五个逻辑 Binding Group，不等于 descriptor set。Vulkan portable 映射为 set 0=Global+View、set 1=Pass、set 2=Material、set 3=Object，set 内紧凑分配；D3D11/D3D12 使用 target/stage/register-class 独立映射。跨 target reflection 不比较 native slot 数字。
- 暂未实现的能力必须明确返回不支持或产生可诊断错误，禁止无操作后返回成功。
- 可参考 UE4.27 的架构和命名思想，但保持本项目现有命名体系，不引入 UE 的宏系统和对象系统。

## CMake 规范

- `cmake_minimum_required()` 和顶层 `project()` 只在根 `CMakeLists.txt` 中声明；`engine/CMakeLists.txt` 中的重复声明属于历史问题，不得在新模块复制，直接涉及时再迁移。
- 第一方目标使用 `target_compile_features(... cxx_std_17)` 并关闭编译器扩展。
- 编译定义、编译选项、包含目录、源码和链接依赖必须使用 `target_*` 命令绑定到具体目标，避免全局配置。
- 按依赖传播需求正确使用 `PRIVATE`、`PUBLIC` 和 `INTERFACE`，实现依赖不得无故暴露给使用者。
- 项目变量使用 `TOY3D_UPPER_SNAKE_CASE`，文件路径使用双引号包围。
- Vulkan、DirectX 11 和 DirectX 12 使用独立构建选项；平台和后端源码只能在对应条件下加入目标。配置阶段必须检查所需 SDK、头文件和库。
- 平台判断按 `WIN32`、`APPLE`、`ANDROID`、其他 `UNIX` 的顺序处理。
- 新模块优先通过 `target_sources()` 显式登记源码；使用 glob 时必须指定 `CONFIGURE_DEPENDS`。
- 新生成头文件必须写入构建目录，并通过目标的 include directory 使用，不得写入源码目录。`engine/runtime/generated/defines.h` 是待迁移的历史路径，不得继续扩展或手工修改。
- 构建和资源操作使用 `${CMAKE_COMMAND} -E`；目标构建后的命令必须显式指定 `POST_BUILD`。
- 修改 CMake 后至少完成一次配置和受影响目标的构建。
- 修改旧 CMake 时只迁移直接涉及的历史全局配置或命名，禁止借机进行无关的全仓整理。

## AI 协作规范

- AI 的对话、说明、提问、代码审查意见和交付总结必须使用中文；代码标识符、API 名称、命令及必要的技术术语保持原文。
- 涉及代码修改时，主 agent 负责需求分析、设计决策、代码实现和最终复查；实现完成后，必须将构建与测试验证交给 sub-agent 独立执行，主 agent 根据验证结果修复问题并汇总交付。
- 纯文档或 skill 修改只需执行与内容对应的结构和一致性检查，无需为此构建 C++ 目标。

## 提交与 Pull Request 规范

近期提交信息较短，例如 `添加 Shader 编译器前端`、`完善 RHI 管线缓存与深度模板支持`。提交标题应简洁，尽量聚焦单一改动。PR 需要包含变更说明、受影响模块、已运行的构建/测试命令、相关 issue；若改动影响编辑器界面或渲染效果，请附截图或录屏说明。

## 安全与配置提示

不要提交本机 IDE 配置、生成的构建产物或临时资源。CMake 中的 Vulkan SDK 路径和 validation layer 定义与平台强相关，修改时需要同步检查所有受影响的平台分支。
