# 材质源码迭代

## 1. 所有权与目录

承接 material-system-design 的 M7。EditorApplication 持有 ProcessService、专用 ThreadManager 和 MaterialShaderWorkflow；向材质窗口、创建框和 MaterialAssignments 注入该工作流。工作流拥有显式登记源、已验证 Program、属性视图、单个编译请求与结果；不是通用 Asset cache，不向 Core 下沉 Shader 策略。

项目源码在 project/shader，清单在 project/config/shader_sources.txt，首行为 `Toy3dShaderSources 1`，其余非注释行是 `Project/Surface/<名称><TAB><shader根下相对路径>`。含内置源总计最多 256 条，清单 64 KiB，单源码 4 MiB。拒绝重复逻辑名、重复物理源、越界/符号链接逃逸和非规范路径。Shader 声明名须与登记逻辑名一致。引擎 Phong 源址由其 CMake 构建输入提供，清单不准覆盖引擎身份。

不扫描 Content 把任意文件当 Shader；创建 Material 下拉列出已登记源码，未成功编译的项先编译才能创建。现有根材质 Shader 切换必须保存/取消草稿，首批通过创建选择新 Shader，未提供对已存在资产强行改名的入口。实例沿父根定位源码。提供可编译的项目示例供第一次使用。

## 2. 外部编辑器

窗口提供 Open Source、Recompile、状态和编译输出；源码路径只由登记源解析。VS Code 首选命令行显式 --reuse-window/--goto，不使用 --wait。Editor 本机路径可由 Editor.CodeExecutable 命令行选项配置，缺省平台探测常见安装目录；找不到明确报错并显示配置方法。打开源码不要求保存参数。根 `.shader` 可解析的错误位置提供 Open Error in VS Code；include、生成源码及无位置错误保留完整编译输出，不将任意日志路径直接启动。引擎源码是共享实现，日常新效果使用项目源码。当前重编译刷新已分配材质资产的场景槽位；ActorFactory 的内置共享缺省材质随应用构建重新加载。外部编辑器属于用户，关闭 Toy3d 不终止 VS Code。

## 3. 编译与候选接管

每次手动请求捕获 Shader 身份、活动 Material AssetId/会话代次、源码 hash；从磁盘保存内容编译。专用 Thread 同步运行已锁定 Toy3dShaderCompiler/工具链，限时 120 秒、输出 1 MiB，GT 每帧轮询完成后 join。一时只接收一个作业，退出 cancel/join。构建写 Saved 下随机请求目录，不覆盖旧产物；结果验证完整 ShaderMapEntry、逻辑名/Pass/profile/default permutation、源码和 include 当前 hash。依赖白名单仍由 compiler 执行。

ShaderMap 提供经验证数据到不可变 Program 的候选创建入口；候选不自动写进旧 key 的缓存。工作流在 GPU 校验和所有材质候选准备成功后替换自身已发布 Program 记录。RT 预检使用 Renderer 的设备/ShaderProgramCache、LocalVertexFactory 和当前场景 attachment，创建普通及双面 pipeline，无额外 submit/wait_idle；GT 只接收拥有的完成结果，不访问 RHI/Proxy。

当前 Forward Pass 的 View/Object/lighting ABI 必须与引擎提供的数据兼容，未知 Pass/Global 资源拒绝；材质资源支持已导入 Texture2D 的强 AssetRef 与普通 Sampler preset，缺失纹理和不受支持的比较采样器使候选接管失败。代码支持新数值/Color/Range Properties，删除/类型变化的覆盖按既有 orphan 规则保留。候选窗口 runtime 从当前草稿生成；更新 schema 不丢草稿和原资产撤销历史，结束手势后才接管。源码编译不保存 `.asset`。

MaterialLibrary 从已加载根/实例的已保存 DTO 准备整个候选配置图，临时后代按自己的本层覆盖解析；不读取窗口草稿。一次 FIFO 发布替换完整有效 Proxy 状态，逻辑对象和 Proxy 地址保持稳定，Component 槽位无需换引用。旧候选、编译失败、非法 schema/VF/pipeline/资源保留旧效果。MaterialAssignments 只保留场景 AssetRef 与命令记录，委托 Library prepare/publish/complete/discard。

材质槽更新经 `SceneInterface::update_primitive_materials` 更新现有 StaticMeshSceneProxy 的材质列表，保留几何资源与 HitProxy 身份。不得用末个 Primitive 的 Remove/Add 模拟纯材质切换，因为 Remove 会终止网格资源生命周期。旧材质引用在更新后的 FIFO 保活命令及版本 owner 中保留，资源 release 仍只在既有 drain 安全点执行。

接管在同一次 GT tick 内执行：准备窗口与 Library 全部候选 → 暂时发布完整共享配置图 → 再次核对根源码/include hash并原子保存当前请求记录 → 提交 Program 和 Library 配置 → 切换窗口 schema/runtime。保存记录或 hash 核对失败时，在本帧 Draw 入队前用同一 FIFO 完整恢复旧配置；旧参数/纹理由发布命令保活。成功后窗口保留草稿与撤销历史，源码编译不保存材质资产。

新 Program 当前 Editor 会话有效；成功接管后保存当前请求目录定位记录，重开 Editor 重新加载并重新 GPU 验证。生成产物不提交，缺少/损坏产物提示重新编译。普通帧不 flush，编译线程和 runtime 生命周期由同一 composition root 显式收尾。

请求目录使用 `Saved/requests/<随机 AssetId>/`，源码逻辑身份只在当前定位记录的 hash 目录出现，避免在完整产物和临时工作路径重复堆叠 hash。Compiler 每个独占 stage 工作目录内使用固定 `input.hlsl`/`output.spv`，完整 compile key 仍在产物中。共享文件系统支持 Windows 长路径，外部编译器的路径能力仍独立于该支持；外部工具拒绝路径时明确报告编译失败。

## 4. 平台与验证

公共候选流程不加后端判断：Vulkan、D3D11 FL11_0/SM5、D3D12 和移动 Vulkan 使用同一 Program/VF/pipeline 语义；首轮外部 compiler 仅有 compile-vulkan，未实现 target 明确拒绝，不声称已支持 D3D 外部编译。Windows Vulkan 实测，POSIX进程/VS Code定位单独核查。

验证包括真实 compiler 成功/失败、项目与引擎 include/逃逸/循环、相同 key 新 Program、不同参数 schema/orphan、旧会话与旧请求拒绝、输出容量/取消退出、场景赋值刷新、ShaderMap 完整验证、GPU 预检失败保留旧对象。所有测试文件写 build 隔离目录，不修改用户 asset。

## 5. 使用入口

1. Content Browser 空白区域右键或 File → Create Asset → Material，选择 `Project/Surface/Painted`，点击 Compile Shader；验证成功后 Create。
2. 双击材质，点击 Open Source。示例源码为 `project/shader/painted.shader`，项目共享函数为 `project/shader/include/project_common.hlsli`。
3. 在 VS Code 修改 Properties/HLSL，保存文件，再回材质窗口点击 Recompile。已赋值的场景槽位在新候选验证成功后刷新；未保存参数草稿仍留在材质窗口。
4. 新源码需在 `project/config/shader_sources.txt` 显式登记逻辑名与相对路径，重开 Editor 读取清单。源身份与 `.shader Shader` 声明名一致。

Windows 缺省检查 LocalAppData/Programs、用户目录的 Program Files 和系统 Program Files 中的 Microsoft VS Code/Code.exe；显式配置优先。VS Code 未安装于这些目录时使用 `Toy3dEditor.exe --Editor.CodeExecutable="D:/Apps/Microsoft VS Code/Code.exe"`。直接启动 Code.exe，不通过 shell 或 code.cmd。当前采用手动编译；参数保存后通过 MaterialLibrary 发布到场景和后代。完整材质球预览及缩略图刷新继续归 M6。
