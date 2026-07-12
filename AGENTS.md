# Repository Guidelines

## 项目结构与模块组织

这是一个 C++17/CMake 3D 引擎项目。根目录 `CMakeLists.txt` 负责平台宏配置并引入 `engine/`。第一方运行时代码位于 `engine/runtime/`，按领域分为 `core/`、`drivers/`、`gamescene/`、`platform/` 和 `renderscene/`。平台实现放在 `engine/runtime/platform/win`、`mac` 或 `android`。编辑器入口在 `engine/editor/source/`，目标名为 `Toy3dEditor`。生成头文件写入 `engine/runtime/generated/`，资源放在 `engine/asset/`。第三方依赖集中在 `engine/thirdparty/`，除升级依赖外不要修改。

## 构建、测试与开发命令

- `build_win.bat`：在仓库根目录生成 Visual Studio 2022 x64 工程。
- `cmake -S . -B build -G "Visual Studio 17 2022" -A x64`：推荐的 Windows 显式配置命令。
- `cmake --build build --config Debug`：构建已配置的 Debug 工程。
- `./build_macos.sh`：在 macOS 上生成 `build/` 下的 Xcode 工程。

构建产物和复制后的运行资源会进入 `bin/`。项目禁止 CMake 源码内构建。

## C++ 编码规范

- 第一方代码统一使用 C++17，不使用更高版本特性。新增代码放入职责最接近的模块；除依赖升级外不得修改 `engine/thirdparty/`。
- 源码、CMake、配置和文档文件统一使用 UTF-8 编码。
- 使用四空格缩进，不使用 Tab。文件名、函数和变量使用 snake_case；类、结构体和 CMake 目标使用 PascalCase；宏使用 UPPER_SNAKE_CASE。
- 修改旧代码时只整理直接涉及的区域，禁止产生无关格式化改动。
- 头文件使用 `#pragma once` 并保证能够独立包含；禁止在头文件中使用 `using namespace`。`.cpp` 首先包含对应头文件，再包含标准库、第三方库和项目头文件。
- 多态基类必须有虚析构函数，重写函数必须使用 `override`。单参数构造函数默认使用 `explicit`，无自定义行为的构造和析构使用 `= default`。
- 所有变量、结构体和图形 API 句柄必须初始化。禁止 C 风格类型转换，使用明确的 C++ 类型转换。
- 使用 RAII 管理资源；独占所有权使用 `std::unique_ptr`，仅在确有共享所有权时使用 `std::shared_ptr`。新代码禁止直接使用 `new` 和 `delete`。
- 能声明为 `const` 的成员函数必须声明为 `const`；只读复杂参数使用 `const&`。新枚举使用 `enum class`，常量优先使用 `constexpr`。
- 平台 API、图形 API 和文件操作的返回结果必须检查并记录错误，禁止静默忽略失败。
- 注释应说明设计原因、资源生命周期或同步约束，不要重复描述代码本身。

## 渲染器与 RHI 规范

- 项目以跨图形 API 渲染器为目标，RHI 需要支持 Vulkan、DirectX 10 和 DirectX 12。整体设计参考 Unreal Engine 4.27，但不照搬不适合本项目的复杂机制。
- 上层渲染代码只能依赖 RHI 公共接口，禁止直接引用 `Vk*`、`ID3D10*`、`ID3D12*` 等后端类型。
- 各图形 API 使用独立后端目录。后端专用类型、宏和头文件不得泄漏到公共 RHI 接口。
- RHI 公共接口表达通用渲染语义，不得围绕单一图形 API 设计。API 特有能力通过 capability 查询或后端扩展提供，禁止在上层散布后端判断。
- RHI 资源必须明确所有权、创建者、销毁者和生命周期；GPU 使用结束前不得销毁资源。
- 命令提交、资源状态转换、同步和描述符管理由对应后端实现，上层只提供必要的语义信息。
- 公共枚举到后端枚举的转换必须集中在后端实现中，禁止依赖枚举数值相同进行强制转换。
- 修改 RHI 公共接口时必须同时评估 Vulkan、DirectX 10 和 DirectX 12 的可实现性，不得为单一后端破坏公共抽象。
- 暂未实现的能力必须明确返回不支持或产生可诊断错误，禁止无操作后返回成功。
- 可参考 UE4.27 的架构和命名思想，但保持本项目现有命名体系，不引入 UE 的宏系统和对象系统。

## CMake 规范

- `cmake_minimum_required()` 和根 `project()` 只在根 `CMakeLists.txt` 中声明。
- 第一方目标使用 `target_compile_features(... cxx_std_17)` 并关闭编译器扩展。
- 编译定义、编译选项、包含目录、源码和链接依赖必须使用 `target_*` 命令绑定到具体目标，避免全局配置。
- 按依赖传播需求正确使用 `PRIVATE`、`PUBLIC` 和 `INTERFACE`，实现依赖不得无故暴露给使用者。
- 项目变量使用 `TOY3D_UPPER_SNAKE_CASE`，文件路径使用双引号包围。
- Vulkan、DirectX 10 和 DirectX 12 使用独立构建选项；平台和后端源码只能在对应条件下加入目标。配置阶段必须检查所需 SDK、头文件和库。
- 平台判断按 `WIN32`、`APPLE`、`ANDROID`、其他 `UNIX` 的顺序处理。
- 新模块优先通过 `target_sources()` 显式登记源码；使用 glob 时必须指定 `CONFIGURE_DEPENDS`。
- 生成头文件必须写入构建目录，并通过目标的 include directory 使用，不得写入源码目录。
- 构建和资源操作使用 `${CMAKE_COMMAND} -E`；目标构建后的命令必须显式指定 `POST_BUILD`。
- 修改 CMake 后至少完成一次配置和受影响目标的构建。

## AI 协作规范

- AI 的对话、说明、提问、代码审查意见和交付总结必须使用中文；代码标识符、API 名称、命令及必要的技术术语保持原文。

## 提交与 Pull Request 规范

近期提交信息较短，例如 `render module`、`rhi module update`。提交标题应简洁，尽量聚焦单一改动。PR 需要包含变更说明、受影响模块、已运行的构建/测试命令、相关 issue；若改动影响编辑器界面或渲染效果，请附截图或录屏说明。

## 安全与配置提示

不要提交本机 IDE 配置、生成的构建产物或临时资源。CMake 中的 Vulkan SDK 路径和 validation layer 定义与平台强相关，修改时需要同步检查所有受影响的平台分支。
