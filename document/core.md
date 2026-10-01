# Core：共享能力与使用边界

## 定位与复用

Core 不依赖 runtime/editor，不承载业务策略。先找 target 与公共头，现有语义无法覆盖时才设计新能力；不能为业务闭环再写一套文件、线程、进程或哈希系统。

头路径相对于 engine/core，target 定义见其 CMake：

| 能力 | target | 公共入口 | 主要验证 |
| --- | --- | --- | --- |
| 文件系统 | Toy3dFileSystem | file_system/file_system.h、native_platform_file.h | tests/file_system_tests.cpp |
| 外部进程 | Toy3dProcess | process/process.h | process/tests/process_tests.cpp |
| 日志 | Toy3dLogging | logging/logger.h | 初始化/退出链与调用方 |
| UTF-8 | Toy3dText | text/utf8.h | tests/text_tests.cpp |
| SHA-256 | Toy3dHash | hash/sha256.h | 资产/Shader 摘要测试 |
| 格式/块布局 | Toy3dPixelFormat | pixel_format/pixel_format.h | tests/pixel_format_tests.cpp |
| PNG 编解码 | Toy3dImageCodec | image_codec/png_codec.h | Texture 导入和缩略图测试 |
| 数学 | Toy3dMath | math/math.h | [Math](math.md) |
| 线程/任务 | Toy3dThreading、Toy3dTaskGraph | threading/、task_graph/ | [Threading](threading.md) |
| 反射/序列化 | Toy3dReflection、Toy3dSerialization | reflection/type_registry.h、serialization/value_codec.h | [Assets](assets.md) |
| 资产读写/编辑 | Toy3dResource | asset/asset_pair_store.h、asset/edit_session.h | [Assets](assets.md) |
| CPU 资产模型 | Toy3dMeshDescription、Toy3dStaticMeshAsset、Toy3dMaterialAsset、Toy3dSceneData、Toy3dSceneAsset、Toy3dTextureAsset | 各同名目录公共头 | [Assets](assets.md) |
| 缩略图缓存 | Toy3dAssetThumbnail | asset_thumbnail/asset_thumbnail.h | [Assets](assets.md)、[Renderer](renderer.md) |

调用方只链接所需 target；平台/第三方细节留具体实现，服务由 root 持有/注入。Core 不知道 Shader include 白名单、Asset 工作区布局或 RenderScene 生命周期。

## 文件系统

- VirtualPath 为 UTF-8、大小写敏感、以 / 为根；拒绝空段、点/父目录段、反斜杠、NUL/控制字符、盘符、UNC/URI。Windows 不改变逻辑大小写。
- PhysicalPath 只在平台根、外部进程、工具链边界；业务不能解析物理路径后绕 mount 权限。
- 分层 FileSystem → FileStore → PlatformFile；启动 add_mount 后 freeze，不依赖工作目录或静默回退。
- 最长完整路径段匹配；同根 priority 降序，相同 priority 拒绝；仅 NotFound 可回退，权限/损坏/I/O 立即失败，一个位置只一个 writable layer。
- 不提供 overlay tombstone；跨 store/device rename 明确失败，枚举稳定 UTF-8 排序。
- FileHandle RAII、close 幂等，关闭后 InvalidState；read/write 可短操作，EOF 成功零字节；read_at 不改 cursor，其并发能力受实现 contract 限制。发布检查 flush/close。
- read_binary/read_text_utf8 有界；write_binary_atomic 同父 staging 后 CreateNew/Replace 单文件发布，不保证断电持久性或多文件原子性；配对资产由 AssetPairStore 管理。

真实 FileHandle 接口使用指针/长度：read(uint8_t*, size_t)、write(const uint8_t*, size_t)、read_at(offset, uint8_t*, size_t) const；不要虚构 Span 重载。完整 mount/权限/短读/发布用例见 tests/file_system_tests.cpp。

## 外部进程

ProcessService 可注入，NativeProcessService 为本机实现、可并发调用；接收绝对 PhysicalPath 和 argv 数组，无 shell 求值/PATH 搜索，校验 UTF-8、NUL、输入上限。

run 同步拥有进程树，stdout/stderr 合流，有界收集且截断后继续排空；默认 timeout=30000 ms、输出=1 MiB，timeout 必须非零、捕获上限最多16 MiB，cancel atomic 的生命周期覆盖调用。当前 argv 最多256项、每项小于32768字节，Windows 还受整条 native command line 长度限制。超时/取消停止所属进程树并回收，失败保留 ProcessError/message。

launch_detached 启动用户所有 GUI，Toy3d 退出不杀它；成功只表示启动确认，不保证文件打开或 GUI 后续正常。Windows CreateProcessW/argv quoting/隐藏 console，run 用 Job 管理；POSIX 进程组/回收，detached 经 exec 确认。

Windows 在回收前显式关闭所属 Job，kill-on-close 终止关联树；未成功加入 Job 的 suspended root 单独终止。执行 deadline 之外最多等待2000 ms确认 root 退出，清理失败追加诊断并保留最初错误，未确认退出时 exit_code 保持 -1，不无限等待或声称回收成功。对应失败用例见 process/tests/windows_cleanup_tests.cpp。

最小片段：executable 是调用方已验证的绝对 PhysicalPath；真实签名见 process/process.h，完整失败用例见 process/tests/process_tests.cpp。

```cpp
toy3d::NativeProcessService processes;
toy3d::ProcessRunOptions options;
options.maximum_output_bytes = 64u * 1024u;
const auto result = processes.run(executable, {"--version"}, options);
if (!result.succeeded())
{
    // 调用方记录 error、message、exit_code、output；不继续发布结果。
}
```

succeeded = launched && error=None && exit_code=0，不能只看 launched。路径白名单、编译策略、Editor 设置留业务层；同步编译使用专用线程，避免占用 TaskGraph worker。

## 其它共享能力与验证

- Logger 现有全局入口由启动/退出链管理，这是现状，不允许据此增加单例；日志失败不影响必要资源释放。
- UTF-8 校验先于解析，限制注明字节/元素，不用 locale 隐式转换。
- SHA-256 表示内容/完整性，不能替代 AssetId。
- PixelFormat 是 core CPU 格式事实，native 映射在后端；块 pitch 向上取整，保留 PVRTC 最小块等限制，不能概括为 width×bytes_per_pixel。
- ImageCodec 做 CPU 转换；runtime Texture 不解码 PNG/JPEG，GPU-ready mip、导入策略和缓存见 Assets。

新增共享服务覆盖 ownership/线程/错误/平台/旧入口删除条件，以最小调用方迁移。核对绕接口、忽略失败、cwd 依赖和重复缓存。构建对应 target/测试；测试名/平台条件从 CMake 查。Process 覆盖 argv、非零退出、截断、超时/取消/detached；FileSystem 覆盖非法路径、mount/权限、短读、发布失败。
