# Runtime：启动、配置、平台与资源

## 定位

Toy3dRuntime 包含 engine/runtime 的 Engine/Application、config/platform/input、gamescene/rendercore/renderscene/drivers；这些目录当前不等于独立 CMake target。Editor 入口在 engine/editor；仓库 project 是带 ShadowDemo Runtime 静态模块的游戏工程。共享能力复用 [Core](core.md)，不从 runtime 输出通用工具库。

## 启动与退出

Application 表达项目侧策略，Engine 管运行设施/World/渲染生命周期；不能把 Cube 内容或 Editor 面板写进 Engine。配置和服务成功后发布运行状态，失败逆序释放，不允许半启动。

root 持有文件系统、任务调度、Engine/Renderer 等服务；World 在 GT，Renderer 在逻辑 RT。SceneInterface 只在 Renderer Running 后发布，关闭前撤回。退出先停业务/异步请求，cancel/join 工具工作，再 drain/释放渲染，最后 shutdown TaskGraph/平台；完整次序见 [Render Framework](render-framework.md)。

Editor 启动策略不进入 main World play；PIE 由 Editor 拥有独立 World，只在 Playing 推进 gameplay，不能顺带 tick 编辑 World。缩略图/预览也使用独立 World。Game 与 PIE 共用 gamescene/scene_view.h：按 Actor/Component 顺序选择首个 Camera，无 Camera 使用统一备用视角。

Editor 入口在 Workspace 初始化前创建 LogBuffer 并调用 Engine::initialize_logging(buffer)，随后 pre_init 复用同一 Logger，避免丢失启动记录；业务/渲染线程退出后才关闭日志并最终 flush。Core 日志 contract 见 [Core](core.md#日志分发)，查看面板见 [Editor](editor.md)。

## Console 与配置

ConsoleManager 保存 section.key 字符串与胜出层的来源/行号；当前没有通用 typed CVar registry。INI 校验已知窗口尺寸、MSAA 和布尔项，未知项保留；数值 getter 完整解析并拒绝非有限 float。不要把字符串 setter 当完整运行期参数验证接口。

ConsoleManager 当前受控全局入口随运行生命周期初始化/关闭；不是所有 core 服务建立单例的模板。渲染变量改变通过 GT 验证/FIFO 更新，不让后台线程直接改 RT 状态。验证见 engine/runtime/tests/console_manager_tests.cpp。

## 路径与部署

| 源码输入 | 用途/写入边界 |
| --- | --- |
| engine/asset | 引擎内置资产，部署后只读 |
| <工程>/asset | 项目创作资产，Editor 写入 .toy 所在工程目录 |
| engine/config/base_engine.ini、<工程>/config/game_engine.ini | 引擎默认和项目覆盖 |
| engine/shader、<工程>/shader | 内置源构建登记、项目源自动发现；编译器实现另放 tools |
| engine/editor/resources | Editor 界面资源 |
| engine/build | 图标、plist 模板与平台部署输入 |
| 根 build、bin | 生成/部署副本，不能当创作源 |

FileSystem 在 startup 注册/冻结 mounts，源码/部署模式均显式确定根，无 cwd fallback。Saved cache 可重建，不承载不可丢失业务资产；runtime 加载部署输入，Editor 修改源输入，禁止混用。

新增资源同批更新 CMake 拷贝/部署规则及 lookup 路径；引擎资产、配置和 UI 资源不混入单一 resources 目录。构建生成头放 build；资源根由 Runtime PRIVATE 编译定义提供，不在源码目录生成配置头。

## 工程与分层配置

`<Name>.toy` 是 YAML 工程描述，工程根取描述文件所在目录，不猜 cwd，不固定使用仓库 project。格式见 core/asset/game_project.h；解析/编码复用 Toy3dAssets 的 PRIVATE yaml-cpp，不新增库。必填 format_version=1、32 位小写十六进制 project_id、name、engine_association；拒绝重复字段、未知字段/版本、alias/tag、多文档和超限输入。名字为字母开头的 ASCII 字母/数字/下划线，最多 64 字符，排除 Windows 保留名。

仓库中的真实描述为 [ShadowDemo.toy](../project/ShadowDemo.toy)；新建工程由 EditorProject::create 生成自己的 ID，不复制该工程身份。

```yaml
format_version: 1
project_id: b5811ef47b354463b795c548d8e80f25
name: ShadowDemo
engine_association: toy3d_dev
modules:
  - name: ShadowDemo
    type: Runtime
```

当前支持关联 `toy3d_dev` 的资源工程（modules=[]）或一个已链接的 Runtime 模块。描述格式仍可表达 Editor 模块，但宿主拒绝额外/不匹配模块；没有引擎安装注册表、动态加载或热重载。`EditorProject(editor_directory, module_name)` 校验宿主身份，普通 Toy3dEditor 不承载项目 C++。Scene > Open Project 按目标描述选择 Toy3dEditor 或 <Module>Editor，新进程切换，不把旧工程模块带到新工程。

创建入口 EditorProject::create(parent, name, editor_directory) 仍生成资源工程、独立 ID 与 asset/config/shader/include/src/saved，不生成 C++ 模板。C++ 工程由明确的 `TOY3D_GAME_PROJECT` CMake 路径加入，默认为仓库 project，空值仅构建引擎。模块链接 Toy3dRuntime，在自身 CMake 调用 `toy3d_add_game_hosts(module, descriptor)`；生成 <Module>Editor（复用 Toy3dEditorCore）和 <Module>Game（不链接 Editor）。项目 Editor 构建也部署对应 Game。工程不靠目录扫描自动编译。

仓库示例的构建/启动：

```powershell
cmake -S . -B build -G "Visual Studio 17 2022" -A x64 -DBUILD_TESTING=ON -DTOY3D_ENABLE_VULKAN_RHI=ON
cmake --build build --config Debug --target ShadowDemoEditor --parallel
./project/launch_editor.bat
# 直接运行游戏，使用 Game.StartupScene：
./bin/ShadowDemoGame.exe --Project=D:/GitProject/toy3d/project/ShadowDemo.toy
```

新项目换 TOY3D_GAME_PROJECT 和自身 target/descriptor；修改 C++ 后重新构建并重启对应宿主。反射由项目显式 codegen，Actor 工厂与场景装配见 [GameScene](gamescene.md#游戏工程接入边界)。

启动入口持有一个不可变 EditorProject，向 EngineStartupPaths、EditorWorkspacePaths、ShaderWorkflowPaths 注入各自所需根。Asset 工作区 `/Engine`→engine/asset、`/Project`→工程 asset；Shader 根 `/Engine/Shaders`、`/Engine/ShaderIncludes`、`/Project/Shaders`、`/Project/ShaderIncludes` 各自独立。AssetId 索引连接资产依赖，Material.shader_name 连接 Program 索引；同名配置覆盖不代表资产/Shader 覆盖。项目可以依赖引擎公共资源，引擎公共源不依赖具体项目。挂载 startup 冻结，worker 只用请求持有的路径，不在后台切换根。

配置顺序：getter 安全默认 → 必需 base_engine.ini → 可选 game_engine.ini → 命令行。同 section 按 key 覆盖，遗漏继承；每个 INI 层先整层解析/校验再发布，失败不部分覆盖。当前已知类型之外仍为字符串，不宣称所有参数都有类型注册。Project Settings 只写项目覆盖项，保留未知项；清空 StartupScene 表示引擎默认，删除键表示继承。保存前比对已读 bytes，使用 Core 原子单文件发布；不承诺外部编辑器并发 CAS。设置在下次启动生效，不即时重设窗口/渲染状态。

```ini
[Editor]
StartupScene=/Project/ShadowDemo.scene

[Game]
StartupScene=/Project/RotatingActor.scene
```

Editor 未指定工程时打开 `/Engine/Scenes/Default.scene`；有效工程按 Editor.StartupScene 打开，空值使用同一默认场景。场景非法/缺失/装配失败记录诊断并回退，保持工程关联，不修改工程配置。描述/配置校验失败则无工程启动并记录错误；默认场景本身损坏明确报错，不以硬编码 Cube/Light 替代。场景加载等待 Shader 启动验证完成，Editor 不 begin_play。Game 优先使用 --PlayScene，再取 Game.StartupScene，空值使用引擎默认 Scene；有效项目的场景缺失/非法明确退出，避免掩盖游戏配置错误。

工程 Saved 放 `<工程>/saved`；无工程放 OS 用户数据根/Toy3d/Editor。日志每 Editor/Game 实例按角色、本地启动日期时间和进程号命名，格式见 [Core 日志分发](core.md#日志分发)；布局和 Shader 缓存在同一 Saved 下。资源工程不加入引擎 CMake、不拷贝到 bin；引擎部署只复制自身 asset/config 和 Editor UI 资源。`--Project=D:/path/Game.toy` 可显式打开工程。

创建/打开工程自动补齐 launch_editor.bat、launch_editor.sh，已有自定义脚本保留；仅完全匹配已知生成模板的旧脚本升级为当前宿主。EditorProject 由入口注入 Editor 部署目录；saved/editor_launch.txt 缓存两行 UTF-8 数据（实际描述文件名、Editor 部署目录），打开时原子刷新，描述文件/项目移动后按新入口更新。启动脚本从自身目录定位工程，优先 TOY3D_EDITOR_BIN，再用 Saved 记录；记录缺失时要求根目录恰有一个 .toy，尝试相邻 ../bin。支持空格/Unicode 路径和额外启动参数，绑定的 --Project 最后传入；缺工程/Editor 或启动失败返回非零，不自动构建。Windows 无参数失败时暂停便于双击查看；POSIX 使用 sh launch_editor.sh，不依赖新建文件的 executable 位。脚本按模块启动 Toy3dEditor 或 <Module>Editor，不自动构建；Game 可直接启动或由 Scene > Standalone Play 启动；Saved 记录为本机缓存，不纳入版本管理。

验证入口：editor/tests/project_tests.cpp（描述、创建/移动、隔离、默认资产/无工程挂载）、runtime/tests/console_manager_tests.cpp（覆盖来源、失败整层保留）、editor/tests/editor_framework_tests.cpp（场景路径读取和 clean 状态）。项目扩展验证见 project/tests/rotating_actor_tests.cpp。工程进程内切换、DLL 热重载、C++ 模板生成和全资产 Cook 尚未实现；Shader Cook 已接入项目 Game 构建。Game 正常启动读取 `GameHostPaths.shader_deployment` 指定的 Player 清单，在装配场景前预加载全部 ShaderMap family，typed 配置查询只访问 CPU 缓存；缺源、配置、清单或 entry 明确失败。项目 `<module>Shaders` target 使用共享 Cook，在 build 生成 Player 目录，成功后拷贝到独立部署目录并将路径注入 Game；不使用 Saved 复活被删除的 Shader。开发时显式 `--EditorShaderArtifacts` 使用已发布的 Editor artifacts，Saved publication source hash 必须与整 family 一致，缺记录才查内置部署；这条开发入口同样预加载完整 family，缺配置不退回默认。`ShaderLoadConfig` 仅注入 built-in root 和是否需要 Editor 程序，已删除未使用的 ShaderCodeLibrary 模式与旧 path 字段；Game 不要求 HitProxy，Editor 仍必须具有 ShadowDepth/HitProxy/Global 所需程序。GPU Skin 使用同一集合与姿态；目前只有 Vulkan ES3.1 产物加载，其他后端未实现时必须明确不支持。

工程缺少 Git 规则时同时补齐 .gitignore（忽略 /saved/）和 .gitattributes（launch_editor.sh 保持 LF），已有规则文件保留；自定义规则须自行保留上述约束，避免提交本机启动缓存或把 shell 脚本检出为 CRLF。

## Platform/Input 与修改检查

OS/window/input 细节留 platform，runtime input 表达状态、映射和项目策略，不透出图形 native 类型。Editor 输入优先级见 [Editor](editor.md)。Android/Linux 等未验证路径不能因为有目录就声称生产可用。

平台选择复用 [Core 平台定义](core.md#平台定义)，显式包含 platform/platform_defines.h；Toy3dRuntime 通过 PUBLIC Toy3dAssets 传递 Toy3dCore 供头文件中的平台判断使用。ENGINE_ASSET_ROOT/ENGINE_SAVED_ROOT 由 Runtime PRIVATE 编译定义提供，保持既有 bin/saved 路径；根 CMake 不再向全仓或第三方注入 WITH_* 平台宏。CMake 仍用目标平台变量选择源码/库，OS 与架构、构建宿主与编译目标分别判断。

Vulkan loader/VMA/SDK include 仅在 TOY3D_ENABLE_VULKAN_RHI 开启时加入 Runtime，属于 PRIVATE 实现依赖；直接测试 native Vulkan 的四个 target 独立声明依赖并受同一开关控制。关闭后不链接 loader、不传播 SDK/VMA include；Shader 离线编译工具链独立于该 backend 开关。GLFW/GLM 等现有 PUBLIC 依赖尚未整体迁移，新增 target 不复制历史配置。

TOY3D_ENABLE_SHADER_MAP_ENTRY_LOADING 关闭时不编译开发加载器；当前 Editor 依赖它，因此不注册 Editor 程序及测试，配置阶段明确提示。Core、Shader compiler、资产处理、Runtime 与其独立测试仍可构建；不会靠失效排除路径偷偷保留加载器实现。

改启动/平台/CMake 需重新配置、构建受影响程序，检查失败回滚、无窗口/最小化、退出中异步任务、源/部署模式及资源缺失；普通配置补 Console 测试。命令从 AGENTS 和平台脚本取得，不把某次机器日志保留为 contract。
