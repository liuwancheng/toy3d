## MODIFIED Requirements

### Requirement: 普通 setter 只投递 owned value
scalar、vector 和 texture setter SHALL先通过Material完整parameter schema按canonical name解析parameter identity与value type，再更新GT override，并投递stable `MaterialRenderProxy` identity、已解析parameter identity与owned value。业务调用方 MUST NOT构造或传入裸`ShaderParameterId`；setter不得创建RHI buffer、material binding、submit、flush或wait。未知parameter、类型不兼容或Texture Asset无效时 MUST返回可诊断失败且不得投递部分update。名字解析只发生在低频Material编辑边界，RT不得按字符串查找参数。

#### Scenario: 连续 setter
- **WHEN** Draw 前同一参数被连续修改
- **THEN** RT MUST 按 FIFO 应用，Draw MUST 使用最终值

#### Scenario: 参数类型错误
- **WHEN** scalar setter使用texture parameter name或未知name
- **THEN** MaterialInstance MUST保持原GT override、不enqueue update并返回可诊断失败

### Requirement: Draw 前按需物化
RT SHALL只在一个可见`MeshBatch`实际使用该`MaterialRenderProxy`时，根据完整Material schema、instance override与parent Material default生成或更新persistent constants，解析`TextureResource`当前active view/binding generation，并物化或复用一个Program-independent Material logical superset BindingSet。Material schema MUST独立于任一具体Program active layout；Shader variant只决定draw时由Pipeline resolver消费superset中的哪些active values。

Material constants、logical BindingSet、backend native packet和实际active RHI resources MUST由对应owner或当前recording/command list保活到安全的queue completion。MaterialRenderProxy自身不得直接submit、等待GPU、持有viewport frame token、Program target mapping或backend descriptor。

#### Scenario: 未参与 Draw 的 dirty Material
- **WHEN** MaterialRenderProxy被更新但本帧无可见Primitive使用
- **THEN** 系统 MUST NOT为其强制创建或更新Material constants/logical binding

#### Scenario: 同一帧多个 Draw 复用 Material
- **WHEN** 两个可见MeshBatch使用相同MaterialRenderProxy且中间没有parameter、texture generation、sampler或Material schema变化
- **THEN** Renderer MUST复用同一个Material logical BindingSet，即使两个Program的active bindings或target mapping不同

#### Scenario: Shader variant 使用 Material 子集
- **WHEN** Material schema包含一张当前Shader variant未使用的纹理
- **THEN** MaterialRenderProxy MUST保留同一个完整logical superset，RHI只验证、transition和保活当前variant的active子集

### Requirement: Material 只填充五组 binding 中的 Material group
Forward Base Pass和其他mesh pass MUST分别组合Global、View、Pass、Material、Object五个logical Binding Group。MaterialRenderProxy MUST只从完整Material schema产生Material group，不得写入View/Object group，不得感知Program target slot、Vulkan physical set、D3D register/root mapping或backend descriptor类型。

最终Draw SHALL通过`RHIGraphicsBindings`原子提供所需logical groups；Vulkan ES3.1 profile的Global+View physical set 0聚合和D3D11/D3D12 native mapping继续由RHI/backend处理。

#### Scenario: Material group 缺失
- **WHEN** 当前Shader Program要求Material group而MaterialRenderProxy无法从完整schema生成兼容logical binding
- **THEN** 对应MeshBatch MUST被跳过并产生可诊断错误，其他合法batch MAY继续录制

### Requirement: Material binding cache 使用明确失效条件
scalar/vector value改变、sampler value改变、texture representation identity改变、`TextureResource` RT-only binding generation改变，或Material parameter schema/data-layout candidate成功发布时，MaterialRenderProxy MUST把Material logical binding标记为dirty。同一native texture/view的内容更新若不改变binding generation，MUST NOT仅因内容upload强制重建；Program、target mapping或只改变active resource subset的Shader variant MUST NOT作为Material logical binding失效条件。

#### Scenario: Texture 内容更新但 view 不变
- **WHEN** Material使用的Texture只更新mip内容且active view identity/binding generation不变
- **THEN** MaterialRenderProxy MUST继续复用logical binding，command list通过resource state与active strong ref使用更新后的内容

#### Scenario: Program active layout 改变
- **WHEN** Material schema和parameter values不变，但新Program使用不同资源子集或target mapping
- **THEN** MaterialRenderProxy MUST继续复用原logical BindingSet，由Pipeline resolver选择新active subset
