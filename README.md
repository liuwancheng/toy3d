# Toy3d

跨平台 3D 引擎、Runtime 与 Editor。协作规范见 `AGENTS.md`，功能文档入口是 `document/index.md`；本文件只讲环境、构建、启动，以及 Shader 和多线程的概览。

## 环境

- Windows：Visual Studio 2022（Desktop development with C++）、CMake 3.19+（打包 3.21+）、Git LFS、支持 Vulkan 的显卡驱动。
- macOS：Xcode（含命令行工具）、CMake、Git LFS。
- Vulkan 头文件与 loader（含 MoltenVK）随 `engine/thirdparty/` 提供，无需另装 Vulkan SDK。
- `engine/thirdparty/ShaderToolchain/` 的编译器二进制由 Git LFS 管理，首次 clone 后执行 `git lfs install`、`git lfs pull`。

## 构建

```powershell
./build_win.bat Debug      # Windows，默认 Debug Toy3dEditor；可换 Release/RelWithDebInfo/MinSizeRel
./build_macos.sh Debug     # macOS，默认 Xcode；受限环境自动改用 Unix Makefiles
```

构建目录 `build/`，可执行文件与资源由 POST_BUILD 部署到 `bin/`，两者都不入库。Editor/Game 目前只有 Vulkan 后端，脚本已默认开启 `TOY3D_ENABLE_VULKAN_RHI`。

## 启动

```powershell
./project/launch_editor.bat        # Windows，打开仓库示例工程 ShadowDemo
sh ./project/launch_editor.sh      # macOS

./bin/Toy3dEditor.exe --Project=D:/GitProject/toy3d/project/ShadowDemo.toy   # 省略 --Project 打开引擎默认场景
./bin/ShadowDemoGame.exe --Project=D:/GitProject/toy3d/project/ShadowDemo.toy # Game，场景取 Game.StartupScene
```

启动脚本不自动构建；bin 不在默认位置时用 `TOY3D_EDITOR_BIN` 指定。也可以在 Editor 里用 Scene > Standalone Play 启动 Game。

## 测试与打包

```powershell
ctest --test-dir build -C Debug --output-on-failure
./scripts/package-project.ps1 -Project ./project/ShadowDemo.toy -Output ./build/packages/ShadowDemo -Configuration Debug
```

部分 GPU 用例需要可用的 Vulkan 设备。

## Shader

自有的 v2 Shader DSL（语法见 `document/shader-language-v2.ebnf`），离线编译为 SPIR-V；当前生产路径只有 Vulkan ES3.1。

- `engine/shader/`、`project/shader/` 是源码，公共 include 为 `engine/shader/include/Toy*.hlsli`。
- `engine/tools/shader_compiler/` 是 frontend/layout/codegen/CLI（外部工具链为锁定的 DXC + spirv-val），`engine/core/shader/` 是格式与共享协议（permutation、schema、entry/index reader），`engine/runtime/rendercore/shader/` 是运行时 ShaderMap 与 typed parameters。
- 源按 Usage=Global/Material/MeshPass、Geometry=Standard/Custom 声明；运行时按 role（Forward 必需，另有 ShadowDepth/HitProxy）+ VertexFactory（Local/GPUSkin）+ 静态配置查已编译的 Program。
- 编译入口：内置源用 `compile-vulkan --settings ... --build-mode Editor|Player`，项目 Game 由 `cook-vulkan` 生成 Player 部署清单；运行时只读集合索引，不扫描目录猜程序。
- 详见 `document/shader.md`。

## 多线程

- `engine/core/threading/` 提供 Thread/Event/RunnableThread/ThreadManager 与 Queue，`threading/task_graph/` 提供 TaskGraph、GraphTask、GraphEvent、NamedThread；TaskGraph 由 composition root 创建持有，不是全局服务定位器。
- 核心是 GT（game thread）与 RT（render thread）：GT 拥有可变 World，RT 拥有 SceneProxy 与资源渲染状态。GT→RT 唯一普通入口是 `enqueue_render_command(name, rvalue_callable)`，closure 必须 `void() noexcept` 且只携带 owned 数据；`RenderCommandFence` 只是 CPU 完成点，不代表 GPU 完成。
- 资产解码走 `engine/runtime/asset_loader/` 的 AssetLoader：一个专用线程 + 四级优先级队列（Critical/High/Normal/Low），同一资产 single-flight；`decode` 跑加载线程，`adopt` 跑 GT tick。
- 注意：AnyWorker 不保证由物理 worker 执行（GT 等待时会帮忙跑），不能持业务锁等待任务，长期阻塞 I/O 用专用线程。
- 详见 `document/threading.md`、`document/render-framework.md`。

## 文档

- `AGENTS.md`：协作规范、目录边界、CMake 与交付约定。
- `document/index.md`：按任务选择功能主文档。
- `engine/thirdparty/ShaderToolchain/README.md`：Shader 工具链的发布与升级。
