# RHI：公共语义、GPU 生命周期与 Vulkan

## 定位与可实现性

公共接口 engine/runtime/drivers/rhi，原生实现 drivers/vulkan；都属于 Toy3dRuntime。RenderScene 不含 native 类型，RenderCore 的 Shader/Material/schema 不下沉 RHI。

| 基线 | 设计要求 / 当前边界 |
| --- | --- |
| Vulkan | 当前后端；移动 Vulkan ES3.1 profile 为 1.1 / SPIR-V 1.3 / 四个 sets |
| D3D11 | 设计评估 FL 11_0 / SM 5.0，不以 D3D10 降级；后端未实现 |
| D3D12 | 设计评估明确状态/descriptor/PSO；后端未实现 |

capabilities、limits、format support、profile 表达差异，Cook/runtime 验 required 集合，不能按开发机 GPU 提升 portable 基线。未实现语义明确 Unsupported，不能空操作成功。数学 convention 统一见 [Math](math.md)，Shader 不平台翻转。

## 设备、资源与 validation

RHIDevice 是公共创建前端，rhi_device.h 的非虚入口做共同 validation/owner/lifecycle 检查，再调用 backend *_impl；不能新增绕前端正式创建入口。资源/view/shader/pipeline 创建和 command recording 分层，后端 enums 集中映射，不按公共枚举数值强转。

- 明确 creator/CPU owner/GPU in-flight reference/destroyer/device/thread；跨 device 资源/列表非法，销毁前结束 GPU 使用。
- RHICPUAccess 为 None/Read/Write，不存在 ReadWrite 万能配置；CPU map/读回能力由资源用途、格式和支持路径验证。
- buffer structured stride 只属于 structured 用途；vertex stride/index format 由对应 binding 语义处理，不能混成一个创建字段。
- 只读格式化 buffer 使用 ShaderResource | TypedBuffer、structure_stride=0 和具名格式 view，绑定类型为 ReadOnlyTypedBuffer。Vulkan 对应 uniform texel buffer；验证 bufferFeatures、元素上限、texel/device offset 对齐及 sampled-resource/总资源预算；view 与 backing buffer 均保活至 completion。
- Texture 与 View 分开；usage/aspect/view format/mips/layers/sample count/附件组合一起验证。HDR color RT|SRV 和 depth DS|SRV 均需完整 format support；depth SRV 明确 Depth aspect，不把 stencil 混入。
- 创建时 initial data 的未支持路径返回 Unsupported；上传使用显式 context/list/submit，不能资源创建偷偷提交/等待。
- device cache 对 sampler/PSO 等不可变 descriptor 去重；key 无裸地址、canonical equality 处理 hash collision，single-flight 不缓存失败。shutdown gate 停新创建、等待已进入调用，再拆 native root。
- 公共结果保留 RHIErrorCode/message；包装成高层错误不能丢 DeviceLost/BackendFailure 分类。

## 录制、提交与资源状态

RHICommandContext 单线程录制，finish 后形成不可变 command list，持强引用/临时上传数据至 GPU completion；命令列表不是借用 caller 临时地址的脚本。

资源状态在 context 中维护 local initial/final state，**录制不修改已发布状态**；queue 按实际 submit 顺序校验并仅在成功后 commit。discard/abort/明确未提交不推进状态；不确定 submit 进入 terminal，不假装可重试。

一般 queue 是 device-level 提交；viewport frame 的 list 必须通过所属 viewport 协议，不绕 WSI 直接提交。CPU fence、queue submission、GPU completion、presentation 是不同事实，不能互相当安全证明。

transient uniform slice 表示 buffer/offset/size，context 拷贝 canonical bytes，completion 管页生命周期与对齐；不携带 generated C++ metadata。Vulkan 对齐为后端限制，D3D12 256-byte alignment 不改变 ABI；D3D11 可独立 buffer 降级，不把所有后端硬套动态 offset。

## Binding：逻辑与 native 分离

逻辑 group 表达身份、ownership、更新频率，不能等同 descriptor set：

| Group | Vulkan portable physical set |
| --- | --- |
| Global、View | 0 |
| Pass | 1 |
| Material | 2 |
| Object | 3 |

set 内按 logical group/resource class/parameter identity 紧凑分配，不预留 0/256/512/768 class-base。D3D11/D3D12 按 target/stage/register class 独立映射，跨 target parity 不比较 native slot 数字。

三层：完整 logical schema → Program active layout → target native mapping。BindingSet 是不可变、program-independent 的逻辑 group snapshot；绑定当前 Program 时验证/物化 active 子集，不让上层手工 native slots。typed parameters/encoder 在 RenderCore，RHI 接收 bytes 与公共资源值；[Shader](shader.md) 是 schema/ABI 权威。

## Viewport/WSI

RHIViewport 独占 frame begin/end-or-abort，一次 frame 恰好消费一次。FrameContext 不公开 swapchain image/slot/native token；image、frame slot、frame identity、submission completion 不能共用一个计数器。

| 结果 | 行为 |
| --- | --- |
| NotReady | 最小化/零尺寸，无 frame，不忙循环 |
| OutOfDate | 下次 clean begin 重建，不带旧无效 slot 重试 |
| Suboptimal | 完成本 frame，后续重建 |
| DeviceLost/BackendFailure | 首错误锁存 terminal，停止业务并有限清理 |

acquire 后 recording abort 仍要消耗 acquire semaphore：最小 transition/submit/present 关闭 WSI 所有权，不能将其当业务 upload/state commit。任一关闭阶段失败保留 terminal 原因。

VulkanViewport → VulkanSwapchain 一条 ownership 链；frameSlots=min(2, actual image count)。rendering_done semaphore 按 image 持有，**同 image 再 acquire 才证明可以重用**，graphics fence 不证明 WSI 已消费 semaphore。

recreate 在 clean begin，用共享 queue idle 收尾，完整 replacement 创建后才发布，不默认 device idle、不发布半个 swapchain、不常驻另一套 retired-generation ownership。

## Vulkan 内存与职责

- VulkanDevice 拥有 native root；资源、pipeline、binding 创建拆实现文件，不新建平行 device owner。
- VMA 3.4.0 只在 vma_implementation.cpp 实例化一次；allocator 归 device，allocation/映射/销毁检查返回值。
- staging/upload/descriptor 临时页与 deferred delete 以实际 completion 回收；不每次小上传都建独立长期 allocation，不按帧号猜安全。
- 非 coherent flush/invalidate 按 device alignment，CPU readback 等真实 completion；GPU 尚在用的 buffer/image/view/native descriptor 不提前释放。
- DeviceLost 不无限等 fence/idle；释放次序不反向调用已销毁 device，失败不冒充正常完成。

## 修改与验证

改公共接口同时给 Vulkan/D3D11/D3D12/mobile 的映射、明确不支持路径、所有权/状态/错误；先 validation 后 backend/caller，避免单后端形状反推公共层。design-rhi skill 提供工作方法，不保存第二份产品要求。

代表测试 engine/runtime/tests/rhi_device_frontend_tests.cpp、rhi_format_tests.cpp、rhi_binding_tests.cpp、rhi_resource_state_tests.cpp、rhi_shader_program_cache_tests.cpp、rhi_viewport_status_tests.cpp、vulkan_type_mapping_tests.cpp、vulkan_device_lifecycle_tests.cpp、vulkan_swapchain_tests.cpp。mock contract 验证不能替代真实 Vulkan 渲染/最小化/重建/退出；其它后端设计评估不等于已经运行。

rhi_resource_state_tests.cpp 当前直接使用 Vulkan 实现，与三个 vulkan_* 测试一样仅在 backend 开关开启时登记；其它公共 RHI 测试保持可在 backend 关闭时构建。Vulkan 测试显式声明 SDK/VMA 依赖，不从 Runtime 公共 include 中借用。
