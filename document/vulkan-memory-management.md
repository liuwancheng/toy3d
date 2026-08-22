# Toy3d Vulkan Buffer 与 Texture 内存管理方案

> **文档状态：Active contract。** 第 2 节“当前实现评估”记录迁移开始前的历史快照，不代表当前代码状态；当前已完成能力以本文后续“当前进度”、公共接口、实现和测试为准。历史问题不得被解释为仍待修复的现状。

## 1. 目标与结论

Toy3d Vulkan 后端应采用以下组合：

- 公共 RHI 只表达资源用途、CPU 可见性、初始状态和上传语义；
- Vulkan 后端使用一层 UE 风格的资源管理封装维护资源身份、分配策略、上传和延迟回收；
- VMA 作为 Vulkan 后端内部的物理内存分配内核，负责 memory type 选择、block suballocation、dedicated allocation 和统计；
- 高频上传使用持久映射、按 frame/queue completion value 回收的 upload ring/page pool，不为每次上传创建 staging buffer；
- Buffer、Image、allocation 和 view 的销毁统一服从 queue completion value，资源析构不能直接假定 GPU 已停止使用；
- 第一阶段不实现 UE 完整的 resource heap、RHI thread、defrag/relocation 和 transient aliasing，但接口应允许后续增加。

这不是简单地把 `vkAllocateMemory` 替换成 `vmaCreateBuffer`。优化重点是减少 Vulkan allocation 次数、减少每次上传的对象创建和映射开销、建立正确的 in-flight 生命周期，并提供可观测的预算和分配统计。

## 2. 当前实现评估

### 2.1 Vulkan 管线

当前 Vulkan 存在以下问题：

- `VulkanDevice::create_buffer()` 每个 Buffer 独立执行 `vkAllocateMemory` 和 `vkBindBufferMemory`；
- `VulkanDevice::create_texture()` 每个 Image 独立执行 `vkAllocateMemory` 和 `vkBindImageMemory`；
- `create_vulkan_staging_buffer()` 每次上传都创建 `VkBuffer`、分配 `VkDeviceMemory`、map、copy、unmap；
- `VulkanMemoryAllocator` 仍是 dedicated allocation 封装，而且资源创建路径没有使用它；
- `VulkanBuffer`、`VulkanTexture` 和 `VulkanStagingBuffer` 析构时直接销毁原生对象和内存；
- viewport frame slot 通过强引用保活提交资源，这能保护当前 viewport 提交，但尚未形成统一 queue completion value 回收模型；
- `VulkanDeferredDeletionQueue` 已存在，但只在 device shutdown 调用 `release_all()`，正常帧流程尚未调用 `release_completed()`；
- `VulkanQueue` 尚未实现真正的 completion value/fence tracking，因此 allocator page、staging range 和资源销毁还不能按 GPU 完成状态统一回收。

这些问题会造成大量 driver allocation、频繁创建 staging 对象、内存碎片和不可扩展的资源生命周期。

### 2.2 旧 Vulkan 管线

旧管线已经使用 VMA，并带有部分 UE 风格痕迹，例如资源类、pending state、descriptor set 和 cache 的分层。不过其内存使用仍不适合直接迁移：

- Buffer/Image 直接保存 `VmaAllocator` 和 `VmaAllocation`，资源层与分配细节耦合；
- 静态 vertex/index buffer 各自长期持有 staging buffer，扩大 host-visible 内存占用；
- 更新路径缺少完整 barrier 和 GPU in-flight 契约；
- 部分构造路径存在 allocator 未传入的问题；
- `create_persistent_buffer()` 把 `VMA_ALLOCATION_CREATE_MAPPED_BIT` 写入了 `VkBufferCreateInfo::flags`，字段位置错误；
- 使用 `VMA_MEMORY_USAGE_GPU_ONLY/CPU_ONLY/CPU_TO_GPU` 等旧式 usage，无法准确表达 required/preferred flags 和顺序访问模式；
- 错误主要由 assert 处理，缺少 Release 可诊断路径；
- VMA allocator 创建结果和销毁生命周期不完整。

因此旧管线适合作为命名和职责拆分参考，不适合复制实现。

### 2.3 第三方 VMA 状态

仓库中的 VMA 已升级到稳定版 `3.4.0`，并通过独立的 `vma_implementation.cpp` 提供唯一实现。Vulkan 接入应继续保持 VMA 依赖升级与资源管理实现分离，便于审查和回退。

## 3. 参考方案的取舍

### 3.1 借鉴 UE 的部分

借鉴 UE4.27 的职责边界，而不是复制其规模：

- RHI resource 是后端无关的资源身份；
- Vulkan resource wrapper 持有 `VkBuffer`/`VkImage` 和 allocation handle；
- device 负责创建，command context 负责 copy/transition，queue 负责 completion；
- allocation manager、staging manager 和 deferred deletion 是 device 级服务；
- 资源释放与 GPU completion 解耦；
- 大资源和特殊资源可以 dedicated allocation，小资源走 block suballocation；
- 统计和预算是 allocator 的正式职责。

暂不照搬：多级 heap/page 体系、完整 defragmentation、RHI thread、device-local staging、多 queue ownership、transient aliasing 和复杂资源重定位。

### 3.2 借鉴 Vulkan Samples/VMA 推荐实践的部分

- device-local 静态资源通过 staging copy 初始化；
- upload memory 持久映射，按对齐要求分配子区间；
- host-visible 非 coherent 内存显式 flush/invalidate；
- 用 allocation flags 表达 host access 和 mapping，而不是只依赖旧 `VmaMemoryUsage`；
- 大资源或 Vulkan dedicated requirements 交给 VMA 自动选择 dedicated allocation；
- 使用 memory budget 扩展时采集 heap budget/usage；
- 避免每个资源一次 `VkDeviceMemory`，也避免每次 upload 一次 `VkBuffer`。

## 4. 分层设计

```text
RenderScene / RenderCore
        |
        | RHIBufferDesc / RHITextureDesc / upload / transition
        v
RHIDevice + RHICommandContext + RHIQueue
        |
        v
VulkanDevice
  |-- VulkanMemoryManager       VMA 生命周期、策略、预算、统计
  |-- VulkanUploadManager       持久映射 upload page/ring
  |-- VulkanDeferredDeletionQueue
  |-- VulkanQueue               completion value 与完成状态
  |
  |-- VulkanBuffer              VkBuffer + VulkanAllocation
  |-- VulkanTexture             VkImage + VulkanAllocation
  `-- VulkanTextureView         VkImageView + texture 强引用
```

### 4.1 `VulkanMemoryManager`

替换当前名不副实的 `VulkanMemoryAllocator`，device 唯一拥有它。建议职责：

```cpp
enum class VulkanAllocationUsage
{
    GpuOnly,
    CpuUpload,
    CpuReadback
};

struct VulkanAllocation
{
    VmaAllocation handle = VK_NULL_HANDLE;
    VkDeviceSize offset = 0;
    VkDeviceSize size = 0;
    void* mapped_data = nullptr;
    std::uint32_t memory_type_index = VK_MAX_MEMORY_TYPES;
};

struct VulkanAllocatedBuffer
{
    VkBuffer buffer = VK_NULL_HANDLE;
    VulkanAllocation allocation;
};

struct VulkanAllocatedImage
{
    VkImage image = VK_NULL_HANDLE;
    VulkanAllocation allocation;
};

class VulkanMemoryManager final
{
public:
    RHIStatus initialize(const VulkanMemoryManagerDesc& desc);
    void shutdown();

    RHIResult<VulkanAllocatedBuffer> create_buffer(
        const VkBufferCreateInfo& buffer_info,
        VulkanAllocationUsage allocation_usage,
        VulkanAllocationFlags flags,
        const char* debug_name);

    RHIResult<VulkanAllocatedImage> create_image(
        const VkImageCreateInfo& image_info,
        VulkanAllocationUsage allocation_usage,
        VulkanAllocationFlags flags,
        const char* debug_name);

    void destroy_buffer(VulkanAllocatedBuffer& buffer);
    void destroy_image(VulkanAllocatedImage& image);
    RHIResult<VulkanMemoryStats> query_stats() const;
};
```

`VmaAllocator`、`VmaAllocationCreateInfo` 和 `VmaAllocation` 不进入公共 RHI。资源类也不自行选择 memory type。

### 4.2 资源封装

`VulkanBuffer` 和 `VulkanTexture` 保存一个完整 allocated object，不再分别保存裸 `VkDeviceMemory`：

```cpp
class VulkanBuffer final : public RHIBuffer
{
private:
    VulkanMemoryManager* memory_manager = nullptr;
    VulkanAllocatedBuffer allocated_buffer;
    RHIAccess resource_access = RHIAccess::Common;
};
```

资源对象负责资源身份和状态，不负责 upload、submit 或 wait。swapchain image 是 non-owning image，allocation 为空，销毁时只销毁 owned view，不销毁 image。

更稳妥的最终模型是让资源析构把 native payload 交给 device/queue 的 retire 服务；短期若 command list 强引用已经保证析构发生在 fence 完成后，可以直接调用 memory manager 销毁，但必须把这个前提写成 invariant 并由测试覆盖。长期统一走 queue completion value 驱动的 deferred deletion，才能支持资源在录制后、提交前或多 viewport 场景释放。

### 4.3 `VulkanUploadManager`

上传管理器是性能优化的核心，不应由每个资源拥有 staging buffer。

建议使用 page pool + linear suballocation：

- 每个 upload page 是一个较大的 `VkBuffer`，使用 VMA 创建并持久映射；
- page 内用线性 offset 分配，返回 buffer、offset、CPU pointer；
- 每个 recording context 保存本次使用的 page/range；
- submit 后将返回的 `completion_value` 记录为 range/page 的 `retire_value`；
- `completed_value >= retire_value` 后 reset/recycle page；
- page 空间不足时先换新 page，大上传走临时 dedicated upload allocation；
- buffer copy 对齐至少满足 Vulkan copy 约束；texture upload 同时处理 texel block、row pitch 和 offset 对齐；
- 非 coherent memory 在提交前按 `nonCoherentAtomSize` 对齐后 `vmaFlushAllocation()`；readback 对应 invalidate；
- 源数据在 `upload_*()` 返回前拷贝进映射区，继续满足当前 RHI 的 `CopiedBeforeReturn` 契约。

建议初始策略值作为可配置项，而不是硬编码 API 契约：

| 项目 | 初始建议 | 说明 |
|---|---:|---|
| upload page | 16 MiB | 桌面平台起点，统计后调整 |
| dedicated upload 阈值 | page 的 1/2 | 避免单次大上传挤占普通 page |
| frames in flight | 跟随 viewport | page 回收实际以 queue completion value 为准 |
| persistently mapped | 开启 | upload/readback page 使用 |
| allocation strategy | 默认/TLSF | 先使用 VMA 默认策略并测量 |

不要一开始实现无锁环。第一阶段 Render Thread 单线程录制，普通线性 allocator 更容易保证正确；未来 pass 级并行时，为每个 recording context/thread 分配独立 current page，page pool 再加内部同步。

## 5. 资源策略

### 5.1 Buffer

| RHI 意图 | Vulkan memory class | 创建与更新策略 |
|---|---|---|
| 静态 vertex/index/storage | `DeviceLocal` | 带 `TRANSFER_DST`，通过 upload manager copy |
| 高频 uniform/小动态数据 | `Upload` 或 frame-local uniform arena | 持久映射子分配，按 uniform alignment 对齐 |
| 通用 GPU-only buffer | `DeviceLocal` | 更新统一走 upload/copy |
| CPU readback | `Readback` | GPU copy 后等待对应 completion value，再 invalidate/map |
| CPU-visible 独立资源 | `Upload`/`Readback` | 仅在公共 map 契约明确后开放 |

高频 uniform 不应创建大量小 `VkBuffer`。建议后续增加 frame-local dynamic uniform arena：一个大 Buffer 加 dynamic offset；公共 binding 仍表达 buffer range，Vulkan 后端映射为 dynamic descriptor 或普通 offset binding，D3D 后端使用其等价机制。

### 5.2 Texture

- 普通 sampled、render target、depth/stencil、storage texture 使用 `DeviceLocal`；
- `VkImageUsageFlags` 必须由公共 usage 集中转换，并根据 upload/readback 需求加入 transfer flags；
- initial data 通过 upload manager 和 command context copy，不在 `create_texture()` 内隐式 submit/wait；
- mip/layer/subresource upload 使用公共 pitch 和 range 描述；压缩格式按 block extent/block bytes 计算，不能继续使用简单 bytes-per-texel；
- 大 render target、depth 或 VMA/Vulkan dedicated requirements 允许 dedicated allocation；
- texture view 独立缓存可以后置，第一阶段保持明确所有权和强引用；
- transient attachment aliasing 不是第一阶段目标，未来应作为 render graph/backend 扩展，不能让 VMA aliasing 语义进入公共 descriptor。

## 6. VMA 配置方式

### 6.1 编译与生命周期

- 建立唯一的 `vma_implementation.cpp`，其中定义 `VMA_IMPLEMENTATION`；
- 其他文件只包含声明，禁止依赖某个包含顺序提供实现；
- `VulkanDevice` 在 logical device 创建后初始化 memory manager；
- 所有 allocated resources、upload pages 和 deferred deletions 清空后，才销毁 VMA allocator；
- 检查 `vmaCreateAllocator()` 返回值并转换为 `RHIStatus`；
- 明确填写 `vulkanApiVersion`，与 instance/device 实际启用版本一致；
- debug 构建接入 allocation name，便于 RenderDoc、validation 和 VMA stats 定位。

### 6.2 allocator flags

按实际 capability/extension 条件启用：

- memory budget 可用时启用对应 budget flag；
- buffer device address 只有 feature 真正启用且资源需要时才启用；
- 不建议第一阶段启用 externally synchronized，先使用 VMA 内部同步；
- dedicated allocation/bind memory2 根据 Vulkan version 和 VMA 版本要求配置，不重复假设扩展存在。

### 6.3 allocation flags

- `DeviceLocal`：`VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE`；
- `Upload`：`VMA_MEMORY_USAGE_AUTO`，要求 sequential-write host access，通常持久 mapped；
- `Readback`：`VMA_MEMORY_USAGE_AUTO`，要求 random host access，通常持久 mapped；
- 只有明确的大资源、外部内存或驱动 dedicated requirement 才强制 dedicated；一般情况交给 VMA 决策；
- 不假设 host-visible 一定 coherent，flush/invalidate 必须成为 upload/readback manager 的固定步骤。

具体 flag 名称应以升级后锁定的稳定 VMA 版本为准。

## 7. 生命周期与同步闭环

目标流程：

```text
create resource
    -> VMA create/suballocate
record upload/copy/draw
    -> command list retains resource + upload ranges
submit
    -> queue returns a monotonically increasing completion_value
    -> native fence/timeline tracks completion
resource last CPU ref released
    -> native payload enqueued with retire_value
completed_value advances
    -> recycle upload pages
    -> destroy retired Buffer/Image/View/allocation
```

必须先完成 `VulkanQueue` 的 completion value/fence tracking，再把 deferred deletion 和 upload page recycling 建立在它之上。不能以 frame index 替代 completion value；frame slot 只是方便的回收域，真正安全条件是对应提交已完成。

公共类型统一命名为：

```cpp
using RHIQueueCompletionValue = std::uint64_t;
```

`RHIQueue::submit()` 返回本次提交的 `completion_value`；`completed_value()` 返回该 queue 已完成的最大值；等待接口使用 `wait_for_value()`。待回收对象保存 `retire_value`，当 `completed_value >= retire_value` 时才允许回收。

第一阶段只有 graphics queue，因此资源记录一个 `last_use_value` 即可。未来多 queue 时，completion value 仍然只在所属 queue 内有序且可比较；跨 queue 需要额外携带 queue domain 或 completion token，当前公共资源接口不应暴露后端同步对象。

## 8. 公共 RHI 边界与三后端映射

公共 `RHIBufferDesc`/`RHITextureDesc` 现有的 `usage`、`cpu_access`、`initial_access` 和 `debug_name` 足以支持第一阶段，不应增加 `VmaMemoryUsage`、heap index 或 dedicated flag。

需要进一步明确但仍保持后端无关的语义：

- CPU access 是长期 map、按次 map，还是仅允许 upload/readback command；
- dynamic/streaming frequency 是否作为性能 hint；
- map mode、range、flush/invalidate 和 in-flight 冲突；
- resource initial data 与显式 upload batch 的关系；
- memory budget/usage 通过 capability/statistics 查询，而不是暴露 allocator。

| 公共语义 | Vulkan | D3D12 | D3D11 后端 |
|---|---|---|---|
| GPU-only | VMA device-local | DEFAULT heap/placed resource | DEFAULT usage |
| upload | host-visible upload page | UPLOAD heap ring | dynamic/staging update 路径 |
| readback | host-visible cached allocation | READBACK heap | staging resource |
| suballocation | VMA block | heap allocator | 多由 driver 管理或 buffer arena |
| completion value | timeline value 或 fence tracking value | fence value | query/fence 模拟值 |
| deferred deletion | completion value + fence/timeline | fence value | completion value + query/fence |

目标后端统一为 Vulkan、D3D11 与 D3D12。D3D11 基线为 Feature Level 11_0 与 Shader Model 5.0；不支持 D3D10/Feature Level 10.x/Shader Model 4。

## 9. 建议源码组织

```text
engine/runtime/drivers/vulkan/
  vulkan_memory_manager.h/.cpp
  vulkan_upload_manager.h/.cpp
  vulkan_deferred_deletion.h/.cpp
  vulkan_resource.h/.cpp
  vulkan_queue.h/.cpp
  vma_implementation.cpp
```

避免建立泛化过早的跨 API `RHIMemoryAllocator`。D3D12 可以拥有自己的 heap allocator，D3D11 使用其自然资源模型；公共层只统一语义和统计接口。

## 10. 分阶段迁移

### 阶段 0：基线与依赖

1. 记录典型场景的资源数量、`VkDeviceMemory` 数量、上传字节数和 frame time；
2. 锁定并验证已升级的 VMA `3.4.0` 及唯一 implementation translation unit；
3. 加入 allocator 初始化失败和统计输出测试。

### 阶段 1：常驻资源接入

当前进度：Buffer 和 Texture 常驻资源子项已完成；swapchain image 保持 non-owning。

1. 用 `VulkanMemoryManager` 替换当前 `VulkanMemoryAllocator`；
2. `create_buffer()` 和 `create_texture()` 统一通过 manager 创建；
3. `VulkanBuffer`/`VulkanTexture` 保存 allocation handle；
4. swapchain image 保持 non-owning；
5. 初期仍使用现有 frame 强引用保证安全，先验证功能和 allocation 数量下降。

### 阶段 2：上传管理

当前进度：已完成 page pool 第一阶段接入。Buffer/Texture upload 已改用 16 MiB 持久映射 upload page 的线性子分配，超过半页的大上传使用独立 page；非 coherent 内存通过 VMA flush。普通 page 用尽后进入 pending 集合，并在关联 submission 的 completion value 完成且没有 command list/frame slot 引用时 reset 复用。压缩纹理和统计仍待后续阶段完成。

1. [已完成] 实现 persistently mapped upload page pool、滚动、in-flight 保活和 completion value 驱动的普通 page 回收；
2. [已完成] buffer/texture upload 改为 page suballocation；
3. 支持 row/slice pitch、压缩格式 block 和 flush alignment；
4. [已完成] 删除每次上传创建 `VulkanStagingBuffer` 的路径；
5. [已完成] 增加大上传 dedicated fallback，以及 upload bytes/count、page creation/rollover/recycle、dedicated fallback 和 page pool 快照统计。

### 阶段 3：completion value 与延迟回收

当前进度：queue completion tracking、viewport 提交、upload page 回收和 deferred deletion 正常帧推进已完成。公共类型统一使用 `RHIQueueCompletionValue`；Vulkan 为普通 queue submit 创建独立 fence，viewport 则登记其 frame slot fence。`completed_value()` 按提交顺序推进，`wait_for_value()` 可等待指定 completion value。Buffer/Texture 会记录最后一次提交值，并将 VMA native payload 延迟到该值完成后销毁。

1. [已完成] 完成 `VulkanQueue::submit_impl()`、单调 completion value 和 fence tracking；
2. [已完成] viewport 提交也统一进入 queue completion 模型，swapchain semaphore 仍由 viewport 私有管理；
3. [已完成] 每帧推进 `completed_value()`；
4. [已完成] 根据 `completed_value >= retire_value` 回收 upload page，并延迟销毁 Buffer/Texture native payload；
5. [准备中] 已增加 allocation 创建/销毁、活跃字节、upload page pool、deferred deletion、completed value 观察快照和资源 last-use 只读查询；待覆盖资源在录制后释放、多 frame-in-flight 和 resize 场景。

### 阶段 4：高频资源优化

1. frame-local dynamic uniform arena；
2. allocation budget、峰值、fragmentation/stats dump；
3. 根据数据调整 page 大小、大资源阈值和 pool 保留策略；
4. 只有测量证明需要时，再考虑专用 pool、virtual block、defrag 或 transient aliasing。

旧管线已删除，Vulkan 后端只保留基于 `RHIDevice` 的资源与 VMA 生命周期实现。

## 11. 验收与性能指标

功能正确性：

- validation layer 无 memory lifetime、bind、map/flush 和 in-flight destruction 错误；
- Buffer/Texture 创建失败返回可诊断 `RHIStatus`；
- 多 frame-in-flight、resize、上传后立即释放 CPU 引用均正确；
- device shutdown 顺序为 queue idle/completion、资源与 page 回收、VMA allocator、device；
- swapchain image 不被 VMA 销毁。

性能与可观测性：

- 常规场景 `VkDeviceMemory` 数量显著小于 Buffer/Image 数量；
- 正常帧的小上传不产生新的 `VkDeviceMemory`，稳定后也不频繁创建 `VkBuffer`；
- 统计 device-local/upload/readback 的 block、allocation、used、unused、budget 和峰值；
- 统计每帧 upload bytes、page count、page rollover、dedicated fallback 和回收延迟；
- 对比迁移前后的 CPU upload 时间、driver allocation 次数、峰值显存和 frame-time spike；
- Debug 可按资源 `debug_name` 定位 allocation，Release 不依赖 assert 处理失败。

建议先设结构性目标，不承诺脱离场景的绝对性能数字。完成一个真实场景基线后，再把 page 大小、dedicated threshold 和池保留上限固化为项目默认值。

## 12. 暂不实施的能力

- GPU 内存 defragmentation 和在线资源 relocation；
- render graph transient resource aliasing；
- sparse resource；
- 多 queue ownership 和 async transfer queue；
- bindless 专用 descriptor/resource residency；
- 完整的内存 residency/eviction 系统。

这些能力都可以建立在 `VulkanMemoryManager + RHIQueueCompletionValue + resource wrapper` 的基础上，不需要提前污染公共 RHI。
