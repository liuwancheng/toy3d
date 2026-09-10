## Why

上一批改动已让 logical `RHIBindingSet` 脱离 Program target slot，但 RenderScene 调用方仍需手工生成 `ShaderParameterId`、重述 constant member offset、计算 data layout hash、填写 Shader ABI version 并拼装底层 RHI descriptor。这既让 Pass 作者承担 Shader ABI 细节，也让 `.shader` schema 与 C++ serializer 存在漂移风险，因此需要继续收敛使用层。

## What Changes

- 新增 UE4.27 `Shader Parameter Struct + Metadata` 思路的 Toy3d 版本：由 `.shader` logical schema 和引擎内建 group schema生成强类型 C++ parameters 与不可变 `ShaderParametersMetadata`，不引入 UE 宏、反射系统或对象前缀。
- 新增 RenderCore 内部参数编码与 logical binding 创建能力；调用方只填写强类型字段，编码器负责 canonical bytes、resource values、stable ID、data layout identity 和错误诊断。
- Pass-local 参数使用明确的 transient binding 创建入口；不引入统一 `.prepare()` 协议、Pass 基类、每 Pass binder/adapter 类或动态字符串参数包。
- View、Global、Material、Object 继续按各自 owner 和生命周期持有 binding：业务 Pass 只消费已有引用；Object binding 可在当前 frame 的 mesh/visibility 数据中跨 BasePass、ShadowPass 复用。
- **BREAKING** 从 transient uniform upload descriptor/slice 删除 Shader data layout hash 与 ABI version。上传 API 只负责字节复制、对齐和 recording/completion 生命周期；constant ABI 由 RenderCore metadata 附加到 logical uniform binding。
- **BREAKING** RenderScene 正式代码不再直接构造 `RHIBindingValue`/`RHIBindingSetDesc`，不再调用 `make_shader_parameter_id()` 或填写 ABI/hash；Tonemap、ImGui、View、Object 和后续 ShadowPass 迁移到 typed parameters。
- **BREAKING** Material 公共更新 API 不再要求调用方提供裸 `ShaderParameterId`；canonical parameter name 在 Material schema 边界解析，RT cache 按完整 Material schema 构建 logical superset，不再遍历具体 Program active layout决定资源集合。
- 提升并严格验证必要的 generated metadata/Shader artifact contract；每个生成单元只输出一个位于构建目录的 `.generated.h`，不生成配套 `.cpp`，并由 CMake 作为 `GENERATED` source 显式加入 VS 工程分组，旧手写 metadata 路径一次性删除，不保留双轨 wrapper。

## Capabilities

### New Capabilities

- `rendercore-typed-shader-parameters`: 定义强类型 Shader parameters、生成 metadata、canonical encoder、transient/persistent logical binding 创建边界以及上层禁止接触的低层身份字段。

### Modified Capabilities

- `game-render-framework/view-render-flow`: View/Object binding 改由共享 typed metadata 驱动，View 每 View 创建一次，Object 在 frame-local mesh 数据中跨多个 mesh pass 复用，BasePass 不构造 Shader ABI descriptor。
- `game-render-framework/material-updates`: Material 参数更新改为 schema 名字接口，MaterialRenderProxy 从完整 Material schema 创建 Program-independent logical superset。

## Impact

- ShaderCompiler 需要从 `.shader` 与内建 Global/View/Object schema 输出单文件 C++ parameters metadata，并建立生成头文件的 CMake 依赖、VS 工程关联与确定性测试。
- ShaderFormat/ShaderMap 需要明确完整 logical schema、Program active layout 与 generated metadata identity 的关系和版本校验。
- RenderCore 新增 metadata/encoder 和 typed binding 创建入口；公共 RHI transient uniform descriptor/slice 缩减为纯内存与生命周期语义。
- RenderScene 的 View、BasePass、Tonemap、ImGui、Material 与 Object draw data 调用方式改变；未来 ShadowPass 直接复用同一 View/Material/Object bindings。
- Vulkan、未来 D3D11/D3D12 的 native binding/materialization contract 不变，仍只消费 logical BindingSet 与当前 Pipeline active layout。
