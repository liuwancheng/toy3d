# Core Process

## 1. 范围与目录

本方案承接已确认的材质源码迭代方案。`engine/core/process/` 的独立目标 `Toy3dProcess` 提供 `ProcessService` 和 `NativeProcessService`，用于 compiler 的 DXC/SPIR-V validator 和 Editor 的外部编译、VS Code 启动。只依赖 FileSystem 的 PhysicalPath、Text 和平台库；Shader 策略、源码白名单、工具链和请求身份仍归调用方。

首版不实现终端模拟、shell 脚本、远程进程、文件关联或通用异步任务框架。只启动明确的可执行文件，参数按数组传递，无 PATH/shell fallback；VS Code 的平台发现由 Editor 处理。

## 2. API 与所有权

```cpp
struct ProcessRunOptions {
    uint32_t timeout_ms = 30000;
    size_t maximum_output_bytes = 1024 * 1024;
    const std::atomic<bool>* cancel = nullptr;
};
struct ProcessResult {
    bool launched = false;
    int exit_code = -1;
    std::string output;
    ProcessError error = ProcessError::None;
    std::string message;
    bool output_truncated = false;
    bool succeeded() const;
};
class ProcessService {
public:
    virtual ~ProcessService() = default;
    virtual ProcessResult run(const PhysicalPath&, const std::vector<std::string>&,
                              const ProcessRunOptions& = {}) const = 0;
    virtual ProcessResult launch_detached(const PhysicalPath&,
                                          const std::vector<std::string>&) const = 0;
};
```

composition root 持有 NativeProcessService 并向 Editor 注入。Compiler 继续允许注入执行回调，未注入时使用局部 NativeProcessService，不新增单例。`run` 同步拥有启动的进程、输出 pipe 和平台句柄；返回前回收。输出达到容量后继续排空，设置 truncated，不因截断死锁。取消/超时终止拥有的进程组并回收，不终止其他进程。

`launch_detached` 只报告平台启动是否成功，不报告编辑器退出或文件打开成功；启动后外部编辑器不属于 Toy3d 关闭协议，无 stdout 捕获。所有路径和参数检查 NUL、UTF-8、数量/长度边界；Windows 使用 CreateProcessW 与正确的 argv 引号规则，隐藏编译器控制台、保留 GUI 窗口。POSIX 使用无 shell 的 argv 和 exec/spawn，分离启动由 exec 错误管道确认并回收中间进程。

## 3. 线程、退出与错误

Service 无可变全局状态，可并发调用；执行所有权局限每次调用。调用方持有 cancel 原子的寿命覆盖 run。Editor 使用已有 Thread 的专用有界编译执行路径，一次一个请求，不占据 Task Graph compute worker；退出先取消并 join，再销毁 service 和 runtime 候选。

启动、等待、I/O、取消、超时和退出码分开表达。工具非零退出不是启动错误，succeeded 同时要求启动成功、正常等待和 exit_code=0。调用方按现有 Logger/窗口呈现错误，不新增诊断系统。

Windows 使用 Job Object 约束拥有的编译进程树，超时/取消终止 Job，完成后关闭句柄；POSIX 使用私有进程组、poll 与 waitpid。macOS/Linux 共用 POSIX 实现，移动平台外部创作不是 runtime 必需功能。平台细节不进入业务头文件。

## 4. 迁移与验证

先登记共享目标和接口，再迁移 dxc_adapter/program_compiler 的默认执行路径，删除 `compiler/process_runner.h/.cpp`，不保留第二个正式入口。Editor 随后注入同一服务。测试用自有 helper 验证空参数、空格/引号/反斜杠/UTF-8、缺失可执行文件、退出码、超容量输出、超时/取消和子进程回收；compiler 原模拟测试与真实 Phong 编译必须通过。Windows 实测，POSIX 未构建时明确记录，不宣称跨平台实测通过。
