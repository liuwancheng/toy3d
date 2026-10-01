# Runtime：启动、配置、平台与资源

## 定位

Toy3dRuntime 包含 engine/runtime 的 Engine/Application、config/platform/input、gamescene/rendercore/renderscene/drivers；这些目录当前不等于独立 CMake target。项目程序代码在 project/src，Editor 入口在 engine/editor。共享能力复用 [Core](core.md)，不从 runtime 输出通用工具库。

## 启动与退出

Application 表达项目侧策略，Engine 管运行设施/World/渲染生命周期；不能把 Cube 内容或 Editor 面板写进 Engine。配置和服务成功后发布运行状态，失败逆序释放，不允许半启动。

root 持有文件系统、任务调度、Engine/Renderer 等服务；World 在 GT，Renderer 在逻辑 RT。SceneInterface 只在 Renderer Running 后发布，关闭前撤回。退出先停业务/异步请求，cancel/join 工具工作，再 drain/释放渲染，最后 shutdown TaskGraph/平台；完整次序见 [Render Framework](render-framework.md)。

Editor 启动策略不进入 main World play，不能顺带运行 gameplay tick；缩略图/预览使用独立 World，不污染主场景。

Editor 入口在 Workspace 初始化前创建 LogBuffer 并调用 Engine::initialize_logging(buffer)，随后 pre_init 复用同一 Logger，避免丢失启动记录；业务/渲染线程退出后才关闭日志并最终 flush。Core 日志 contract 见 [Core](core.md#日志分发)，查看面板见 [Editor](editor.md)。

## Console 与配置

Runtime Console 由 config 模块实现，变量登记、类型、默认值、可修改阶段与覆盖来源明确；读取和赋值不能绕 typed validation。覆盖遵循现有默认/配置/命令行顺序，平台路径和用户配置由 composition root 决定。

ConsoleManager 当前受控全局入口随运行生命周期初始化/关闭；不是所有 core 服务建立单例的模板。渲染变量改变通过 GT 验证/FIFO 更新，不让后台线程直接改 RT 状态。验证见 engine/runtime/tests/console_manager_tests.cpp。

## 路径与部署

| 源码输入 | 用途/写入边界 |
| --- | --- |
| engine/asset | 引擎内置资产，部署后只读 |
| project/asset | 项目创作资产，Editor 写入源目录 |
| engine/config、project/config | 引擎默认和项目策略 |
| engine/shader、project/shader | Shader 源与显式登记，编译器实现另放 tools |
| engine/editor/resources | Editor 界面资源 |
| engine/build | 图标、plist 模板与平台部署输入 |
| 根 build、bin | 生成/部署副本，不能当创作源 |

FileSystem 在 startup 注册/冻结 mounts，源码/部署模式均显式确定根，无 cwd fallback。Saved cache 可重建，不承载不可丢失业务资产；runtime 加载部署输入，Editor 修改源输入，禁止混用。

新增资源同批更新 CMake 拷贝/部署规则及 lookup 路径；引擎资产、配置和 UI 资源不混入单一 resources 目录。构建生成头放 build；资源根由 Runtime PRIVATE 编译定义提供，不在源码目录生成配置头。

## Platform/Input 与修改检查

OS/window/input 细节留 platform，runtime input 表达状态、映射和项目策略，不透出图形 native 类型。Editor 输入优先级见 [Editor](editor.md)。Android/Linux 等未验证路径不能因为有目录就声称生产可用。

平台选择复用 [Core 平台定义](core.md#平台定义)，显式包含 platform/platform_defines.h；Toy3dRuntime 通过 PUBLIC Toy3dAssets 传递 Toy3dCore 供头文件中的平台判断使用。ENGINE_ASSET_ROOT/ENGINE_SAVED_ROOT 由 Runtime PRIVATE 编译定义提供，保持既有 bin/saved 路径；根 CMake 不再向全仓或第三方注入 WITH_* 平台宏。CMake 仍用目标平台变量选择源码/库，OS 与架构、构建宿主与编译目标分别判断。

Vulkan loader/VMA/SDK include 仅在 TOY3D_ENABLE_VULKAN_RHI 开启时加入 Runtime，属于 PRIVATE 实现依赖；直接测试 native Vulkan 的四个 target 独立声明依赖并受同一开关控制。关闭后不链接 loader、不传播 SDK/VMA include；Shader 离线编译工具链独立于该 backend 开关。GLFW/GLM 等现有 PUBLIC 依赖尚未整体迁移，新增 target 不复制历史配置。

TOY3D_ENABLE_SHADER_MAP_ENTRY_LOADING 关闭时不编译开发加载器；当前 Editor/Cube 依赖它，因此不注册这两个程序及 Editor 测试，配置阶段明确提示。Core、Shader compiler、资产处理、Runtime 与其独立测试仍可构建；不会靠失效排除路径偷偷保留加载器实现。

改启动/平台/CMake 需重新配置、构建受影响程序，检查失败回滚、无窗口/最小化、退出中异步任务、源/部署模式及资源缺失；普通配置补 Console 测试。命令从 AGENTS 和平台脚本取得，不把某次机器日志保留为 contract。
