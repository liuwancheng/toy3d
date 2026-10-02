# Core：共享能力与使用边界

## 定位与复用

Core 不依赖 runtime/editor，不承载业务策略。先找 target 与公共头，现有语义无法覆盖时才设计新能力；不能为业务闭环再写一套文件、线程、进程或哈希系统。

目录按功能组织，构建库按依赖边界组织；不是每个功能一个库。头路径相对于 engine/core，真实源码与生成规则集中在该目录的 CMakeLists.txt。

| target | 内容与目录 | 依赖边界 |
| --- | --- | --- |
| Toy3dCore | platform（平台定义、进程与桌面服务）、misc（UTF-8/SHA-256/enum flags）、file_system、logging、math、threading（含 task_graph）、reflection、serialization、image（格式/PNG） | 基础设施，不依赖资产、Shader、Runtime 或 Editor；GLM/spdlog 公共，stb/Threads 实现依赖 |
| Toy3dShaderFormat | shader/：跨 compiler/runtime 的格式、Binding、参数 ABI、内置 schema | 只依赖 Core；Editor 属性数据开关仅在此库定义，不反向依赖 compiler |
| Toy3dAssets | asset/：持久化/身份/索引/编辑；mesh、material、texture、scene、thumbnail 子目录 | 依赖 Core/ShaderFormat，yaml-cpp 私有；资产反射由独立 codegen 生成到 build |

Core 测试统一放 tests/，进程 helper 与故障注入测试也在此处。资产处理库见 [Assets](assets.md)，数学、线程及格式规则分别见主文档；不把业务策略压入 misc。

调用方链接所需库即可获得公共头路径；公共头统一用功能前缀，例如 `misc/utf8.h`、`image/pixel_format.h`、`threading/task_graph/task_graph.h`、`asset/mesh/static_mesh_asset.h`、`shader/shader_format_types.h`。不新增旧路径转发头或旧库别名。

```cmake
target_link_libraries(MyTool PRIVATE Toy3dCore) # 文件、进程、日志等共享能力
target_link_libraries(MyAssetTool PRIVATE Toy3dAssets) # 同时获得 Core/ShaderFormat
```

只合并编译单元与目录，不改变公共类型、资产 schema/ID、所有权、同步或错误语义。服务仍由 root 持有/注入，Core 不决定 Shader include 白名单、Asset 工作区布局或 RenderScene 生命周期。新增 target 必须表达独立依赖、构建开关或部署边界；独立 CLI/测试保留自己的入口。

## 平台定义

平台头由 `Toy3dCore` 提供；没有单独的 Platform target。调用方声明 Core 依赖并显式包含头，不能依赖预编译头或全局宏注入。

显式包含 `platform/platform_defines.h` 后，WITH_WIN、WITH_MAC、WITH_IOS、WITH_ANDROID、WITH_LINUX 均为 0/1，按编译目标恰有一个为1；未知 OS 编译报错。Windows 不含位数语义，旧 WITH_WIN64 已删除；TOY3D_ARCH_X64、TOY3D_ARCH_ARM64 独立描述架构，未知架构均为0，需要特定架构的调用方不能默认回退为 x64。交叉编译识别目标而非构建宿主，macOS universal 的每个编译 slice 各自识别架构。

原生 OS/架构宏只在这个头内使用；Android 在 Linux 前识别，Apple 使用 TargetConditionals 区分 macOS/iOS，其他 Apple 目标不冒充二者。平台宏全部已定义，调用方用 `#if` 判断值，不用 `#ifdef`/defined 判断是否存在，也不自行定义或覆盖。NOMINMAX、VK_USE_PLATFORM_WIN32_KHR、SYS_renameat2 等 SDK 配置/能力宏仍由具体平台实现检查，不能机械替换为 OS 判断。

```cpp
#include "platform/platform_defines.h"

#if WITH_WIN
#include <Windows.h>
#endif
```

平台宏只说明目标身份，不保证模块可用；Process、文件系统的非 Windows 分支保留 POSIX 实现。shader_toolchain_host_platform 当前识别 Windows/macOS/Linux 的 x64/arm64 组合，其他组合返回 unknown，discovery 再与 manifest 比较；尚缺对双方同为 unknown 的显式拒绝，不能把它当作受支持工具链。iOS/Android/Linux 的完整 runtime/window/backend 支持仍需对应实现与真实平台验证。修改此入口须检查独立包含、相关平台 API 分支、工具链 host key，以及 Runtime/Editor/工具调用方；不根据 Windows 构建通过声称其他平台可用。

## 文件系统

- VirtualPath 为 UTF-8、大小写敏感、以 / 为根；拒绝空段、点/父目录段、反斜杠、NUL/控制字符、盘符、UNC/URI。Windows 不改变逻辑大小写。
- PhysicalPath 只在平台根、外部进程、工具链边界；业务不能解析物理路径后绕 mount 权限。
- 分层 FileSystem → FileStore → PlatformFile；启动 add_mount 后 freeze，不依赖工作目录或静默回退。
- 最长完整路径段匹配；同根 priority 降序，相同 priority 拒绝；仅 NotFound 可回退，权限/损坏/I/O 立即失败，一个位置只一个 writable layer。
- 不提供 overlay tombstone；跨 store/device rename 明确失败，枚举稳定 UTF-8 排序。
- FileHandle RAII、close 幂等，关闭后 InvalidState；read/write 可短操作，EOF 成功零字节；read_at 不改 cursor，其并发能力受实现 contract 限制。发布检查 flush/close。
- read_binary/read_text_utf8 有界；write_binary_atomic 同父 staging 后 CreateNew/Replace 单文件发布，不保证断电持久性或多文件原子性；配对资产由 AssetPairStore 管理。

真实 FileHandle 接口使用指针/长度：read(uint8_t*, size_t)、write(const uint8_t*, size_t)、read_at(offset, uint8_t*, size_t) const；不要虚构 Span 重载。完整 mount/权限/短读/发布用例见 tests/file_system_tests.cpp。

## 平台服务与外部进程

平台外部操作统一声明于 `platform/platform_services.h`，实现在对应 cpp，归属 Toy3dCore；Windows 的 shell32/ole32 为该目标的 PRIVATE 依赖。文件承载进程执行、用户所有的 detached 程序和桌面目录打开，类型仍使用 ProcessService/NativeProcessService；文件读写、线程和 runtime 窗口行为各归原模块。

用户数据根查询由 platform/platform_services.h 的 user_data_directory() 提供，返回 FileResult<PhysicalPath>，不创建目录。Windows 使用 Known Folder LocalAppData；macOS 使用 HOME/Library/Application Support；Linux 使用绝对 XDG_DATA_HOME 或 HOME/.local/share。Editor 决定 Toy3d/Editor 子目录，Core 不持工程全局状态。

ProcessService 可注入，NativeProcessService 为本机实现、可并发调用；接收绝对 PhysicalPath 和 argv 数组，无 shell 求值/PATH 搜索，校验 UTF-8、NUL、输入上限。

run 同步拥有进程树，stdout/stderr 合流，有界收集且截断后继续排空；默认 timeout=30000 ms、输出=1 MiB，timeout 必须非零、捕获上限最多16 MiB，cancel atomic 的生命周期覆盖调用。当前 argv 最多256项、每项小于32768字节，Windows 还受整条 native command line 长度限制。超时/取消停止所属进程树并回收，失败保留 ProcessError/message。

launch_detached 启动用户所有 GUI，Toy3d 退出不杀它；成功只表示启动确认，不保证文件打开或 GUI 后续正常。Windows CreateProcessW/argv quoting/隐藏 console，run 用 Job 管理；POSIX 进程组/回收，detached 经 exec 确认。

Windows 在回收前显式关闭所属 Job，kill-on-close 终止关联树；未成功加入 Job 的 suspended root 单独终止。执行 deadline 之外最多等待2000 ms确认 root 退出，清理失败追加诊断并保留最初错误，未确认退出时 exit_code 保持 -1，不无限等待或声称回收成功。对应失败用例见 tests/windows_cleanup_tests.cpp。

最小片段：executable 是调用方已验证的绝对 PhysicalPath；真实签名见 platform/platform_services.h，完整失败用例见 tests/process_tests.cpp。

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

`open_directory_on_desktop(path, error)` 用系统文件管理器打开已有绝对目录，不执行 shell 文本、不提供文件读写或进程回收。Windows 使用 ShellExecuteW，macOS 复用 launch_detached 启动 `/usr/bin/open`，其他平台明确返回不支持；打开的界面归用户所有，成功仅表示请求被接受。函数可按需同步调用，不持有会话状态、不记录日志，调用方负责报告 error；Console 选择日志目录的策略仍留 Editor。空路径、相对路径、缺失目录和普通文件失败见 tests/process_tests.cpp。

## 其它共享能力与验证

### 日志分发

Logger 将同一事件分发到终端、滚动文件和可选 LogBuffer。LogBuffer 由启动入口创建并注入，不依赖 Editor/ImGui；记录拥有正文、时间、等级、线程和源码位置，按采集序号稳定排列。缓冲按条数和字节数有界，超长记录明确截断、淘汰计数可见，完整正文仍按文件滚动策略保留。只读快照不持锁绘制，过滤/清空由消费者决定，不修改文件或生产者等级。

默认保留最近10000条、16 MiB（记录元数据及文本字节，不含消费者持有的快照和容器开销）。快照共享不可变记录，消费者应替换旧快照，避免无限持有历史。文件输出沿用10 MiB/5份滚动备份，Warning及以上立即 flush，退出最终 flush；文件失败的那条正文可能未写入文件，Console 中仍保留并显示文件健康状态。

启动入口在业务初始化前创建 `std::make_shared<LogBuffer>()`，赋给 `LogConfig::memory_output` 后调用 `Logger::init(config, &error)`；Editor 将工程或用户 Saved 根注入 Engine。Editor/Game 日志用 `make_dated_log_file_name(role)` 生成，例如 `saved/logs/editor-2026-10-02_12-05-30-123-p37228.log`：本地启动日期时间、毫秒和进程号，不同并发实例不共写。业务继续使用 `TOY_LOG_ERROR("Material [{}]: {}", asset_id.hex(), error)`；UI 从 `buffer->snapshot()` 读取，不访问 spdlog sink 或设置 Logger 等级。接口见 logging/log_buffer.h、logger.h，完整失败和并发示例见 tests/logging_tests.cpp。

日志路径边界使用 UTF-8，文件名经 u8path 转为原生路径；Windows 构建统一启用 spdlog 的 SPDLOG_WCHAR_FILENAMES，sink 使用 path.native()，避免中文或非 BMP 字符在窄字符转换中丢失。该定义由 spdlog target 传播，调用方不能局部改变文件名 ABI。

文件创建、轮转或 flush 失败时保留其他输出，通过缓冲健康状态和 stderr 报警，不递归调用 Logger。init 返回 false 表示所请求输出没有全部成功，不代表剩余输出失效。生命周期由启动/退出入口持有：先初始化日志再初始化业务；工作线程停止后 flush/exit，Logger 的写入与 exit 互斥。Editor 注入会话缓冲，工具可只使用现有终端/文件。新增实现放 logging，仍属于 Toy3dCore，不增加库。

验证覆盖文件/缓冲同一事件、多线程与退出、条数/字节上限、轮转、初始化和运行中写入失败；Editor 的显示策略见 Editor 文档。

- Logger 现有全局入口由启动/退出链管理，这是现状，不允许据此增加单例；日志失败不影响必要资源释放。
- UTF-8 校验先于解析，限制注明字节/元素，不用 locale 隐式转换。
- SHA-256 表示内容/完整性，不能替代 AssetId。
- PixelFormat 是 core CPU 格式事实，native 映射在后端；块 pitch 向上取整，保留 PVRTC 最小块等限制，不能概括为 width×bytes_per_pixel。
- ImageCodec 做 CPU 转换；runtime Texture 不解码 PNG/JPEG，GPU-ready mip、导入策略和缓存见 Assets。

新增共享服务覆盖 ownership/线程/错误/平台/旧入口删除条件，以最小调用方迁移。核对绕接口、忽略失败、cwd 依赖和重复缓存。构建对应 target/测试；测试名/平台条件从 CMake 查。Process 覆盖 argv、非零退出、截断、超时/取消/detached；FileSystem 覆盖非法路径、mount/权限、短读、发布失败。
