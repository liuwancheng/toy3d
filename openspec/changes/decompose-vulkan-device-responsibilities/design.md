## Context

见 [proposal.md](./proposal.md) 的动机。本 change 发生在 `converge-rhi-device-frontend` 之后：公共 `RHIDevice` 已负责统一 admission、validation、owner identity、terminal state 和 pipeline cache，Vulkan 只实现 protected backend hooks。

当前 `vulkan_device.cpp` 约 2100 行，其中前约 570 行是 RHI→Vulkan 类型转换，其后同时包含 native device 启停、format capability、12 类创建 hook、binding packet materialization 和 pipeline 构建。`VulkanCommandContext`、`VulkanViewportContext`、`VulkanSwapchain` 又持有 `VulkanDevice&`，通过它取得 `VkDevice`、physical device、surface、queue、upload manager 和回收服务。这使调用方的真实依赖不可见，也允许任一 backend 对象逐步使用 device 的全部能力。

已有 `VulkanQueue`、`VulkanMemoryManager`、`VulkanUploadManager`、`VulkanDeferredDeletionQueue`、`VulkanViewportContext` 和 `VulkanSwapchain` 已经表达稳定的状态、所有权或同步职责。本设计复用这些对象，不再增加通用 `Services`、`Manager`、`Context` 或第二个 device wrapper。

架构参考只取职责划分：UE4.27 的 device/factory 与 command context 分离；Flax 的 Vulkan backend 也将 queue、context、resource、swapchain 和 descriptor/pipeline 实现分文件，但其全局 `GPUDevice::Instance`、`DrawBegin()` device 入口和大型 device service registry 不适用于 Toy3d。

## Goals / Non-Goals

**Goals:**

- `VulkanDevice` 继续是唯一 backend facade、native handle owner 与 composition root，但其 hook 只组合窄实现函数。
- backend object 的构造函数显式列出实际依赖，使 command、viewport、swapchain 不再通过 `VulkanDevice&` 查找服务。
- RHI→Vulkan 映射只有一个 backend-local 定义位置；resource、binding、pipeline 创建按稳定领域拆分。
- 保持初始化失败回滚、ordinary/device-lost shutdown 顺序、owner identity、错误码、queue completion 和 WSI 行为完全一致。
- 文件和依赖形状可以作为未来 D3D11/D3D12 backend 的参考，但不强迫三后端共享 native helper。

**Non-Goals:**

- 不修改 `RHIDevice`、`RHICommandContext`、`RHIQueue`、`RHIViewportContext` 或 renderscene 公共调用方式。
- 不实现 D3D11/D3D12 backend，也不抽象跨后端 `BackendDeviceServices` 或 `RHIResourceFactory`。
- 不改变 presentation lifecycle、descriptor pool 策略、pipeline cache、resource state tracker、VMA/upload/deferred deletion 算法。
- 不补 CPU map/readback、buffer view、GPU fence、validation 配置链或 render pass prepare/execute 拆分。
- 不以本 change 为由进行无关命名、格式化或目录重写。

## Decisions

### 1. `VulkanDevice` 保留 facade 与生命周期所有权

`VulkanDevice` 继续直接继承 `RHIDevice`，拥有 instance、debug messenger、primary surface、physical/logical device，以及 queue/memory/upload/deletion 服务。它负责有序 initialize/shutdown、capability 查询、公共 hook 路由和 observation 汇总。

不会引入 `VulkanBackendDevice`、`VulkanDeviceServices` 或另一个可被上层持有的根对象。第二个 device 会制造所有权歧义；一个汇总所有引用的 services bag 只是把 service locator 换了名字。保留单一 composition root 也符合 `RHIDevice` 是 God facade、不是 God implementation 的既定边界。

目标形状为：

```text
Renderer
  -> RHIDevice
       -> VulkanDevice                    唯一 facade / lifecycle owner
            -> VulkanQueue                submit / completion
            -> VulkanMemoryManager        allocation
            -> VulkanUploadManager        upload pages
            -> VulkanDeferredDeletionQueue
            -> backend-private creation functions
                 - resource/view/shader/sampler
                 - binding layout/set/packet
                 - graphics pipeline
```

### 2. 用 backend-private 窄函数拆创建领域，不新增无状态 factory 类

新增或整理以下 implementation modules，确切文件名可在实现时按现有命名微调，但职责不得重新合并：

- `vulkan_type_mapping.*`：`PixelFormat`、usage、dimension、sample count、shader stage、descriptor type、pipeline state 等 RHI→Vulkan 显式转换。
- `vulkan_resource_creation.*`：buffer、texture、texture view、shader、sampler 的 native 创建与 wrapper 构造；buffer view 的 `Unsupported` 路径保留在对应领域。
- `vulkan_binding_creation.*`：binding layout、logical binding set 和 physical binding packet materialization。
- `vulkan_pipeline_creation.*`：compatibility render pass、pipeline layout 和 graphics pipeline 创建。

这些函数放在 `toy3d` backend namespace 中，只接收完成操作所需的 `const RHIDevice&`、native handles、descriptor 和具体 manager references。它们不接收 `VulkanDevice&` 或 `initialized` frontend 状态，不调用公共 `RHIDevice::create_*()`，也不重复公共 frontend validation。需要 native handle 的函数可以防御 `VK_NULL_HANDLE` 这一 backend API 前置条件；不需要 native handle 的函数不再复制初始化检查。

`VulkanDevice::create_*_impl()` 保留为短适配器，向相应函数传入依赖并原样返回 `RHIResult`。相比把 member function 机械移动到多个 `.cpp`，窄函数会真正切断对完整 device 的访问；相比增加 `VulkanResourceFactory` 等无状态对象，普通函数没有伪造生命周期或所有权名词。

### 3. 映射按语义集中，调用方不得复制 switch

现有 `vulkan_device.cpp` anonymous namespace 中的转换和 `vulkan_resource.*` 中的 format helper 归入 mapping 模块。返回可能失败的映射继续使用 `RHIResult<T>`；确定完备且调用前已由公共 enum 限定的映射可以返回 native enum，但未知值仍须得到显式错误或安全的 invalid sentinel，禁止数字强转。

同一映射由 resource creation、pipeline creation、command recording 与 capability 查询共同使用。mapping 模块不拥有 device state、不执行 native allocation，也不包含 renderscene 类型。

### 4. backend object 使用逐项构造注入

移除 `VulkanCommandContext`、`VulkanViewportContext`、`VulkanSwapchain` 对完整 `VulkanDevice&` 的保存，改为逐项注入：

- command context：`const RHIDevice&` owner identity、`VkDevice`、所需 command pool/frame association、`VulkanUploadManager&`；binding packet 通过窄 creation 函数 materialize。
- viewport context：`const RHIDevice&` owner identity、`VkPhysicalDevice`、`VkDevice`、primary `VkSurfaceKHR`、graphics queue family、`VulkanQueue&`、upload/deletion 回收引用。
- swapchain：`const RHIDevice&` owner identity、`VkPhysicalDevice`、`VkDevice`、`VkSurfaceKHR`；present 时仍显式接收 queue handle。

不创建一个聚合这些参数的 non-owning `VulkanDeviceContext/Services`。构造参数较长是 composition root 真实依赖的可见成本；若以后多处出现同一组且具有独立生命周期含义，再另立 change 评估具名对象。

`VulkanViewportContext*` 这类表达 frame association 的窄引用可以保留，因为它代表命令列表的 presentation 归属，不是服务查找入口。

### 5. shutdown 与 device-lost 顺序作为不可变迁移约束

迁移后 ordinary shutdown 仍由公共 frontend 关闭 admission、等待在途创建并执行一次 idle policy；backend shutdown 的销毁顺序保持：

```text
queue/pending submissions
  -> deferred deletion payload
  -> upload manager
  -> memory manager
  -> logical device
  -> primary surface
  -> debug messenger
  -> instance
```

`shutdown_after_device_lost()` 不增加 native idle wait。任何提取函数都不得拥有或延长上述根 handle 的生命周期；所有注入引用必须短于 `VulkanDevice`，这一点由 Renderer/device 已有销毁 contract 保证。

### 6. 只在 backend 边界增加结构检查，行为由现有测试证明

实现完成后要求：

- `vulkan_command_context.*`、`vulkan_viewport_context.*`、`vulkan_swapchain.*` 不再 include `vulkan_device.h`，成员中不存在 `VulkanDevice&/*`。
- creation/mapping 模块不依赖 renderscene，也不调用公共 `RHIDevice::create_*()`。
- `vulkan_device.cpp` 不再包含大段 type mapping 或 native resource/binding/pipeline 构建逻辑。
- 公共 frontend tests、binding/resource-state/viewport/swapchain tests 和 Editor build/smoke 继续通过。

不新增运行时“架构对象”来方便测试，也不依赖仅检查行数的脆弱测试；结构边界通过 include/dependency 搜索与 code review 验证，行为通过现有及必要的定向单元测试验证。

## Risks / Trade-offs

- [逐项注入使部分构造函数参数较长] → 只传真实依赖，并在 composition root 使用具名局部变量保持可读；不为缩短参数列表创建无语义 services bag。
- [移动 mapping 时可能改变错误分类或默认值] → 先建立映射定向测试，再做机械迁移；hook 原样传播 `RHIResult`，不统一改写错误。
- [拆分 translation unit 后出现循环 include] → helper header 只暴露最小前置声明和 Vulkan/RHI 值类型；resource wrapper 定义留在现有资源模块，creation 模块依赖 wrapper 而非反向依赖。
- [viewport/device 解耦时破坏 WSI 或 completion 双完成域] → 不改变 submit/present/recreate 算法，仅替换依赖来源；继续以 presentation lifecycle specs 和 resize/minimize/restore smoke 验证。
- [free-function 模块未来变成新的杂物箱] → 每个模块只覆盖一个创建领域；需要状态、缓存或同步时必须由已有 owner 持有，不能使用 static mutable state。
- [一次迁移范围较大，难以定位回归] → 按 mapping、creation、consumer injection、accessor cleanup 顺序实施，每步保持可构建并可单独回退。

## Migration Plan

1. 记录当前 include/call dependency 与定向测试基线；补足 type mapping 和错误传播测试缺口。
2. 提取并统一 `vulkan_type_mapping.*`，迁移调用方，删除旧 anonymous/重复 mapping。
3. 依次提取 resource、binding、pipeline creation 函数；把 `VulkanDevice::create_*_impl()` 收敛为窄适配器。
4. 将 command context、swapchain、viewport context 改为逐项构造注入，先迁移构造链，再删除 `VulkanDevice&` 成员与不再需要的 native/service accessors。
5. 复核 initialize/shutdown/device-lost 顺序、CMake source registration、Active RHI 文档中的 backend implementation 摘要和结构搜索结果。
6. 由独立验证 sub-agent 完成 Windows configure、受影响 tests、`Toy3dEditor` build、全量 CTest 和 Vulkan validation smoke。

这是内部重构，不需要数据迁移或双轨兼容层。若某一步发生行为回归，回退该领域的提取并恢复原调用位置；禁止保留新旧两个正式 creation 路径。
