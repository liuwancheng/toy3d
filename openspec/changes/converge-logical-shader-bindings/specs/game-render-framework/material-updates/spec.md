## MODIFIED Requirements

### Requirement: Draw 前按需物化
RT SHALL只在一个可见`MeshBatch`实际使用该`MaterialRenderProxy`时，解析instance override/parent Material default、依据canonical Material parameter schema生成或更新persistent constants、解析`TextureResource`当前active view/binding generation，并物化或复用一个由Material group拥有、按稳定ShaderParameterId索引且不依赖Program target layout的logical BindingSet；多个dirty FIFO update MAY合并为一次构建。

Material constants、logical BindingSet、backend native packet和实际active RHI resources MUST由对应owner或当前recording/command list保活到安全的queue completion。MaterialRenderProxy自身不得直接submit、等待GPU、持有viewport frame token、Program binding layout、target slot或backend descriptor。

#### Scenario: 未参与 Draw 的 dirty Material
- **WHEN** MaterialRenderProxy被更新但本帧无可见Primitive使用
- **THEN** 系统 MUST NOT为其强制创建或更新Material constants/logical binding

#### Scenario: 同一帧多个 Draw 复用 Material
- **WHEN** 两个可见MeshBatch使用相同MaterialRenderProxy且中间没有parameter、texture generation、sampler或Material schema变化
- **THEN** Renderer MUST复用同一Material logical BindingSet，即使两个Program的target mapping不同；backend只保活各draw实际使用的active resources

#### Scenario: Shader variant 使用 Material 资源子集
- **WHEN** Material logical BindingSet包含完整schema资源而当前Shader variant只使用其中一部分
- **THEN** MaterialRenderProxy MUST保持同一个logical set，RHI MUST只解析、验证和绑定当前variant的active子集

### Requirement: Material binding cache 使用明确失效条件
scalar/vector value改变、sampler value改变、texture representation identity改变、`TextureResource` RT-only binding generation改变，或Material parameter schema/constant data layout candidate成功发布时，MaterialRenderProxy MUST把Material logical binding标记为dirty。同一native texture/view的内容更新若不改变binding generation，MUST NOT仅因内容upload强制重建；Shader Program、target slot、完整Program binding layout或只影响target mapping的hot reload MUST NOT作为Material logical binding失效条件。

#### Scenario: Texture 内容更新但 view 不变
- **WHEN** Material使用的Texture只更新mip内容且active view identity/binding generation不变
- **THEN** MaterialRenderProxy MUST继续复用logical binding，command list通过resource state和active strong ref使用更新后的内容

#### Scenario: 仅 Program target mapping 改变
- **WHEN** active Material schema、constant ABI和resources不变，但新Shader Program为binding分配不同target slot
- **THEN** MaterialRenderProxy MUST继续复用原logical BindingSet，由新Pipeline/backend重新解析target mapping

