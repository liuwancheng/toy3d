## 1. Shader binding identity 与产物版本

- [x] 1.1 将canonical `ShaderParameterId`收敛到现有中立ShaderFormat窄头文件，迁移ShaderCompiler、RenderCore与RHI引用并删除RenderCore重复alias；以全仓`rg`确认只有一套类型定义，构建ShaderCompiler frontend/layout tests验证依赖方向无环。
- [x] 1.2 定义完整`ShaderDataLayoutHash`与聚合constant-buffer hash算法，覆盖ABI version、group、buffer ID、总size和全部member layout字段；增加声明重排、同size不同offset、matrix/array stride和hash确定性layout tests。
- [x] 1.3 扩展ShaderFormat Program binding、stage reflection和runtime ShaderMap metadata，保存binding ID、uniform byte size、data layout hash与ABI version；更新semantic hash/equality并用ShaderMap和compiler tests验证metadata round-trip不丢失。
- [x] 1.4 提升ShaderMapEntry required format与target mapping version，更新writer/reader、manifest、fixtures和cache key；验证新Entry round-trip成功、旧版本明确失败、缺字段/篡改hash不发布。
- [x] 1.5 更新RHI Program转换和ShaderProgram cache key，使logical identity/data ABI与target mapping分别参与正确的validation和失效；用不同slot相同ID、相同size不同layout和stage-specific slot测试覆盖。

## 2. 公共 RHI logical BindingSet

- [x] 2.1 扩展`RHIShaderBindingReflection`与`RHIBindingLayoutEntry`的identity/data ABI字段，补齐规范化排序、equality、limits和stage/native overlap validation；构建并运行RHI device frontend、binding和pipeline cache定向测试。
- [x] 2.2 将`RHIBindingValue`改为`binding_id + array_index + resource/range/ABI`，从`RHIBindingSetDesc`删除完整layout并实现frontend结构、owner、range、唯一性验证和canonical ordering；测试invalid ID、empty set、重复array、错误resource choice、跨device与合法superset。
- [x] 2.3 让`RHIDevice::create_binding_set()`直接创建普通immutable logical set，删除`create_binding_set_impl()`、backend fake hooks、`VulkanBindingSet`及其downcast；以全仓搜索和所有RHI test target构建确认无旧创建路径。
- [x] 2.4 实现Pipeline-driven active binding resolver，按group/ID/array/type/constant ABI返回实现内部resolved entries；测试required缺失、type/array/hash错误、不同target slot复用同一set和inactive superset不参与解析。
- [x] 2.5 修改`validate_graphics_bindings()`和graphics draw flush边界：公共bind只检查五组字段/group/device，draw按当前Pipeline验证required groups和active values；用pipeline未使用group、错误字段、pipeline切换和失败不录制draw测试验证。
- [x] 2.6 确保resolved active resources而非整个superset进入resource-state validation和GPU payload；增加inactive未transition纹理可成功draw、active纹理状态错误失败、旧packet保活到completion的测试。

## 3. Recording-scoped transient uniform data

- [x] 3.1 增加`RHITransientUniformDataDesc`、`RHIUniformBufferSlice`和command-context上传入口，定义recording状态、source复制、logical size、layout hash/ABI与alignment validation；用mock context测试空数据、非recording、超限、复制后源失效和跨device range。
- [x] 3.2 将Vulkan upload/uniform page重构为单一native allocation owner可产生bindable`RHIBuffer` slice，按device uniform alignment分页分配并清零padding；用allocator tests验证不重叠、page增长、逻辑size与allocation padding分离。
- [x] 3.3 让Vulkan command list保活实际uniform pages，queue submit按completion退休并回收，discard/recording failure/submit failure走未提交路径；扩展多frame-in-flight和故障注入测试确认GPU读取期间不覆盖。
- [x] 3.4 为D3D11 FL11_0 pooled standalone constant-buffer fallback和D3D12 upload-page/256-byte alignment写公共实现contract测试替身，确认未来backend无需修改公共API且未实现入口仍返回`Unsupported`。

## 4. Vulkan native binding materialization

- [x] 4.1 更新Vulkan binding layout创建：依据新增entry identity构建四个physical set layouts，将engine uniform bindings映射为dynamic uniform descriptor并计算per-physical-set layout signature；测试Global+View set 0、stage visibility、binding冲突和dynamic descriptor limits。
- [x] 4.2 实现device-owned且显式注入context的分页descriptor pool manager/recording arena，一个page服务多个sets并由command list/completion退休；用observation和故障注入验证不再每packet创建pool、page按需增长、失败不泄漏。
- [x] 4.3 重写Vulkan physical packet materializer以消费resolved active entries，按当前target binding一次写入每个physical set并原子聚合Global+View；测试不同Program slot映射、required缺失、错误ABI和失败不录制`vkCmdBindDescriptorSets`。
- [x] 4.4 实现recording-local packet cache，key覆盖physical-set signature、active静态资源和uniform backing/range但排除dynamic offset；测试同buffer不同Object offset复用packet、texture view变化不误复用、无关Pipeline state和inactive资源不进入key。
- [x] 4.5 按physical set/binding规范顺序生成并绑定dynamic offsets，更新graphics dirty-state与packet retain；在validation测试中覆盖多draw offsets、Pipeline切换、set 0双dynamic UBO和最多四个bound sets。

## 5. RenderCore 与 RenderScene 调用方迁移

- [x] 5.1 将View uniform准备改为每View一次canonical bytes、transient slice和logical View BindingSet，删除`view_binding_adapters_`及find/add/resolve adapter API；测试同View不同Program target slot只上传一次、只创建一个logical set。
- [x] 5.2 将Object uniform helper改为canonical bytes→transient slice→logical Object set且不接收Program/layout，BasePass复用frame-local结果；用多MeshBatch计数测试确认draw数增长不再线性增加GPU buffer和descriptor pool创建。
- [x] 5.3 将`MaterialRenderProxy`改为从完整Material schema/default/override构建persistent constant资源与logical superset set，失效只依赖parameter/sampler/texture generation/schema ABI；测试跨Program/Pass复用、inactive texture无transition要求和mapping-only hot reload不重建。
- [x] 5.4 迁移BasePass的五组binding组合，使prepare阶段不向View/Material/Object传Program layout或target slot且draw command形状保持稳定；运行RendererSceneOwnership和RenderResourceManager tests覆盖batch skip与render-pass前失败。
- [x] 5.5 迁移Tonemap和ImGui的Pass/Material logical bindings到稳定ID，移除所有直接slot写入并保持BasePass→Tonemap→可选ImGui单graphics list顺序；构建并运行Tonemap/ImGui相关测试。
- [x] 5.6 更新Global/Pass现有及未来helper的统一使用示例和测试，验证unused group使用null、required source缺失可诊断，并以Shadow-style不同target mapping fixture证明无需新增adapter或pass基类。

## 6. 删除旧路径与文档同步

- [x] 6.1 删除per-draw独立`create_uploaded_shader_uniform_buffer`高频调用路径、View adapter、Material cached Program layout、slot-based BindingValue和完整layout BindingSet比较；用`rg`硬门槛确认旧符号/字段/诊断无残留且不保留alias或wrapper。
- [x] 6.2 clean重编并部署全部受影响ShaderMapEntry/生成shader产物，确认runtime只接受新version且生成目录、部署目录与manifest/hash完全一致。
- [x] 6.3 同步`document/rhi-design.md`、`document/rhi-binding-aggregation-design.md`和`document/shader-system-design.md`，更新`document/index.md`仅在入口/status变化时修改；运行文档关键术语搜索和OpenSpec严格验证确认无active adapter/旧slot contract冲突。

## 7. 集成验证与交付门槛

- [x] 7.1 增加端到端故障注入和observation测试，覆盖Entry旧版本、constant ABI mismatch、logical set创建、transient page、descriptor page/packet分配、draw中止、discard、submit failure和completion回收，并运行全部相关test targets。
- [x] 7.2 主agent完成实现复查，逐项核对五份delta specs、无backend类型泄漏、D3D11/D3D12/移动Vulkan可实现性、所有权与错误码保留，并运行`git diff --check`和OpenSpec strict validation。
- [x] 7.3 由独立sub-agent按Windows x64 Debug重新配置，构建ShaderCompiler、RHI/ShaderMap/RenderResource/RendererScene/Tonemap/ImGui测试目标与`Toy3dEditor`，执行定向CTest和全量CTest并报告完整命令、结果和未覆盖平台。
- [x] 7.4 由独立sub-agent完成Vulkan validation Cube冒烟，验证多draw正确画面、BasePass→Tonemap→可选ImGui、resize/minimize/restore、正常退出、四physical sets、dynamic offsets和无validation warning/error；主agent根据结果修复后重复独立终验。
