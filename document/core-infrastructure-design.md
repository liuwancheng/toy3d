# Toy3d 共享文件系统设计

## 1. 文档状态与决策摘要

本文定义 Toy3d 跨 runtime、editor、tools 的共享文件系统 contract，并取代此前以“`VirtualFileSystem` 将虚拟路径解析为物理路径”为中心的设计。现有 `Toy3dFileSystem` 阶段 A/B 与 ShaderCompiler 迁移成果继续保留，但只视为迁移基础，不视为最终抽象。

核心决策如下：

1. 文件系统只负责路径、字节、目录和存储访问，不负责 Asset ID、资源导入、Shader include 白名单、Cook 规则或缓存 key。
2. 上层统一使用 `FileSystem` 和 `VirtualPath`；虚拟路径解析结果是“存储后端 + 后端内相对路径”，不是物理路径。
3. `PlatformFile` 只服务 host 物理文件系统和必须使用物理路径的工具场景；普通 runtime/editor 业务不得先 `resolve()` 再绕过 VFS。
4. `FileStore` 是可挂载的存储后端 contract。首个实现为 `DirectoryFileStore`，未来可增加 `MemoryFileStore`、`PackageFileStore` 和移动端只读 store，而不改变调用方 API。
5. 大文件和局部读取通过 RAII `FileHandle` 完成；整文件读写是建立在 handle 之上的便利 API，不再是唯一能力。
6. composition root 创建服务并注册 mount；library core 不提供不可替换的全局单例。
7. v1 保持同步 I/O。未来异步层组合 `read_at()` 与共享 task system，不在本轮把线程池或 coroutine 固化进文件系统。
8. UTF-8 有效性检查复用 `Toy3dText`；FileSystem 自行检查路径专有的 NUL、segment 与 mount 规则，不在多个 `.cpp` 中保留重复 UTF-8 解码器。

## 2. 设计来源与取舍

本方案参考 UE4.27 的 `IPlatformFile`、`IFileHandle`、Pak/Sandbox 等可替换后端思想，也参考 Flax 将基础文件/stream 能力保持精简、把 Content/Asset 语义放在更高层的边界。Toy3d 不直接复刻二者：

| 来源 | 吸收 | 不采用 |
| --- | --- | --- |
| UE4.27 | 底层平台文件接口、RAII 文件句柄、存储实现可替换、package 不泄漏给调用方 | 全局 `FPlatformFileManager`、任意 wrapper chain、历史兼容层、过早引入完整 Pak/IoStore 体系 |
| Flax | 小而直接的 file/stream contract、文件系统与 Content/Asset 分离、平台实现留在底层 | 静态全局入口、用 host path 作为所有模块的共同身份 |
| Toy3d | 强类型虚拟/物理路径、显式注入、mount 安全边界、稳定错误模型 | 将虚拟路径永久等同于磁盘路径，或让业务模块各自封装 `fstream`/`std::filesystem` |

Toy3d 最终采用三层而不是单一大接口：

```text
runtime / editor / tools domain services
                 |
                 v
        FileSystem (virtual namespace)
          | mount routing + policy
          v
        FileStore (relative namespace)
          |-- DirectoryFileStore
          |-- MemoryFileStore             [future]
          |-- PackageFileStore            [future]
          `-- MobileAssetFileStore        [future]
                    |
                    v
          PlatformFile (host filesystem)
                    |
                    v
             NativePlatformFile
```

`FileSystem` 与 `FileStore` 是调用方稳定边界；`PlatformFile` 是实现后端及 host tool 的底层边界。这样既保留当前实现价值，也避免 package/mobile 到来时推翻 `resolve -> PhysicalPath` contract。

## 3. 用例与非目标

### 3.1 必须支持的用例

- runtime 从 `/Engine`、`/Project`、`/Plugin/<Name>` 读取资源和 ShaderMap；
- editor 读取工程内容，并向 `/Saved`、`/Temp` 或明确可写的项目目录输出派生数据；
- ShaderCompiler 读取虚拟 Shader source，同时使用物理路径发现/启动外部 compiler；
- Cook 工具遍历输入、写 staging、原子发布输出，并验证大小写冲突；
- 单元测试注入内存或故障后端，不访问真实工程目录；
- 未来从 archive/package、Android APK assets、Apple bundle 或远程预热后的本地缓存读取文件；
- 大文件按区间读取，避免所有调用方都先分配完整 `vector`；
- 错误携带稳定 code、操作和安全的路径上下文，可转换为日志或领域 diagnostic。

### 3.2 明确非目标

- 不在文件系统中实现 Asset Registry、Asset ID、资源依赖图、import/cook 规则；
- 不在文件系统中定义 Shader include 白名单、ShaderMap key 或 publication 完整性规则；
- 不承诺跨多文件事务；“一个完整 ShaderMapEntry 可见”仍由 Shader 领域层负责；
- v1 不实现网络文件系统、文件监听、通用压缩、加密、memory mapping 或异步调度器；
- 不允许 runtime 用任意物理绝对路径加载产品资源；
- 不把 `std::filesystem::path`、Windows wide string 或平台句柄暴露到公共跨模块 API。

## 4. 路径与命名空间

### 4.1 `VirtualPath`

`VirtualPath` 是上层文件身份，规则固定且与 host OS 无关：

- UTF-8，使用 `/`，必须以 `/` 开头；根路径为 `/`；
- 禁止空 segment、`.`、`..`、反斜杠、NUL 和控制字符；
- 不接受 drive、UNC、URI、CWD relative path；
- 规范化只做语法规范化，不悄悄修正非法输入；
- 身份比较始终大小写敏感，即使当前后端是 Windows；
- `join()` 只接受已验证的相对 segment/path，不能通过字符串拼接绕过校验。

推荐顶级命名空间：

```text
/Engine                 引擎随产品发布的只读内容
/Project                当前项目内容；runtime 通常只读，editor/cook 可按配置写
/Plugin/<PluginName>    插件独立内容，插件卸载不影响其他 namespace
/Saved                  日志、缓存、ShaderMap、用户生成的持久数据
/Temp                   当前进程或任务拥有的临时数据
/User                   明确授权的用户内容；默认不挂载
```

当前宿主另挂载 `/Engine/Config`、`/Project/Config` 读取引擎与项目配置，Editor 创作实例另有只读 `/Editor/Resources`。这些 mount 是宿主策略而非 FileSystem 硬编码；对应物理目录和部署边界见 [资源目录设计](resource-directory-design.md)。

目录只是 namespace contract，不硬编码到共享 library。每个 executable 的 composition root 根据部署配置挂载。Asset 系统可以用自己的 `AssetId` 或逻辑 URI，但只能通过领域 service 转成 `VirtualPath`，不能反过来让文件系统理解 Asset。

### 4.2 `PhysicalPath`

`PhysicalPath` 表示 host path，仅允许出现在以下边界：

- `PlatformFile` 与 `DirectoryFileStore` 配置；
- executable/tool composition root；
- 外部 process executable、working directory 和 toolchain bundle；
- 平台适配与诊断的受控内部信息。

它持有 UTF-8 文本，并在 `NativePlatformFile` 内转换为 native path。它不隐式转成 `VirtualPath`，也不作为 runtime asset identity。Shipping 日志默认不得输出完整物理路径。

### 4.3 后端相对路径

`StorePath` 是 `FileSystem` 内部解析产生的、相对某个 `FileStore` 根的规范路径。它不能以 `/` 开头，也不能含 `..`。调用方不能自行构造 `(store, StorePath)` 来绕过 mount 权限。

## 5. 公共接口

以下签名表达 contract 形状，不要求本轮立即按字面实现全部 API。命名可在实现批次中微调，但职责不得重新混合。

### 5.1 文件句柄

```cpp
enum class FileOpenMode
{
    Read,
    WriteNew,
    WriteTruncate,
    ReadWrite
};

class FileHandle
{
public:
    virtual ~FileHandle() = default;

    virtual FileResult<std::uint64_t> size() const = 0;
    virtual FileResult<std::size_t> read(
        Span<std::uint8_t> destination) = 0;
    virtual FileResult<std::size_t> write(
        Span<const std::uint8_t> source) = 0;
    virtual FileResult<std::uint64_t> tell() const = 0;
    virtual FileStatus seek(std::uint64_t offset) = 0;
    virtual FileResult<std::size_t> read_at(
        std::uint64_t offset,
        Span<std::uint8_t> destination) const = 0;
    virtual FileStatus flush() = 0;
    virtual FileStatus close() = 0;
};
```

设计约束：

- handle 独占 native/archive handle，析构执行 best-effort close；需要可靠发布的写入方必须显式检查 `flush()` 和 `close()`，禁止裸句柄泄漏；
- `close()` 幂等；关闭后的其他操作返回 `InvalidState`，不能访问已经释放的 native/archive handle；
- `read`/`write` 是顺序 stream 的基础能力；`seek` 与 `read_at` 是否高效或可用由 capability 表达；
- `read_at` 不改变隐式 cursor，便于支持并行 range I/O 和未来异步调度；不支持时必须返回 `Unsupported`；
- EOF 不是错误：到达 EOF 的读取成功返回 0，跨越 EOF 可成功短读；其他 I/O 错误必须失败。整文件 helper 必须循环处理短读/短写；
- 只读 handle 的写入返回 `AccessDenied`，不做无操作成功；
- v1 不要求 `flush()` 等于断电持久化。需要 crash durability 的调用方必须使用后续明确的 durability contract，不能从“原子 rename”推断出来。

若项目尚无稳定 `Span`，首个实现可使用 `(data, size)`，但不得让该局部需求顺手固化一个未经设计的全项目容器。

### 5.2 `PlatformFile`

`PlatformFile` 面向 host 物理路径，提供：

- `stat/open/create_directories/enumerate/remove/rename_no_replace/replace`；
- `absolute/lexically_normal/canonical/parent/join_relative`；
- 可选 capability 查询，如 atomic replace、symlink、case-sensitive lookup；
- `read_binary/read_text_utf8/write_binary` 等便利函数可以保留，但实现必须基于稳定的 handle/primitive contract。

`PlatformFile` 不理解 mount、asset、package 或虚拟路径。`NativePlatformFile` 是默认实现，内部集中处理 `std::filesystem`、Win32/POSIX API、native error 映射和 UTF-8 转换。

### 5.3 `FileStore`

```cpp
struct FileStoreCapabilities
{
    bool writable = false;
    bool enumerable = false;
    bool supports_seek = false;
    bool supports_read_at = false;
    bool supports_atomic_rename = false;
};

class FileStore
{
public:
    virtual ~FileStore() = default;

    virtual FileStoreCapabilities capabilities() const = 0;
    virtual FileResult<FileStat> stat(const StorePath& path) const = 0;
    virtual FileResult<std::unique_ptr<FileHandle>> open(
        const StorePath& path,
        FileOpenMode mode) = 0;
    virtual FileResult<std::vector<DirectoryEntry>> enumerate(
        const StorePath& path) const = 0;
    virtual FileStatus create_directories(const StorePath& path) = 0;
    virtual FileStatus remove_file(const StorePath& path) = 0;
    virtual FileStatus remove_empty_directory(const StorePath& path) = 0;
    virtual FileStatus rename_no_replace(
        const StorePath& source,
        const StorePath& destination) = 0;
    virtual FileStatus replace(
        const StorePath& source,
        const StorePath& destination) = 0;
};
```

`DirectoryFileStore` 持有 `PlatformFile&` 与一个 canonical physical root，并负责确保任何操作都留在 root 内。package/mobile store 没有物理路径，也无需伪造一个路径来满足接口。

首批只实现 `DirectoryFileStore`。下列后端仅在真实用例出现后单独设计和实现：

- `MemoryFileStore`：测试与 compiler-owned generated overlay；
- `PackageFileStore`：Cook 后只读 package，可提供压缩块与校验；
- `MobileAssetFileStore`：Android AssetManager/Apple bundle；
- `CacheFileStore`：若未来需要，只负责存储机制，不包含领域 cache key/eviction policy。

### 5.4 `FileSystem`

`FileSystem` 是大多数模块唯一需要看到的入口，接收 `VirtualPath` 并执行 mount routing、权限检查和后端调用：

```cpp
class FileSystem
{
public:
    FileResult<FileStat> stat(const VirtualPath& path) const;
    FileResult<std::unique_ptr<FileHandle>> open(
        const VirtualPath& path,
        FileOpenMode mode);
    FileResult<std::vector<std::uint8_t>> read_binary(
        const VirtualPath& path) const;
    FileResult<std::string> read_text_utf8(
        const VirtualPath& path) const;
    FileStatus write_binary_atomic(
        const VirtualPath& path,
        Span<const std::uint8_t> bytes,
        FilePublishMode mode);
    FileResult<std::vector<DirectoryEntry>> enumerate(
        const VirtualPath& path) const;
};
```

整文件 helper 必须有显式最大字节数或使用系统统一上限，避免损坏/恶意文件造成无界分配。文本 helper 只验证 UTF-8，不做平台 newline 转换。

`write_binary_atomic()` 是单文件基础设施：在同一 store/父目录创建唯一 staging file，写完并关闭后 rename/replace。它不扩展成多文件 transaction；ShaderMap/package 的 manifest 顺序、完整性校验和目录 publication 仍属于领域 storage。

## 6. Mount 模型与查找语义

### 6.1 Mount descriptor

```cpp
struct FileMountDesc
{
    VirtualPath virtual_root;
    std::shared_ptr<FileStore> store;
    StorePath store_root;
    MountAccess access = MountAccess::ReadOnly;
    bool allow_enumeration = false;
    int priority = 0;
    std::string debug_name;
};
```

- longest complete-segment virtual root 优先；`/ProjectA` 不能匹配 `/Project`；
- 同一 virtual root 可注册多个只读层，按 `priority` 降序查找；priority 相同拒绝注册，避免依赖注册顺序；
- `NotFound` 才继续查询较低层；`AccessDenied`、`InvalidData`、`IoError` 等立即返回，不能用 fallback 掩盖故障；
- 一个 overlay group 最多有一个 writable layer；写入、创建、rename 和删除只进入该层；
- v1 不提供 tombstone。删除只删除 writable layer 中的对象，不能隐藏 lower read-only layer；需要 hide/whiteout 时随 package patching 单独设计；
- 跨 store rename 返回 `CrossDevice` 或 `Unsupported`，不会偷偷退化为 copy + delete；
- enumeration 合并各层同名 entry，优先层胜出，并按 UTF-8 code unit 稳定排序，保证 Cook/test 可复现。

### 6.2 Mount 生命周期

composition root 完成注册后调用 `freeze()`。冻结后的 mount snapshot 不可变，可被任意读取线程共享。Editor 将来若需要插件动态加载，构建完整新 snapshot 后原子替换；已有 operation/handle 持有对应 `shared_ptr<FileStore>`，不会看到半更新表或悬空后端。

共享 library 不硬编码 `/Engine` 对应源码目录，也不从 CWD 猜测部署根。配置缺失或 path 未挂载返回可诊断错误，不允许 fallback 到项目根。

## 7. 分层边界与典型调用方

### 7.1 Runtime 与 RenderScene

runtime composition root 持有 `NativePlatformFile`、stores、`FileSystem` 以及使用它们的领域 service。RenderScene 不直接读取 Shader 字节路径；它依赖 Shader/RenderCore 提供的 `ShaderMapProvider`。这既避免 RenderScene 理解部署布局，也不让文件系统吸收 Shader policy。

### 7.2 Editor 与 Asset/Cook

Editor/Cook 使用相同 `FileSystem` contract，但拥有不同 mount access。Asset 层负责 source asset、imported artifact 与 Asset ID 的映射；Cook 层负责目标 profile、package layout、hash 和 publication。文件系统只提供读取、遍历和可靠的基础写入原语。

### 7.3 ShaderCompiler

- Shader source 使用 `ShaderSourceProvider`，其 VFS 实现只暴露允许的 virtual roots；
- include 白名单、dependency graph、cycle/depth 与 content hash 是 Shader 领域规则；
- DXC executable/toolchain bundle 明确使用 `PhysicalPath` 与 `PlatformFile`，不放进 runtime VFS；
- `ShaderMapStorage` 组合文件原语实现目录级 publication，文件系统不理解 `ShaderMapEntry`。

### 7.4 日志、配置与其他基础设施

日志系统初始化可能早于完整 VFS，因此 composition root 可给日志 sink 注入一个受限的 `PlatformFile`/`DirectoryFileStore` 或已启动的最小 `/Saved` mount。Logger 不应反向成为 `Toy3dFileSystem` 依赖；文件错误由调用方决定如何记录。

`ConfigManager` 等历史代码若只是业务配置策略，应依赖 `FileSystem` 读取内容，不再直接使用 `fstream`。文件监听属于独立共享基础设施方案，不能顺手塞进 `FileSystem`。

## 8. 所有权、线程与异步演进

- composition root 创建 `PlatformFile`，再创建 stores 和 `FileSystem`；销毁顺序相反；
- `FileSystem` snapshot 以 `shared_ptr<FileStore>` 保证 operation/handle 生命周期；业务对象只持有 `FileSystem&` 或领域接口；
- 冻结后的查找、stat、open 和只读操作可并发；后端必须声明并满足自己的并发 contract；
- 带 cursor 的 `read/write/seek/tell` 不允许并发调用；支持 `read_at` 的只读 handle 必须允许并发 `read_at`；
- 同一路径写/rename 冲突不由进程内全局 mutex 假装解决，调用方通过唯一 staging、no-replace publication 和领域 key 协调；
- v1 所有方法是同步的，调用方不得在 render thread 执行不可控磁盘读取；
- 未来 `FileIoDispatcher` 接收 store/handle、offset、buffer ownership、priority 和 cancellation token，在 task system 上优先调度 `read_at()`；不支持 range I/O 的后端由 dispatcher 独占 handle 并顺序读取。同步 handle contract 足以支撑该演进，不需要现在引入文件系统私有线程池。

## 9. 错误模型

文件 API 不以异常表达预期失败，返回 `FileStatus` 或 `FileResult<T>`。`FileErrorCode` 至少包含：

```text
NotFound, AlreadyExists, AccessDenied, ReadOnly,
InvalidPath, InvalidData, InvalidState, OutsideRoot, NotDirectory, IsDirectory,
NotEmpty, CrossDevice, TooLarge, Busy,
Unsupported, Cancelled [future async], IoError
```

约束如下：

- `exists()` 只能把 `NotFound` 转成 false，其他错误必须保留；
- error 保存稳定 code、operation、virtual path、backend debug name、可选 platform code；
- physical path 仅在 trusted editor/tool diagnostic 中附加，shipping message 默认脱敏；
- 不在底层记录日志，避免重复日志、初始化环和测试噪声；
- `FileResult<T>` 当前可作为局部类型继续使用；抽象全项目 `Result<T, E>` 需要独立方案；
- 所有后端统一以成功的 0-byte read 表达 EOF，不能另行映射为 `IoError`。

## 10. 平台与存储差异

### Windows

- `NativePlatformFile` 内部使用 wide API；公共路径仍为 UTF-8；
- 明确处理 sharing mode、长路径、drive/UNC 和 reparse point；
- no-replace rename 与 replace 分开实现，不依赖 `std::filesystem::rename` 的模糊覆盖语义；
- Windows 的大小写不敏感不改变 `VirtualPath` identity。mount/cook 时扫描并拒绝仅大小写不同的冲突。

### macOS/Linux

- 不能假定 volume 一定大小写敏感；使用相同的虚拟路径 collision 规则；
- rename 原子性只在同一 filesystem 范围内承诺；
- symlink 必须按 `DirectoryFileStore` policy 检查，不能只做 lexical prefix 比较。

### Android/iOS

- APK assets/bundle 可能不可枚举、seek 代价较高或没有稳定物理路径；由只读 `FileStore` capability 准确表达；
- backend 不支持的操作返回 `Unsupported`，不能无操作成功；
- Cook/runtime 必须根据目标 profile 验证所需文件能力，不能按桌面实现推断移动端可用性。

### Package

`PackageFileStore` 将来负责 entry index、block 读取、压缩/校验等机制；package 格式、版本、签名与 patch 策略需另立设计。调用方仍通过 `FileSystem::open/read`，不得依赖 archive offset 或 native descriptor。

## 11. 安全边界

- 所有来自配置、资源或用户输入的 virtual/store path 都必须 parse 后使用；禁止字符串 prefix 安全检查；
- `DirectoryFileStore` 注册时 canonicalize root，访问时逐段处理 symlink/reparse point，并验证最终目标仍在 root；
- mount 分别声明 read/write/enumerate 权限；只读层即使底层目录可写也不得写入；
- 递归删除不进入通用 `FileSystem` 高频 API。测试清理和领域 owned staging cleanup 必须携带明确 owner/root guard；
- atomic helper 只删除本次创建并拥有的 staging 对象；不清理未知文件；
- mount 不接受空 root、filesystem root、CWD 猜测或未解析环境变量作为可写/删除边界；
- package/mobile backend 必须校验 entry path，拒绝 archive traversal、重复规范路径和大小写碰撞；
- Shipping build 的 diagnostics 不泄漏 toolchain、用户名或本机绝对目录。

## 12. 目录与 CMake 目标

建议在接口落地时渐进整理为：

```text
engine/core/file_system/
|-- file_error.h/.cpp
|-- file_handle.h
|-- file_path.h/.cpp              # PhysicalPath、VirtualPath、内部 StorePath
|-- platform_file.h
|-- native_platform_file.h/.cpp
|-- file_store.h
|-- directory_file_store.h/.cpp
|-- file_mount.h/.cpp
|-- file_system.h/.cpp
`-- atomic_file_writer.h/.cpp
```

首阶段仍维持一个 `Toy3dFileSystem` 静态库，避免接口演进期拆成多个微型 target：

```text
Toy3dFileSystem
    <- Toy3dRuntime
    <- Toy3dShaderCompilerCore
    <- future Toy3dAsset / Toy3dCook
```

`Toy3dFileSystem` 只依赖 C++17 与必要平台 API，不依赖 logging、runtime、Shader、RHI、editor 或第三方业务库。未来 package 实现若引入压缩库，应作为独立 target 依赖核心接口，不能让原生目录访问被迫链接 package 依赖。

## 13. 测试矩阵

`Toy3dFileSystemTests` 按 contract 而非实现细节组织，同一组 store contract tests 应可运行于 `DirectoryFileStore` 和未来 `MemoryFileStore`：

- path parse/join：UTF-8、空路径、`.`/`..`、separator、drive/UNC、边界 segment；
- handle：空文件、顺序/range read、seek、EOF/短读、越界、flush、模式权限、大文件 size；
- directory store：root containment、symlink/reparse escape、read-only、enumeration；
- mount：longest segment、同 root priority、错误 fallback、唯一 writable layer、freeze；
- overlay：高层覆盖、lower fallback、合并枚举、无 tombstone 删除语义；
- deterministic behavior：跨平台排序、大小写 collision、UTF-8 filename；
- publication：create-new、replace、目标已存在、同 key 并发、staging cleanup ownership；
- fault injection：open/read/write/flush/close/rename/enumerate 每个阶段失败和短 I/O；
- concurrency：冻结 snapshot 并发 lookup、同 handle 并发 `read_at`、mount snapshot 替换；
- security：恶意 store entry、absolute-path leak、递归删除 guard；
- platform：Windows sharing/reparse/long path，macOS/Linux symlink/rename，未来移动端 capability；
- integration：Shader source provider、ShaderMap publication、runtime asset load、Cook reproducibility。

纯 mock 不能替代 native integration test。每个支持平台至少运行 native directory store contract；package/mobile backend 出现后必须在对应目标或平台增加同级验证。

## 14. 分阶段迁移

迁移必须保持小批次可构建，不建立长期双轨正式入口。

### 阶段 0：冻结当前方向

- 保留已经完成的 `PhysicalPath`、`VirtualPath`、错误模型、`NativePlatformFile` 和 ShaderCompiler 注入成果；
- 当前 `VirtualFileSystem::resolve() -> PhysicalPath` 只作为过渡实现，不再扩展为公共业务依赖；
- 新调用方不得直接依赖旧 runtime `FileSystem` 单例。

### 阶段 1：补齐 handle 与物理层 contract

- 为 `PlatformFile` 增加 RAII handle 和 range I/O；整文件 helper 改为基于 handle；
- 补齐短读/短写、文件大小上限、replace/no-replace 和 capability；
- 运行 native contract 与 fault-injection tests。

完成条件：ShaderCompiler 现有用例不回退到 `fstream`，所有受影响测试通过。

### 阶段 2：引入 `FileStore` 与新 `FileSystem`

- 实现 `DirectoryFileStore`，把现有 mount physical root 安全逻辑迁入 store；
- mount table 改为持有 store，不再把物理路径作为 resolve 的公共结果；
- 添加 overlay、权限、枚举和 snapshot tests；
- 提供短期内部 adapter 承接现有 `VirtualFileSystem` 调用，但不暴露给新模块。

完成条件：核心 VFS 测试全部通过，普通调用方无需 `PhysicalPath`。

### 阶段 3：迁移 runtime composition root

- 在 runtime/editor composition root 创建 `/Engine`、`/Project`、`/Saved`、`/Temp` mounts；
- RenderScene 经 Shader/RenderCore provider 获取 shader，不直接访问路径；
- 迁移 `ConfigManager`、必要日志路径和资源读取；
- 删除 `ENGINE_ASSET_ROOT` 运行时发现逻辑、CWD fallback 和旧的 runtime `FileSystem` 单例。

完成条件：旧单例无生产调用且从 target 中删除；Editor 在部署目录而非源码绝对路径下可运行。

### 阶段 4：收敛 Shader 与工具

- 提供基于新 `FileSystem` 的受限 `ShaderSourceProvider`；
- `ShaderMapStorage` 复用单文件原子原语，但保留目录级领域 publication；
- 明确 toolchain 物理路径是允许的 host-tool 例外；
- 删除生产代码中残留的重复 `fstream/std::filesystem` I/O。

完成条件：ShaderCompilerCore 与 runtime 均不依赖旧 VFS 物理 resolve，真实 DXC 与 fault-injection 测试通过。

### 阶段 5：按需求增加后端

仅当相应用例已确定时，分别设计和实现 `MemoryFileStore`、`PackageFileStore`、`MobileAssetFileStore` 与 `FileIoDispatcher`。每个增量必须通过相同 store contract tests，并明确 capability 降级；不得为某个后端修改上层业务语义。

## 15. 删除旧实现的条件

以下条件全部满足后，删除旧 runtime 文件系统与旧 `resolve -> PhysicalPath` 公共入口：

- runtime/editor/tools 的 composition root 均显式注入共享服务；
- 生产调用方只在外部 process、toolchain 和平台实现中使用 `PhysicalPath`；
- `/Engine`、`/Project`、`/Saved` 部署 mount 有集成测试；
- Shader source、ShaderMap、配置和现有 runtime asset load 已迁移；
- Windows x64 与 macOS x64/arm64 至少通过配置、受影响目标构建和文件系统测试；
- 未保留 CWD/root fallback、静默失败或无操作成功；
- 文档和 CMake 不再推荐旧入口。

## 16. 验收标准

- 上层相同代码可从目录、内存或未来 package store 读取文件，不需要得到物理路径；
- `PlatformFile`、`FileStore`、`FileSystem` 三层职责清晰且依赖单向；
- 小文件便利 API、大文件 range I/O、只读/可写 mount 和原子单文件发布具有稳定 contract；
- runtime、editor、tools 不通过全局单例共享文件系统；
- Asset、Shader、Cook 策略留在各自模块，只组合共享文件能力；
- path traversal、symlink/reparse escape、大小写碰撞、错误 fallback 和 staging ownership 有自动化测试；
- 未实现能力返回可诊断的 `Unsupported`，移动端与 package 不需要伪装成物理目录；
- 当前代码可按阶段迁移，每一阶段都能独立构建、测试并明确删除旧实现的条件。
