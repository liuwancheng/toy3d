## Context

动机见 [proposal.md](./proposal.md)，规范行为见本 change 的五份 delta specs。当前 ShaderMap 已保留 `ShaderParameterId` 和 target binding，但转换为公共 `RHIShaderBindingReflection`/`RHIBindingLayoutEntry` 时丢弃 ID 和 constant ABI identity；`RHIBindingValue` 随后只能用 slot 与完整 Program layout 配对。公共 `validate_graphics_bindings()`、Vulkan draw flush 和 physical packet materializer 又要求各 logical set 与 Pipeline 使用完整相等 layout，导致 View adapter、Material per-layout cache 和 Object per-Program materialization。

当前 Vulkan logical `VulkanBindingSet` 本身没有 `VkDescriptorSet`，native set 在 draw flush 时生成；但每个 physical packet 创建独立 `VkDescriptorPool`。高频 uniform helper还为每个Object创建独立`RHIBuffer`并录制copy/transition。新模型需要同时改变identity、公共RHI对象、native materialization和uniform storage，才能避免把adapter转移到另一层。

约束包括：五个logical groups保持不变；Vulkan ES3.1 profile仍为Vulkan 1.1、SPIR-V 1.3和最多四个bound descriptor sets；D3D11基线为FL11_0/SM5且不能依赖`ID3D11DeviceContext1`；公共层不得暴露descriptor set、dynamic descriptor、root signature或stage register；一个viewport frame当前仍只录制一个graphics command list；业务pass仍须在`begin_render_pass()`前完成CPU参数生成和公共资源准备。

`modularize-scene-mesh-passes`已完成但未归档，其中adapter是当时公共RHI限制下的允许路径；归档/同步顺序应先落该change，再由本change的delta取代adapter contract。`add-tonemap-imgui-output-passes`仍在进行中，实施必须迁移其已落地的Tonemap/ImGui调用点，不能留下旧BindingSet正式入口。

## Goals / Non-Goals

**Goals:**

- 让逻辑资源owner只依赖稳定参数身份和数据ABI，不依赖任何消费Program、target slot或backend布局。
- 保持`RHIBindingLayout`作为Program/Pipeline的target-specific active mapping，并让RHI在recording时结合layout与logical sets。
- 以一个公共模型覆盖View、Pass、Material、Object、fullscreen/UI和未来ShadowPass/compute；本轮删除全部旧绑定路径。
- 把高频constant数据从per-draw resource creation收敛到completion-scoped分页slice，并让Vulkan descriptor packet可排除dynamic offset复用。
- 明确validation、resource-state、GPU payload、hot reload、descriptor回收和失败闭合。

**Non-Goals:**

- 不实现D3D11或D3D12完整backend；公共接口、mock tests和设计必须保证二者无需再次破坏API即可实现。
- 不引入bindless、descriptor indexing、update-after-bind、push/root constants、RDG或多线程pass录制。
- 不建立跨帧persistent Vulkan descriptor cache；第一版使用recording-local packet cache和completion-scoped pool pages，未来可在backend内部增加跨帧cache而不改变公共接口。
- 不把Material parameter packing、View字段、Shadow数据来源或pass调度下沉到RHI。
- 不新增公共`RHIBindingGroupLayout`、View adapter、Key/Token/Enabler类或仅包装find/add的cache对象。

## Decisions

### 1. 使用同一个 ShaderParameterId 贯穿 ShaderFormat 到 RHI

将canonical `ShaderParameterId` value type放在现有中立ShaderFormat contract的窄头文件中；ShaderCompiler、RenderCore和公共RHI直接使用同一类型。删除`rendercore/shader/shader_parameter_id.h`的重复alias，不新增`RHIBindingId`。

每个聚合constant buffer使用已有规则生成稳定binding ID：`group + Constant + empty canonical name`；buffer内各member继续使用`group + Constant + member name`，两类身份不混用。独立texture/sampler/storage resource使用各自category与name。

选择稳定ID而不是name，是为了避免运行时字符串匹配；选择共享类型而不是RHI新ID，是为了避免语义重复和转换遗漏。ID collision仍由Cook/Entry reader结合canonical name和typed metadata拒绝。

### 2. RHIBindingLayout 保存逻辑身份和 target mapping

目标descriptor形状为：

```cpp
struct RHIBindingLayoutEntry
{
    ShaderParameterId binding_id = 0;
    RHIBindingGroup group = RHIBindingGroup::Material;
    RHIResourceBindingType type = RHIResourceBindingType::UniformBuffer;
    RHIShaderStageFlags stages = RHIShaderStageFlags::None;
    std::uint32_t target_binding = 0;
    std::uint32_t array_count = 1;
    ShaderDataLayoutHash data_layout_hash{};
    std::uint32_t data_size = 0;
    std::uint32_t shader_abi_version = 0;
};
```

非uniform entry的data fields必须为zero/invalid。相同ID在多个stage使用同一target mapping时可以合并stage flags；target mapping不同则保留独立entry。创建边界按`group、binding_id、type、stage、target_binding`规范化排序，拒绝逻辑冲突和同一native namespace overlap；debug name不参与相等或cache key。

`RHIShaderBindingReflection`同步携带相同identity/ABI metadata，使`create_shader()`与`create_binding_layout()`能够验证最终binary reflection、Program active layout和target mapping一致。`RHIBindingLayout`仍由Program拥有并进入Pipeline/cache；它不是资产级schema，也不由资源owner持有。

只增加group-layout对象的替代方案被否决：只按group比较layout仍然使用target slot作为resource identity，也无法让superset跨active variants复用。

### 3. RHIBindingSet 是无 layout 的 immutable logical resource table

目标公共形状为：

```cpp
struct RHIBindingValue
{
    ShaderParameterId binding_id = 0;
    std::uint32_t array_index = 0;
    RHIBufferRef buffer;
    RHIBufferViewRef buffer_view;
    RHITextureViewRef texture_view;
    RHISamplerRef sampler;
    std::uint64_t buffer_offset = 0;
    std::uint64_t buffer_size = 0;
    ShaderDataLayoutHash data_layout_hash{};
    std::uint32_t shader_abi_version = 0;
};

struct RHIBindingSetDesc
{
    RHIBindingGroup group = RHIBindingGroup::Material;
    std::vector<RHIBindingValue> bindings;
    std::string debug_name;
};
```

frontend要求每个value恰好提供一种resource，检查nonzero ID、`binding_id + array_index`唯一、buffer range、uniform metadata、resource owner和device limits中不依赖Program的部分，并规范化排序便于binary search。空set拒绝；未使用group用null。

`RHIDevice::create_binding_set()`通过公共NVI admission后直接构造普通`RHIBindingSet`。删除`create_binding_set_impl()`、`VulkanBindingSet`和backend downcast。logical set只强持有其资源，没有native descriptor生命周期。

保留`RHIBindingSetRef`而不是把vector内嵌每个`MeshDrawCommand`，是因为View/Material等owner需要共享immutable snapshot，command构建和recording需要稳定强引用。第一版不增加通用builder；各领域helper直接构造descriptor，只有重复显著后才提取普通函数。

### 4. Pipeline-driven resolver 只选择 active values

`bind_graphics_bindings()`先验证五个字段的group和device owner；需要当前Pipeline的完整性检查在draw flush前执行。RHI共享resolver按每个active layout entry查找相应group set中的`binding_id + array_index`，验证type、array completeness、uniform size/hash/ABI和resource range，产生仅实现内部使用的resolved entry序列：

```text
RHIBindingLayoutEntry(target mapping)
    + RHIBindingValue(logical resource)
    -> resolved active binding
```

该序列不是公共resource/class，不保存到renderscene。Backend使用它完成native mapping和resource-state检查。Superset中未被选择的value不得被transition、retain或写descriptor；这避免inactive texture状态影响draw，也避免command list因完整Material schema保活未使用资源。

在BindingSet创建时要求精确Program layout的替代方案被否决，因为它正是adapter来源；在RenderCore为每个Program构建target packet的替代方案也被否决，因为它只是把耦合从RHI移回pass/material代码。

### 5. Constant data compatibility 使用完整布局身份

仅比较buffer size不足以证明ABI兼容。ShaderCompiler为每个聚合constant buffer计算完整`ShaderDataLayoutHash`，覆盖ToyShaderABI version、group、buffer binding ID、总size，以及按canonical顺序排列的member ID/type/offset/size/array stride/matrix stride。该hash以完整SHA-256值进入Entry和runtime metadata，不使用截断hash做最终equality。

uniform `RHIBindingValue`携带写入bytes所依据的hash和ABI version；Pipeline resolver与layout entry做完整比较。D3D12的256-byte native allocation padding、Vulkan dynamic alignment和D3D11 buffer capacity不进入logical hash，`data_size`与native allocation size分离。

mapping-only hot reload会重建RHI layout/PSO但复用logical sets；constant hash改变时，View/Pass/Object重新序列化，Material走既有candidate replacement并只在完整成功/submit后发布。

### 6. Transient uniform allocation 属于 recording context

公共API采用recording-scoped操作，而不是让上层管理mapped ring：

```cpp
struct RHITransientUniformDataDesc
{
    RHIInitialData source;
    ShaderDataLayoutHash data_layout_hash{};
    std::uint32_t shader_abi_version = 0;
    std::string debug_name;
};

struct RHIUniformBufferSlice
{
    RHIBufferRef buffer;
    std::uint64_t offset = 0;
    std::uint64_t size = 0;
    ShaderDataLayoutHash data_layout_hash{};
    std::uint32_t shader_abi_version = 0;
};

RHIResult<RHIUniformBufferSlice>
RHICommandContext::upload_transient_uniform_data(
    const RHITransientUniformDataDesc& desc);
```

调用必须处于recording状态并在返回前复制bytes。Backend按自身alignment向上分配、清零padding并返回logical size；page buffer以`UniformBuffer` access供绑定，不为每个slice录制copy/transition。command list保活实际page，submit成功后以queue completion退休；discard/submit failure走未提交回收。

Vulkan和D3D12将现有upload-page思想扩展为可直接绑定的uniform pages。D3D11优先使用pool中的standalone dynamic constant buffer并返回offset 0；若设备/context支持可靠subrange，可在backend内部使用，不改变公共API。Material跨帧稳定constants继续使用persistent buffer/candidate路径，避免每帧重复upload。

将allocator放在RenderScene的替代方案被否决，因为它必须了解backend memory、alignment和completion；把mapped pointer暴露给上层也被否决，因为会破坏复制时机、flush和线程边界。

### 7. Vulkan 使用 completion-scoped descriptor arena

新增窄职责的Vulkan descriptor pool manager，由`VulkanDevice`拥有并逐项注入command context；它不是service locator。每个recording context从manager获取page并局部分配多个descriptor sets，page达到容量后按需增长。command list强持有使用的pages；queue submit标记retire completion，manager只在completed value达到后reset/reuse。不得为单个physical packet创建/销毁独立pool，也不一次预申请不可控数量。

第一版packet cache保持recording-local。physical packet key使用当前physical set layout signature和resolved active values：

```text
physical-set layout signature
+ sampled/storage view identities
+ sampler identities
+ non-dynamic buffer identity/offset/range
+ dynamic uniform backing buffer identity/range size
```

不包含debug name、完整Pipeline其他sets/state、inactive logical values或dynamic slice offset。key持有或由cache伴随强引用保护对象identity，避免地址复用。未来增加跨帧persistent cache只改变Vulkan backend内部，不改变RHI contract。

### 8. Vulkan uniform buffer 默认使用 dynamic descriptor mapping

ShaderCompiler的Vulkan resource类型仍是uniform buffer；Vulkan binding layout创建将引擎constant-buffer entries映射为`VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC`，descriptor写入backing buffer和range，bind时按physical set/binding规范顺序提供dynamic offsets。physical set 0继续一次聚合Global+View。

Cook/runtime验证`maxDescriptorSetUniformBuffersDynamic`、per-stage uniform limits、offset alignment和range。五组每组v1最多一个聚合constant buffer，因此portable profile最多需要五个dynamic uniform bindings、set 0最多两个；仍按真实device limits检查，不能依赖桌面GPU。

若某backend path不能安全提供dynamic descriptor，应在capability/profile检查返回`Unsupported`；D3D11 fallback不受Vulkan descriptor capability影响。公共层不增加`dynamic` flag，因为它是backend优化/映射选择而非Shader资源语义。

### 9. 各领域owner的最终状态

`ViewInfo`每frame持有canonical CPU values、一个transient `RHIUniformBufferSlice`和一个View `RHIBindingSetRef`；删除adapter vector和find/add API。View准备不接收Program/layout，并在首个消费View的pass前完成一次。

`MaterialRenderProxy`从完整Material schema/default/override构造persistent constant buffer和resource superset set；cache失效只由parameter value、sampler、texture binding generation、schema/data-layout generation决定。删除active/staged binding layout引用。不同Base/Shadow/Depth variants由RHI选择active子集。

Object helper按canonical Object schema写bytes、从context取得slice并创建Object logical set，不接收Program/layout。相同primitive数据需要被多个mesh pass消费时，调用方可在其frame-local lifetime内复用set；第一版不引入GPU Scene。

Tonemap/ImGui等具体pass按Pass或其shader声明的logical group建立set，使用稳定ID，不写slot。`MeshDrawCommand`和`RHIGraphicsBindings`形状保持不变；draw execution仍原子提交五组引用。

### 10. Shader artifact 与 cache 迁移

扩展中立ShaderFormat record，使Program bindings和stage reflection同时保存binding ID、uniform data size/hash/ABI。提升ShaderMapEntry required format version和target mapping version，并更新hash输入、writer、reader、fixtures、runtime loader、ShaderMap/RHI program cache key。旧Entry直接报版本错误；生成目录和部署shader全部clean rebuild，不提供字段默认值或旧reader。

RHI pipeline/layout/cache equality覆盖新增identity与ABI字段。target binding hash继续覆盖target slots；logical/data layout hash不混入native mapping hash，便于mapping-only变更准确失效Pipeline而不失效logical resource data。

### 11. 三后端映射

| 公共语义 | Vulkan 1.1 / ES3.1 | D3D11 FL11_0 / SM5 | D3D12 |
| --- | --- | --- | --- |
| Program layout | 四physical set映射和native binding | stage/register-class mapping | root signature/table mapping |
| Logical set | frontend普通immutable对象 | 同左 | 同左 |
| Active resolve | ID→set/binding | ID→stage b/t/s/u | ID→root/table/register |
| Transient uniform | host-visible page + dynamic offset | pooled standalone CB；可选subrange | upload page + aligned GPU VA/CBV |
| Native packet cache | recording-local descriptor packet | context slot state cache | command-list descriptor/root state cache |
| Lifetime | descriptor/uniform pages到completion | buffer/immutable packet到query completion | heap/upload pages到fence completion |

未实现D3D backend继续返回`Unsupported`，但公共mock tests必须覆盖stage-specific mappings、subrange fallback contract和无native术语泄漏。

## Risks / Trade-offs

- [Risk] 一次修改ShaderFormat、RHI、Vulkan和全部调用方，编译中间态较大 → 先增加可独立验证的metadata字段和tests，再在一个明确切换点替换公共BindingSet descriptor；切换后立即迁移所有调用方并删除旧API，不发布双轨状态。
- [Risk] Superset logical set会强持有Material完整schema资源 → Material owner本来就持有这些资源；recorded native packet只保活active子集，避免把未使用资源扩散到GPU in-flight lifetime。
- [Risk] recording-local descriptor cache每frame仍需分配和写部分static descriptors → 分页pool消除per-packet pool创建，dynamic offsets消除高频constant descriptor churn；跨帧cache后置且不需要公共API变化，先用observation计数确认瓶颈。
- [Risk] transient page作为bindable `RHIBuffer`需要重构现有Vulkan upload page所有权 → 使用单一allocation/page owner并让RHI buffer view持有它，禁止复制native memory owner或建立第二套page manager。
- [Risk] logical set创建时无法判断特定Program是否完整 → 这是Program-independent复用的必要代价；创建时做结构/device validation，draw前由Pipeline resolver做required active validation并缓存成功packet。
- [Risk] dynamic uniform descriptor limits在低端移动设备不足 → Cook按portable profile验证，runtime按真实limits复核并返回`Unsupported`；不静默切回Program-specific adapters。
- [Risk] active `add-tonemap-imgui-output-passes`仍在演进旧调用点 → 本change把所有仓库内`RHIBindingSetDesc`生产者和测试fake列为删除门槛，不能以该change未归档为理由保留旧入口。

## Migration Plan

1. 先按OpenSpec顺序同步/归档已完成的`modularize-scene-mesh-passes`，记录`add-tonemap-imgui-output-passes`当前代码作为必须迁移的source baseline；本change不修改其历史artifact来伪造一致。
2. 扩展ShaderFormat canonical ID/data-layout hash record，提升Entry/mapping version，更新compiler、reflection、reader/writer、hash和fixtures；clean重编全部shader产物，旧产物加载测试必须失败。
3. 扩展RHI shader reflection和binding layout entry，更新descriptor canonicalization、equality、cache key、limits validation和Program conversion，在旧BindingSet切换前保证metadata端到端不丢失。
4. 单批切换`RHIBindingValue`/`RHIBindingSetDesc`，实现frontend-only logical set与Pipeline active resolver，删除backend create hook、`VulkanBindingSet`和完整layout equality要求。
5. 实现Vulkan resolved binding materialization、physical-set signature、descriptor arena、recording-local packet cache与dynamic offsets，保持Global+View原子set 0和command-list completion lifetime。
6. 实现公共transient uniform API及Vulkan bindable uniform pages，迁移Global/View/Pass/Object高频constant路径；Material保留persistent candidate更新。
7. 迁移View、Material、Object、BasePass、Tonemap、ImGui及所有tests/fakes，删除adapter cache、per-layout cache、per-draw独立uniform helper和全部slot写入。
8. 同步三份Active设计文档与OpenSpec相关capability，搜索确认旧API、旧Entry version、adapter和Program-layout materialize路径无残留。
9. 完成ShaderCompiler/ShaderMap/RHI/Vulkan/RenderScene故障注入与计数测试、Windows Debug全量构建和CTest、Vulkan validation Cube/resize/exit冒烟。任何切换失败以整个change源码与生成产物一起回退，不恢复兼容wrapper或双reader。

删除旧实现的硬门槛为：仓库中没有`RHIBindingSetDesc::layout`、slot-based `RHIBindingValue`、`create_binding_set_impl()`、`VulkanBindingSet`、View adapter API、Material cached Program layout和旧Shader artifact；同一View跨不同target mapping只上传/创建一个logical set；Object draws不再线性增加GPU buffer和descriptor pool创建；全部失败路径保持可诊断且无validation warning/error。
