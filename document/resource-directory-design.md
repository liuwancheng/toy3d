# 资产、配置与平台构建资源目录

## 1. 职责与边界

`asset` 对应 UE 的 `Content`。`engine/asset` 保存引擎内置资产，`project/asset` 保存当前游戏项目资产；资产子目录由使用者组织，目录名不决定类型或身份。配置、应用打包输入和 Editor 界面资源各自独立。

资产文件遵循 [Asset 描述与处理数据格式](asset-pair-format-design.md)：一般资产的 `.asset` 与场景的 `.scene` 都是 YAML 描述；处理后数据才有配对 `.meta`，当前 Scene 不产生 meta。Editor 显示两类描述入口并由同一资产服务管理。缩略图在 `bin/saved/AssetThumbnails/` 缓存，不进入 `project/asset/`。

| 源目录 | 职责 | 部署位置 |
| --- | --- | --- |
| `engine/asset` | 引擎资产 | `bin/engine/asset` |
| `project/asset` | 游戏资产 | `bin/project/asset` |
| `engine/config` | 引擎默认配置 | `bin/engine/config` |
| `project/config` | 项目默认配置覆盖 | `bin/project/config` |
| `engine/editor/resources` | Editor 字体、Logo、界面图片 | `bin/editor/resources`，仅 Editor 部署 |
| `engine/build/windows/resources` | `.rc`、资源 ID、`.ico` | 编译嵌入 exe，不复制到资产目录 |
| `engine/build/mac/resources` | `.icns`、iconset、plist 模板 | `.icns` 与生成的 plist 进入 `.app`，iconset 不部署 |
| `engine/shader` | Shader 源码 | 现有 shader 构建规则管理产物 |
| `project/shader` | 项目 Shader 源码与 include，由 `project/config/shader_sources.txt` 显式登记 | Editor 编译产物进入 `bin/saved/material_shaders`，独立源码不作为 `.asset` 部署 |

`engine/build` 是受版本管理的构建输入；仓库根 `build` 是不提交的 CMake 输出。`engine/runtime/config` 是 C++ 配置系统代码，与 `engine/config` 数据分开。当前 `project` 仍是 Cube 验证项目，不引入项目描述文件、项目生成器或 Cook。

## 2. 加载、所有权与写入

Engine composition root 用现有 `FileSystem` 注册只读 `/Engine`、`/Project` 资产 mount，分别指向各自部署目录；配置通过独立的只读 `/Engine/Config`、`/Project/Config` mount 读取。Shader 保持 `/Engine/Shader`，运行生成数据保持 `/Saved`、`/Temp`。

Editor executable 持有独立的创作 `EditorWorkspace`，用相同文件系统实现挂载源码侧的可写 `/Project` 和只读 `/Engine`、`/Editor/Resources`。Content Browser 默认浏览 `/Project`，可以显示引擎资产。统一扫描两个资产根后才校验 Asset ID 与强依赖，允许项目引用引擎内置资产；失败保留最后有效 catalog。界面资源不参与 Asset 扫描。

`--Editor.AssetRoot` 只修改项目资产的创作根，不重定向 runtime 的部署配置或引擎资产。创作根不得与部署根或引擎资产根重叠，包括父子目录关系；不存在或无法安全解析时失败，不能降级到部署副本。资产 API 继续使用虚拟路径，物理根只存在于 composition root 与平台实现。

材质源码工作流拥有独立只读 `/Engine/Shaders`、`/Engine/ShaderIncludes`、`/Project/Shaders`、可选 `/Project/ShaderIncludes` 和 `/Project/Config` mount，以及可写 `/Saved`。它与创作 Asset mount 分开，Content Browser 不扫描这些根。`--Editor.ProjectShaderRoot` 与 `--Editor.ShaderConfigRoot` 可显式覆盖源码及登记清单根；不会随 `Editor.AssetRoot` 隐式变化。详见[材质源码迭代](material-source-workflow-design.md)。

## 3. 配置与平台资源

配置顺序为代码默认值 → `/Engine/Config/engine_config.ini` → `/Project/Config/engine_config.ini` → 命令行 → 运行时控制台。项目配置仅覆盖明确写出的键，省略键继承引擎值。项目配置文件缺失允许继续，其他读取错误记录日志；失败不修改已加载值。用户配置持久化层尚未实现。

Windows 各 executable 将自己的图标绑定到相同窗口资源 ID：Editor 用 `Toy3dEditor.ico`，Cube 用 `Toy3d.ico`。runtime 窗口读取当前 executable 的资源，无需依赖 Editor 类型或通过窗口标题判断。图标采用共享 Windows 资源生命周期，读取失败记录日志。

macOS 共用 `Info.plist.in`，各 target 显式填写应用名称、标识、版本和图标。构建使用 `.icns`，不在用户机器运行 iconset 转换；当前两个应用沿用已有 macOS 图标。目录采用 `windows`、`mac`，不按处理器位数命名。

## 4. 部署与迁移

Windows 一键入口为根目录 `build_win.bat [配置] [Toy3dEditor|Toy3dCubeTest]`，默认 `Debug Toy3dEditor`。脚本生成 `build/toy3d.sln`，启用 Vulkan 并编译指定 target；exe 和资源由 target 的 `POST_BUILD` 规则自动部署到 `bin`。显式参数调用不暂停，配置、编译或部署失败返回非零退出码；无参数双击运行会暂停显示结果。macOS 使用 `build_macos.sh`。

共用的 CMake 部署规则放入 `engine/build/cmake`，Editor 与 Cube 复制同一组引擎/项目输入；不删除共享资产树，不写回源码。Editor 另复制自己的界面资源。复制式开发部署不自动清理被删除的源文件；需要干净发布时使用独立 staging，后续 Cook 再提供发布清单。

先迁移配置与平台输入，再修改挂载、Editor 默认根和部署规则；同步更新使用文档。旧 `bin/asset` 不再挂载，自动迁移不删除用户可能存放其中的文件。

## 5. 验证

Windows 重新配置并构建 Editor、Cube 与受影响测试；覆盖项目配置继承/覆盖/读取失败、两个资产根的引用与重复身份、创作/部署根重叠拒绝、引擎资源只读、部署不会删除已有文件。启动两个应用检查配置/资源加载与正常退出。macOS 应用包配置做静态检查；当前 Windows 验证不宣称 macOS 实际构建通过。

## 6. Asset 代码目录

通用 Asset 容器、身份、索引与编辑事务统一位于 `engine/core/asset`，保留独立 `Toy3dResource` target。它与内容目录 `engine/asset`、`project/asset` 分开；不再建立 `engine/resource` 或笼统的 `engine/runtime/resource`。领域数据、外部导入和运行时对象仍各自分层；目录迁移不改变 Asset 文件格式、身份或公共 API。
