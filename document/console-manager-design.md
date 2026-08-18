# Toy3d 轻量 ConsoleManager 设计

## 1. 用例与非目标

`ConsoleManager` 是 runtime 内统一的字符串变量表，用于让 Config 文件、命令行参数和未来的 cmd 命令读写同一批变量。Window、Renderer 等 runtime 模块可通过全局入口查询变量，不需要逐层传递配置对象。

本阶段不实现 UE 的完整 CVar flags、自动静态注册、分线程镜像、持久化控制台历史、远程控制台或反射系统，也不把 `ConsoleManager` 提升为 runtime、editor、tools 共用的 `engine/core` 基础设施。

## 2. 目录与依赖

实现位于 `engine/runtime/config/console_manager.h/.cpp`，随 `Toy3dRuntime` 构建。依赖方向固定为：

```text
runtime modules -> ConsoleManager -> Toy3dFileSystem contract
```

`ConsoleManager` 只在启动加载 Config 时使用注入的 `FileSystem`，不持有文件系统引用，也不把 `FileSystem` 变成全局服务。

## 3. 接口与生命周期

`ConsoleManager::get_instance()` 提供 runtime 进程级全局入口。`Engine::pre_init()` 在创建 Window 和 Renderer 前按以下顺序初始化：

1. 从 `/Engine/config/engine_config.ini` 加载变量；
2. 应用命令行覆盖；
3. 后续 cmd 命令可继续通过 `set_value()` 覆盖。

覆盖优先级通过明确的调用顺序实现：

```text
调用方默认值 < Config < 命令行 < 运行时 cmd
```

本阶段不支持在运行期间重新加载 Config；避免低优先级来源覆盖已经生效的命令行或 cmd 值。

## 4. 所有权、线程与错误

- singleton 持有变量表，进程退出时由静态生命周期回收；不持有 Window、Renderer、FileSystem 或其他业务对象；
- 查询使用共享锁，写入和 Config 批量替换使用独占锁；返回字符串副本，不向调用方暴露容器引用；
- Config 缺失、路径非法、UTF-8 非法或 I/O 失败保留 `FileStatus`，由 composition root 记录诊断；
- 无效数值转换返回调用方提供的默认值；未知变量不会自动创建，只有 `set_value()` 和输入来源可以写入；
- `reset_for_tests()` 只用于测试隔离，生产初始化流程不得调用。

## 5. 平台与安全边界

所有平台使用相同的虚拟 Config 路径和变量命名。Config 只能通过已冻结、只读的 `/Engine` mount 加载，不接受 CWD fallback 或任意物理路径。命令行与未来 cmd 只修改变量值，不获得文件系统访问能力。

## 6. 测试与迁移

自动化测试至少覆盖 Config 虚拟路径读取、section key、CRLF、行尾注释、默认值、命令行覆盖、失败加载保留最后有效状态和测试重置。迁移完成条件为：

- 删除 `ConfigManager` 类型及 Window 构造函数中的配置参数；
- runtime 生产代码统一通过 `ConsoleManager` 查询变量；
- Config 资源随 Editor 部署，且部署内容与源文件一致；
- Windows Debug 的配置测试、`Toy3dEditor` 构建和全部 CTest 通过。
