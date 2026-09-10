## 1. 锁定完整 schema 与 generated metadata contract

- [x] 1.1 在 `Toy3dShaderFormat` 中定义完整 logical schema record、schema identity、generated format version 以及它们与 Program active layout 的一致性规则，并用 round-trip、损坏 identity、active binding 非 schema 子集测试验证 publication 会在进入 RHI 前失败
- [x] 1.2 为内建 Global/View/Object schema 建立与 `.shader` Pass/Material schema 共用的规范化输入，删除运行时手写布局作为 schema 权威的可能性，并用声明重排、相同输入重复生成和五组 identity golden test 验证结果确定
- [x] 1.3 定义 `.shader` 名字到 C++ 类型/字段/函数标识符的版本化映射与冲突诊断，覆盖关键字、非法字符、大小写归一化碰撞和重复资源名测试，验证 codegen 不追加不稳定序号

## 2. 实现 Shader parameters C++ codegen 与构建集成

- [x] 2.1 扩展 ShaderCompiler，为内建 schema 和每个 Shader 的完整 Pass schema 各生成一个自包含`.generated.h`，其中包含普通 C++17 parameters struct、不可变 metadata accessor 与薄`inline encode_shader_parameters`重载且不生成配套`.cpp`，并用 Tonemap、ImGui、View、Object fixture 验证字段类型、metadata 和 canonical schema 一致
- [x] 2.2 实现 generated header 的确定性输出、仅内容变化时替换和依赖追踪，将输出限定到 `<build>/generated/shader_parameters/`，并用两次生成字节比较、schema 单文件变更和 stale output 清理测试验证增量行为
- [x] 2.3 用显式 CMake custom command/output/dependency 生成 headers，将其标记为`GENERATED`、通过`target_sources()`接入实际消费目标并归入 VS `Generated Files\\Shader Parameters` source group；验证全新 build tree 在编译 Runtime 前完成生成、生成文件可在 VS 中浏览和调试，且关闭开发期 Entry loading 时仍无 ShaderCompiler/RenderCore 反向链接环

## 3. 建立 RenderCore metadata encoder 与 typed 创建入口

- [x] 3.1 在 RenderCore 定义只读 `ShaderParametersMetadata` 与内部 `ShaderParameterEncoder`，实现零初始化 canonical bytes、逐字段 typed constant 写入及独立 texture/sampler/buffer value 收集，并用宿主 padding、matrix/array stride、未写 padding 为零和资源不进入 constant bytes 的单元测试验证
- [x] 3.2 实现薄的 `create_transient_shader_binding(device, context, parameters)` 入口，通过生成的普通重载取得 metadata 和编码字段，不引入 `.prepare()`、Pass 基类、binder/adapter、字符串 builder 或复杂模板探测，并用编译期调用 fixture 验证 Tonemap 与 Shadow-style parameters 可直接接入
- [x] 3.3 在 typed 创建路径中先完成 group/schema、required resource、数组和 resource owner/type 校验，再执行 upload 与一次 logical BindingSet 创建；用 fault-injection 测试验证确定性错误不产生 upload/半成品 set，RHI 失败保留原始错误码
- [x] 3.4 在 Shader artifact/Program publication 与 binding 创建边界校验 generated metadata identity、完整 schema identity、constant ABI 及 active-subset 关系，并用仅 byte size 相同但 identity 不同的反例验证拒绝近似兼容

## 4. 收窄 transient uniform RHI contract

- [x] 4.1 从 `RHITransientUniformDataDesc` 删除 binding ID、layout hash 和 ABI 字段，从 `RHIUniformBufferSlice` 删除 layout hash 和 ABI 字段，更新公共 validation、测试 fake 与 Vulkan 实现，并验证非 Shader bytes 可仅凭 source/debug name 上传
- [x] 4.2 由 RenderCore 根据 generated metadata 为 uniform `RHIBindingValue` 附加 binding ID、data layout hash 与 Shader ABI version，验证 Pipeline resolver 仍能拒绝不兼容 ABI，且 Vulkan dynamic uniform offset、四 physical-set 聚合和 packet cache 行为不变
- [x] 4.3 补充跨后端 contract 测试，验证同一纯内存上传接口可映射到 Vulkan dynamic uniform、D3D12 对齐后的 suballocation 与 D3D11 standalone constant buffer fallback，公共头文件不出现 native descriptor/register/root 参数语义

## 5. 迁移 Pass、View 与 Object owner

- [x] 5.1 将 Tonemap 与 ImGui 的 Pass group 迁移到各自 generated parameters 和 typed transient 创建入口，删除手写 ID、offset、layout hash、ABI 与 `RHIBindingSetDesc` 组装，并用现有 pass 测试验证 required resource 错误及合法录制结果
- [x] 5.2 用内建 generated View parameters 替换 `view_uniform_shader_parameters` 手写 serializer，将批量入口收敛为 `create_view_shader_bindings(...)`，由每个 `ViewInfo` 每帧持有一次创建的 binding，并用多 View、BasePass/Shadow-style 双消费者和无效矩阵测试验证不重复上传
- [x] 5.3 用内建 generated Object parameters 替换手写 Object serializer，在 frame-local mesh draw data 中按 `PrimitiveSceneProxy` identity 与 object-data generation 复用 binding，并用同一 Primitive 进入 BasePass 和 Shadow-style fixture 的测试验证每帧只上传/创建一次且帧结束释放
- [x] 5.4 迁移 BasePass 只组合 Global、View、Pass、Material、Object owner-provided bindings；Program 要求但 owner 缺失时诊断并跳过该 batch，并用 mixed-validity draw-list 测试验证其他合法 batch 仍可录制

## 6. 迁移 Material schema、更新与 persistent binding

- [x] 6.1 扩展 Material 的完整 runtime schema，包含 canonical name、parameter ID、value/resource type、default、constant layout及schema identity，且不依赖任一 Program active layout；用不同 variant 资源子集测试验证共享同一完整 schema
- [x] 6.2 将 `MaterialInstance` GT-facing setter 改为按 canonical name 的 typed overload，在完整 schema 中解析并验证后仅向 RT 投递已解析 ID 和 owned value；用未知名字、类型错误、连续更新 FIFO 与字符串不跨线程测试验证失败不产生部分更新
- [x] 6.3 将 `MaterialRenderProxy` persistent constants 与 logical superset 物化改为完整 Material schema 驱动，复用 canonical encoder 写入规则但保留既有 candidate publication/completion 生命周期，并用不可见 dirty material、同帧多 draw 和 candidate replacement 测试验证按需创建与安全发布
- [x] 6.4 收敛 Material binding cache 失效条件为 schema/data ABI、value、sampler、texture representation 或 RT binding generation 变化；用 mapping-only、active-subset、texture 内容更新但 view 不变的测试验证不会无效重建

## 7. 删除旧入口并执行架构门禁

- [x] 7.1 删除旧 View/Object serializer、Pass 手写 metadata、Material 裸 ID 公共 setter及所有兼容 wrapper，使用 `rg` 验证 RenderScene 正式代码不再调用 `make_shader_parameter_id()` 或直接构造 `RHIBindingValue`/`RHIBindingSetDesc`
- [x] 7.2 用头文件依赖与链接检查验证 ShaderFormat/ShaderCompiler 不依赖 Runtime，RenderScene 不依赖 backend，generated metadata/parameters 类型不进入公共 RHI backend hook，且未新增 `Prepared*`、adapter、Pass 基类或全局可变 metadata registry
- [x] 7.3 更新 `document/index.md` 指向的 Active RHI、binding aggregation、Shader system 设计及受影响的主 OpenSpec specs，验证长期 contract 只记录最终边界且不包含施工流水账或旧双轨说明

## 8. 集成与运行验证

- [x] 8.1 从干净生成状态运行 Windows x64 Debug CMake configure，构建 ShaderCompiler、Runtime、相关测试目标与 `Toy3dEditor`，再运行完整 CTest，并验证 generated headers/artifacts 可重复生成且工作区不产生应提交的构建产物
- [x] 8.2 运行 Vulkan validation Cube 冒烟，验证 Tonemap、ImGui 与 BasePass draw/present 无 validation error，Global+View 仍原子映射到 physical set 0，View/Object 在多 Pass fixture 中没有重复 transient upload
- [x] 8.3 对 Vulkan ES3.1 profile、D3D11 FL11_0 与 D3D12 的 schema limits、constant alignment、logical binding 和 active resolver 定向测试进行最终复核，验证新增 ShadowPass 只需 schema、generated parameters 与现有 owner bindings，不需要修改公共 RHI 或 physical mapping
