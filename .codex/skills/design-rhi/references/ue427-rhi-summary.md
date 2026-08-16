# UE4.27 RHI 精简参考

## 目的

本文从 `D:/ue4.27plus/Engine/Source/Runtime/` 提炼可供 Toy3d 使用的设计要素。它是架构参考，不是移植清单；Toy3d 最终边界必须同时适合 Vulkan、DirectX 11、DirectX 12 与移动端 Vulkan profile。

## 核心模型

UE4.27 把后端无关资源、设备创建、GPU 命令语义、命令调度和上层渲染设施分开：

| 层 | UE4.27 代表类型 | 职责 | Toy3d 借鉴点 |
|---|---|---|---|
| 公共定义 | `RHIDefinitions.h` | format、usage、load/store、access、capability | 枚举表达通用语义，不对应原生数值 |
| 资源句柄 | `FRHIResource` 派生体系 | 后端无关资源身份和引用生命周期 | 原生对象只存在于后端资源类 |
| 设备接口 | `FDynamicRHI` | 初始化、资源、view、shader、PSO 创建 | 与 draw/dispatch 命令分离 |
| 执行上下文 | `IRHIComputeContext`、`IRHICommandContext` | render pass、binding、draw、dispatch、copy、transition | 后端必须翻译的命令语义 |
| 命令前端 | `FRHICommandListBase` 派生体系 | 直接执行或录制、调度、上下文选择 | 属于可后置的多线程机制 |
| 上层设施 | `RenderCore` | shader、render resource、PSO cache、render graph | 不进入公共 RHI |

主要源码入口为 `RHI/Public/RHIDefinitions.h`、`RHIResources.h`、`DynamicRHI.h`、`RHIContext.h`、`RHICommandList.h`，以及 `RenderCore/Public/RenderResource.h`、`RenderGraph.h`。

## 可复用的设计要素

### 资源是抽象身份

`FRHIResource` 派生出 buffer、texture、shader、view、state、pipeline、fence 等类型。具体后端类拥有 `Vk*` 或 `ID3D*` 对象。引用释放不代表 GPU 已停止使用，因此后端还要负责 in-flight 延迟销毁。

### 创建与执行分离

`FDynamicRHI` 接近设备/工厂；`IRHICommandContext` 表达 GPU 工作。Toy3d 当前 `IDynamicRHI` 同时负责资源创建、render pass、binding、draw/dispatch 和 map/update，职责已明显过宽。是否立即拆成两个 C++ 类型可以讨论，但应先建立语义边界。

### Command List 不是原生 Command Buffer

`FRHICommandList` 是调用方与后端 context 之间的录制/调度层，还服务于 UE 的渲染线程、RHI 线程和并行翻译。Toy3d 第一阶段可只有一个同步 `RHICommandContext` 实现，但接口不能假定 context 永远是唯一全局对象。后续多线程只在 pass 之间展开：每个 pass 使用独立录制单元，单个 pass 内保持串行，再由调度层按依赖顺序汇总和提交；不复制 UE 面向 draw batch 的复杂并行翻译设施。

### PSO 汇总不可变绘制状态

`FRHIGraphicsPipelineStateInitializer` 聚合 shader、vertex declaration、blend、rasterizer、depth/stencil、primitive 和 render-target compatibility。Vulkan/D3D12 可创建原生 pipeline/PSO；D3D11 可拆成多个 shader/state object 并组合绑定。公共接口不暴露 pipeline layout 或 root signature。

### Binding 使用 shader 可见语义

UE4.27 context 主要按 shader stage 和 slot 设置 texture、sampler、SRV、UAV、uniform buffer，后端自行映射 descriptor 或 D3D slot。Toy3d 应保留逻辑 binding identity，并为 Vulkan、D3D11、D3D12 生成各自 target mapping；Vulkan set/binding 和 D3D12 root parameter 不应成为核心公共概念。

### Render Pass 表达附件作用域

`FRHIRenderPassInfo` 描述 color/depth/stencil attachment、load/store、resolve 和 subresource。Vulkan 映射原生 render pass/dynamic rendering，D3D12/D3D11 可用目标绑定、clear、resolve 模拟。D3D11 没有显式 store action，后端可自然保留或按 capability 处理 discard。

### 公共意图驱动后端同步

UE4.27 用 `ERHIAccess` 和 transition 表达用途变化与 pipeline 关系。Vulkan/D3D12 生成 barrier；D3D11 可维护逻辑状态、验证 hazard 并解除冲突绑定。公共层不应暴露 Vulkan stage/access mask，也不能因 D3D11 隐式状态而删除 access 语义。

### 能力差异必须可查询

Toy3d 不需要复制 UE 大量全局 capability，但需要小型 `RHICapabilities`，至少覆盖 compute、geometry/tessellation、UAV、indirect draw、format/MSAA、timestamp query、async compute。不支持时必须明确失败并记录原因，上层不得按后端名称分支。

## 三后端边界

| 公共语义 | Vulkan | D3D12 | D3D11 | 结论 |
|---|---|---|---|---|
| usage/access | barrier/layout | resource state | 隐式状态与绑定冲突 | 公共表达用途，后端决定同步 |
| graphics PSO | pipeline + layout | PSO + root signature | state object 组合 | 公共 PSO 可用，不暴露布局 |
| binding | descriptor set | heap/root binding | stage slot | slot/reflection 作为基线 |
| render pass | 原生/动态渲染 | API 或命令组合 | OM + clear/resolve | attachment 作用域均可实现 |
| recording | command buffer | command list | immediate device，由上层串行提交 | 不承诺同等并行能力 |
| async compute | 可选 queue | compute queue | 通常不可用 | capability-gated |
| bindless | 可选特性 | tier dependent | 不适合作为基线 | 扩展能力，不进最小核心 |

## 与 Toy3d 文档的关系

本文仅提供 UE4.27 参考事实。Toy3d 已确认的取舍、阶段范围和验收条件见 `toy3d-rhi-requirements.md`；当前代码问题见 `toy3d-rhi-current-review.md`。
## 不建议照搬

UE 的宏/容器/对象基础设施，RHI thread、task graph、command bypass、并行翻译，多 GPU、ray tracing、VRS，大量全局 capability，以及历史兼容产生的重复接口和默认空实现。这些机制服务于 UE 的规模和历史；Toy3d 应只吸收职责分层、公共语义、后端隔离、显式能力与生命周期约束。
