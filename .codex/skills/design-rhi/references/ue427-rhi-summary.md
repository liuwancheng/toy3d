# UE4.27 RHI：按需参考

只解释架构来源，不规定 Toy3d 当前接口或阶段。Toy3d 的选择以 document/rhi.md 为准；当前实现必须查代码，不能由 UE 类型名推测。

本机参考源码根 D:/ue4.27plus/Engine/Source/Runtime，若不存在就明确未核对。主要入口：

| UE 文件/类型 | 可借鉴职责 |
| --- | --- |
| RHI/Public/RHIDefinitions.h、RHIResources.h | 后端无关用途、格式、资源身份 |
| DynamicRHI.h / FDynamicRHI | 设备与资源创建 |
| RHIContext.h / IRHICommandContext | 命令、draw/copy/transition |
| RHICommandList.h | 命令前端与调度；复杂多线程机制不自动移植 |
| RenderCore/Public/RenderResource.h | 渲染资源初始化/释放边界 |
| RenderCore/Public/RenderGraph.h | 上层依赖组织，不属于 backend/native API |

可借鉴：创建/执行分离；资源引用释放与 GPU completion 分离；PSO 聚合不可变状态；公共 access 表用途、后端决定 barrier；render pass 表附件作用域；capability 约束差异。

Toy3d 保持自身 Device/Context/List/Viewport ownership、逻辑 binding 与 typed 参数边界。D3D11 隐式状态不意味着删除公共 access；Vulkan set/root signature 不成为业务身份。UE 全局 capability、RHI 线程、draw-batch 并行和对象系统都不因参考而成为本项目要求。
