# game-render-framework/texture-resources Specification

## Purpose
定义 Texture Asset、地址稳定的 TextureResource、initial upload、内容更新、native replacement、candidate 发布和 Material binding cache 失效边界。

## Requirements

### Requirement: Stable representation address
Texture Asset SHALL 拥有地址稳定的 `TextureResource` allocation；GT 不得读取或修改其 RT state。MaterialRenderProxy MAY 保存 non-owning `TextureResource*`，但 MaterialInstance GT state MUST 持有 `TextureRef` 覆盖 Proxy 使用期。最终 Texture release前，所有 MaterialRenderProxy texture update/remove MUST 先 enqueue，随后 Texture ownership路径 MUST 把 TextureResource ownership移入最后一条 release RenderCommand并由 RT析构。

#### Scenario: Material 切换 Texture
- **WHEN** GT 将材质参数切换到另一 Texture
- **THEN** 新 Asset 强引用 MUST 先建立，proxy update MUST 排在旧 Asset 引用释放之前

#### Scenario: 最后一个 TextureRef 释放
- **WHEN** 最后一个 GT TextureRef 即将释放且 MaterialRenderProxy 已切换或移除其引用
- **THEN** Proxy update/remove MUST 先进入 FIFO，TextureResource ownership-transfer release MUST 后进入并由 RT执行

### Requirement: TextureDesc 完整验证 Texture2D payload
Texture 创建 MUST 验证非零 width/height、合法 `PixelFormat`、合法 mip count 与逐 mip extent，以及每个 payload 的 row pitch、slice pitch、byte count和算术溢出。TextureDesc不得携带 PNG/JPEG/DDS 等外部文件编码、可重新 Cook source data、RHI usage、`Vk*`、D3D resource、native layout/heap或 platform image object。

`PixelFormat` metadata MUST 提供 block width、block height 与 bytes per block。未压缩格式按 1×1 block 处理；BC、ASTC、PVRTC 等 block-compressed format 的最小 row pitch、slice pitch 与 byte count MUST 按横纵 block count 计算并检查溢出，不得假定每 texel 固定字节数。

Cook MUST 按目标 sampled-texture profile 验证 cooked `PixelFormat` payload；runtime MUST 在 `TextureResource` 构造固定 RHI descriptor 后验证完整的 `PixelFormat`、`ShaderResource | CopyDestination` 和单采样组合。任一阶段不支持时 MUST 返回可诊断的 `Unsupported`，不得把 RHI usage 反向存入 Asset descriptor。

第一阶段若公共 Asset format只能映射到部分 backend/profile，Cook/runtime MUST 返回可诊断 `Unsupported` 或等价失败；不得截断 mip、缩小 pitch、猜测缺失 bytes或无操作后成功。

#### Scenario: Row pitch 不足
- **WHEN** 一个 mip 的 row pitch 小于其 format和 width要求的最小字节数
- **THEN** Texture 创建 MUST 失败并保留精确诊断，不得创建 TextureResource或 enqueue upload

#### Scenario: Mip payload 溢出
- **WHEN** extent、pitch和slice count计算超出可表示范围或 payload byte count不足
- **THEN** validation MUST 在任何 allocation/copy前失败

#### Scenario: Block-compressed row pitch
- **WHEN** BC、ASTC 或 PVRTC payload 的 width/height 不是 block extent 的整数倍
- **THEN** validation MUST 使用向上取整的 block count 计算最小 row/slice pitch，不得按 bytes-per-texel 截断

### Requirement: Initial upload 通过 RenderResourceManager
新 `TextureResource` SHALL 在 RT进入 PendingUpload并由 `RenderResourceManager::record_pending_uploads()` 创建固定为 `ShaderResource | CopyDestination`、单采样的空 RHI texture和 shader-resource view，复制 cooked bytes到 RHI-owned staging、录制逐 mip upload和必要 `RHIAccess` transition。TextureResource自身不得创建 context、submit、present、flush或 wait。

当全部必要 mip recording成功、view创建有效且Material binding validation完成后，该 resource MAY 在当前同一 list的后续 test/Base Pass局部使用；长期 Ready和active publication只能在business submit成功后发生。frame abort、list discard或submit failure MUST保留可重录CPU payload。

#### Scenario: Initial upload 与 Draw 同帧
- **WHEN** 4×4 RGBA8 Texture 的 create/upload/transition/view validation在当前 list的Draw前全部成功录制
- **THEN** Material binding MAY在该list后续使用该view，但TextureResource在submit成功前 MUST仍为PendingUpload

#### Scenario: Initial upload frame abort
- **WHEN** Texture uploads已录制而frame在submit前abort
- **THEN** TextureResource MUST保持PendingUpload、不得发布active view/binding generation，并能在后续有效frame重录

### Requirement: 内容更新不替换 native view
descriptor、format、mip结构和view identity不变时，内容更新 MUST只向现有active RHI texture录制upload/transition，不更换active view、不递增binding generation、不强制重建Material binding。第一阶段单graphics queue上的更新 MUST排在旧Draw后并在新Draw前使用公共state tracker排序，不允许resource私自wait GPU idle。

#### Scenario: 更新一个 mip 内容
- **WHEN** descriptor、format 和 view identity 不变
- **THEN** active texture/view MUST 保持，只有 GPU 内容更新

#### Scenario: 内容更新 submit 失败
- **WHEN** 内容更新 list明确未成功submit
- **THEN** active texture/view与binding generation MUST保持，CPU update payload MUST按资源策略保留以供重试或明确报告失败

### Requirement: Native replacement 使用 candidate
extent、descriptor、mip结构、format或hot reload需要替换native texture/view时，active MUST保持可用；TextureResource MUST在私有RHI refs中创建完整candidate、录制全部upload/transition并验证view。candidate只能在对应business submit成功后原子发布为active并递增RT-only binding generation，不新增公开candidate类型。

初次active publication MUST建立非零binding generation；后续只有active view identity变化才递增。同一native texture/view内容更新 MUST保持generation不变。

#### Scenario: Candidate submit 失败
- **WHEN** candidate list 未成功 submit
- **THEN** active与binding generation MUST不变，candidate MUST丢弃或保留完整payload进入明确重试路径

#### Scenario: Candidate submit 成功
- **WHEN** candidate 成功 submit
- **THEN** candidate MUST 成为 active，RT-only binding generation MUST 递增

#### Scenario: Candidate 部分创建失败
- **WHEN** candidate RHI texture创建成功但view、某个mip upload或transition validation失败
- **THEN** candidate MUST整体失败且不得发布，active路径 MUST继续可用

### Requirement: Binding 保活实际 view
MaterialRenderProxy 必须以 TextureResource identity与RT-only binding generation判断Material binding cache是否失效。Material logical binding与command list MUST强引用解析时的实际RHI view/resource，使后续active replacement不改变已录制GPU工作；MaterialRenderProxy不得把TextureResource pointer当作GPU lifetime root。

#### Scenario: Draw 后替换 Texture
- **WHEN** 旧 view 已录制进 Draw 且 Texture 发布新 active view
- **THEN** 旧 view MUST 保活到对应 queue completion

#### Scenario: 内容更新不失效 Material binding
- **WHEN** 同一active texture/view只更新内容且binding generation不变
- **THEN** MaterialRenderProxy MUST继续复用兼容binding，不得仅因pixel bytes变化重建descriptor
