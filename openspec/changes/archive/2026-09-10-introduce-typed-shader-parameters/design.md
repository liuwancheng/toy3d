## Context

见 `proposal.md`。`converge-logical-shader-bindings` 已完成底层切换：logical BindingSet按stable ID索引，Pipeline在draw前解析active subset，Vulkan再物化四个physical sets。当前未收敛的是使用层：View/Object/Pass仍在C++中手写ID、member layout、hash和ABI；Material setter与RT cache仍把ID和Program active layout暴露到领域逻辑。

UE4.27的可借鉴点有两条而不是一套统一入口：Global/RDG Pass用`Shader Parameter Struct + FShaderParametersMetadata`让调用者只填字段；mesh path则从View、Pass、Material、Primitive/VertexFactory各owner取得现成uniform/resource并加入draw bindings。Toy3d不具备也不需要UE宏反射、对象系统和RDG，因此以ShaderCompiler codegen替代宏metadata，并保持现有显式SceneRenderer/Pass录制。

实现受以下约束：第一方C++17；生成头文件只能写入构建目录；ShaderCompiler/ShaderFormat不得依赖Runtime；RenderScene不得依赖backend；Vulkan ES3.1 profile仍最多四个bound sets；D3D11 FL11_0与D3D12必须无需改变公共typed-parameter contract即可实现。

## Goals / Non-Goals

**Goals:**

- 让Pass作者只填写强类型C++字段，并以一次明确创建操作获得Pass logical binding。
- 让ID、constant layout、hash和ABI只由schema/metadata产生，RenderScene正式调用点不再拼装底层RHI descriptor。
- 让完整logical schema、Program active layout与target mapping成为三份职责明确的数据。
- 保持Global、View、Pass、Material、Object各自owner、更新频率与生命周期，不因统一编码器而统一资源管理策略。
- 为ShadowPass提供无需新增adapter、Pass基类或RHI接口的直接接入方式。

**Non-Goals:**

- 不引入UE宏、反射注册、`F`/`T`前缀、RDG或RHI thread。
- 不设计通用Pass调度器、Pass基类、`Prepared*`对象或字符串/variant参数builder。
- 不在本change实现D3D11/D3D12 backend；只保持公共接口和测试contract可实现。
- 不把Material任意资产schema强制生成为静态C++ struct；Material继续使用运行时schema与按名字的低频编辑API。
- 不改变Vulkan四physical-set映射、descriptor packet cache或Pipeline active resolver。

## Decisions

### 1. 采用“生成强类型值 + 生成编码函数”，不做运行时C++字段反射

ShaderCompiler为内建Global/View/Object schema和`.shader`中的Pass parameters/resources生成构建目录头文件。以Tonemap为例，调用形状为：

```cpp
TonemapPassParameters parameters;
parameters.exposure_ev = exposure_ev;
parameters.scene_color = scene_color;
parameters.scene_sampler = scene_sampler;

auto pass_binding = create_transient_shader_binding(device, context, parameters);
```

生成类型提供不可变`ShaderParametersMetadata`以及一个对应的显式编码重载。编码重载直接读取每个C++字段并调用内部encoder的typed write/add resource操作，不依赖`offsetof`读取含智能指针的对象，不raw-copy整个struct，也不需要通用运行时reflection容器。

生成代码使用普通struct、普通函数和直接字段访问。为了保持C++17可读性，通用入口只使用一个薄模板把具体类型路由到生成的`shader_parameters_metadata(parameters)`与`encode_shader_parameters(parameters, encoder)`重载；不使用SFINAE、tag dispatch、复杂traits或visitor。生成失败、符号冲突和未知类型在Shader构建阶段诊断。

备选方案：复制UE宏生成metadata。拒绝，因为会引入预处理器元编程、重复`.shader`声明并让C++成为第二schema源。备选方案：运行时字符串字典。拒绝，因为失去编译期字段/type检查且把拼写失败留到draw。

### 2. `.shader`/内建 schema 是单一真源，生成类型不是新的资产contract

Pass metadata从`.shader`的完整`Parameters`与`Resources` block生成，不从SPIR-V/DXBC/DXIL裁剪后的reflection反推。Material完整schema来自`Properties`及其resource/default记录。View/Object/Global使用ShaderCompiler当前已经参与每个Program layout的canonical内建schema，并由同一codegen命令输出一次共享头文件。

生成输出分两类：

```text
<build>/generated/shader_parameters/builtin_shader_parameters.generated.h
<build>/generated/shader_parameters/<shader-key>.generated.h
```

每个生成单元只输出一个自包含的`.generated.h`，不生成配套`.generated.cpp`。参数struct、只读metadata accessor与薄的`inline`编码重载共同位于该头文件；复杂的canonical编码、validation、upload与BindingSet创建仍保留在手写RenderCore实现中，避免生成大量translation unit、链接符号和静态初始化顺序。

生成到构建目录不等于脱离IDE工程。CMake必须把每个generated header标记为`GENERATED`，通过显式`target_sources()`加入实际消费target，并用`source_group("Generated Files\\Shader Parameters")`归入VS工程。生成目录作为build include directory传播给直接消费者。这样VS可以浏览文件、跳转定义、定位编译错误，并在Debug未优化路径中单步进入薄`inline`编码函数；主要断点仍位于手写`ShaderParameterEncoder`和typed binding创建路径。

每个generated record携带schema identity、生成格式version和ToyShaderABI version。ShaderMapEntry继续保存Program active layout；完整schema record另外保存所有参数/default与resource declarations。Runtime publication核对generated metadata identity、完整schema identity与Program引用的active bindings，禁止只比较byte size。

生成文件不提交源码仓库，CMake用显式custom command/output/dependency使`Toy3dRuntime`在编译使用者前生成头文件，并把同一份输出同时登记为IDE可见的generated source。`Toy3dShaderCompiler`只输出文本，不包含或链接Runtime/RHI，因此依赖仍为：

```text
ShaderFormat <- ShaderCompiler -> generated C++
ShaderFormat <- Runtime/RenderCore -> generated C++
```

备选方案：把生成头提交仓库。拒绝，因为会形成源码与生成物双轨并产生stale header。备选方案：从每个Program active reflection生成。拒绝，因为variant裁剪会改变调用方C++类型并破坏logical superset。

### 3. Metadata 与 active layout 分离

`ShaderParametersMetadata`描述一个完整logical group schema，包含：

- group与schema identity；
- 聚合constant-buffer ID、logical byte size、data layout hash和ABI version；
- constant members的ID、类型与canonical布局；
- textures/samplers/buffers的ID、类型、array count和required/default规则。

`ShaderMapProgram::bindings`继续只表达当前Program实际使用的active layout和target mapping。RenderCore encoder依据完整metadata创建logical superset；RHI resolver依据Program active layout选择子集。这使mapping-only或active-subset变化不触发View/Material/Object set重建。

Metadata对象不可变，由生成代码或已验证Shader schema record按值持有，不使用进程全局可变registry或service locator。相同schema的identity用于严格equality与cache invalidation，不能把metadata对象地址作为资产或cache key。

### 4. RenderCore 内部 encoder 是唯一 descriptor 组装点

新增RenderCore内部`ShaderParameterEncoder`，一次编码产生：

```text
zero-initialized canonical constant bytes
+ independent texture/sampler/buffer logical values
+ metadata identity needed by RHIBindingValue
```

`create_transient_shader_binding(device, context, parameters)`的顺序固定为：

1. 获取生成metadata并验证group/schema；
2. 完整编码到CPU临时结果，验证required resources；
3. 若存在constants，调用RHI transient upload复制bytes；
4. 由RenderCore附加constant binding ID、data layout hash和ABI version；
5. 一次调用`RHIDevice::create_binding_set()`创建logical set。

所有确定性校验在任何GPU allocation/upload之前完成。上传或BindingSet创建失败返回原始`RHIErrorCode`，不产生可被draw消费的部分结果。

正式RenderScene代码禁止直接创建`RHIBindingValue`/`RHIBindingSetDesc`。这些公共RHI类型仍保留，因为它们是跨后端logical contract和RHI定向测试入口，不把访问控制或新的Token/Key wrapper用于机械性封锁；通过模块API、代码搜索门槛和测试维持上层边界。

### 5. Transient uniform API 回归纯内存职责

公共类型收敛为：

```cpp
struct RHITransientUniformDataDesc
{
    RHIInitialData source;
    std::string debug_name;
};

struct RHIUniformBufferSlice
{
    RHIBufferRef buffer;
    std::uint64_t offset = 0;
    std::uint64_t size = 0;
};
```

RHI验证recording状态、source、size、device limit与backend alignment；它不理解Shader schema。RenderCore在从slice构建uniform `RHIBindingValue`时使用metadata填入data layout hash/ABI。Vulkan dynamic descriptor、D3D12 256-byte allocation padding和D3D11 standalone buffer fallback均不需要改变此接口。

备选方案：保留ABI字段以便RHI上传时校验。拒绝，因为上传时没有Pipeline required ABI，字段只能由调用方重复填写且不提供额外正确性。

### 6. 五个 group 共享编码机制，但不共享 owner 协议

不建立`.prepare()`接口或统一parameter owner基类：

| Group | schema/value owner | 创建时机 | 消费方式 |
| --- | --- | --- | --- |
| Global | Renderer/device domain | 初始化或global generation变化 | Pass读取缓存binding |
| View | 每帧`ViewInfo` | CPU init/visibility完成后、首个业务render pass前，每View一次 | Base/Shadow等Pass读取同一binding |
| Pass | 具体业务Pass栈上值 | Pass开始render pass前创建transient binding | 当前Pass使用 |
| Material | Material/MaterialRenderProxy | 可见draw首次需要且schema/value/resource generation变化 | 多Program/Pass复用persistent logical superset |
| Object | Primitive canonical values与frame-local draw data | 当前frame首次可见消费时一次 | Base/Shadow等mesh pass复用 |

View批量函数改用明确的`create_view_shader_bindings(...)`命名；调用方随后只使用`view_info.view_binding()`。Object binding由frame-local mesh data/cache关联到Primitive identity与transform generation，visibility只组织关联和复用，不编码metadata、不形成跨帧资源cache。未来SceneVisibility由函数演进为类并非本change前提。

Object cache key使用当前frame内稳定的PrimitiveSceneProxy identity和object-data generation；value保存binding强引用并随SceneRenderer/frame销毁。它不使用仅为查表而新造的公共Key/Token类型。

### 7. Material 使用名字更新、内部ID传输与完整schema物化

GT-facing `MaterialInstance` setter按canonical parameter name和value type重载，例如：

```cpp
material_instance.set_vector("base_color", color);
material_instance.set_texture("surface_tint_texture", texture);
```

setter在Material完整schema中解析名字，验证类型后才更新override并把已解析ID与owned value投递RT。字符串生命周期不跨线程，RT不重复查名。第一版不新增`Handle`/`Token`/`Key`公共类型；若未来性能数据证明名字解析成为问题，再以schema ownership、type和lifetime均明确的typed parameter reference单独立项。

MaterialRenderProxy持有完整Material schema identity和parameter/resource superset，不再从`ShaderMapProgram::bindings`枚举Material资源。Program candidate仍决定Shader与effective graphics state，但仅Program mapping或active subset变化不使Material binding dirty；schema/data ABI变化走完整candidate replacement。

Material constants继续使用persistent RHI buffer/candidate路径，不改为每帧transient upload。通用encoder可复用canonical写入逻辑，但persistent资源创建、submit publication和cache invalidation仍属于MaterialRenderProxy/RenderResourceManager策略。

### 8. Pass 代码形状与 ShadowPass 扩展

Tonemap/ImGui等Pass直接包含对应generated header、填充参数并调用`create_transient_shader_binding()`。BasePass通常没有Pass group时保持null；View/Material/Object来自owner。新增ShadowPass时只需：

```text
shadow.shader Parameters/Resources Pass block
-> generated ShadowPassParameters
-> create_transient_shader_binding(...)
-> 复用 View/Material/Object bindings
```

不新增Pass基类、adapter、Program-specific set或physical-set知识。`MeshDrawCommand`与`RHIGraphicsBindings`结构保持稳定。

### 9. 错误、线程与生命周期

Codegen错误在Shader构建阶段失败；schema/artifact identity错误在Program publication或binding创建阶段失败；required value/type错误在任何upload前失败；backend upload/set创建失败保留原始RHI状态。

Generated metadata只读且可跨线程共享。GT Material setter只读完整schema并投递owned value；RT owner创建RHI资源。CPU parameters临时值只需活到`create_transient_shader_binding()`返回，因为RHI upload必须同步复制bytes。logical set、slice backing page、persistent Material buffer与active resources继续由现有command list/completion规则保活。

## Risks / Trade-offs

- [Risk] ShaderCompiler生成头文件会形成新的构建顺序和增量依赖，构建目录输出若未登记还会降低VS可发现性 → 使用显式CMake outputs/depfile、`GENERATED` source、`target_sources()`与VS `source_group`，增加clean build、IDE生成工程检查、单文件schema变化和无ShaderCompiler→Runtime链接环测试。
- [Risk] `.shader`名字到C++标识符可能冲突或包含非法字符 → 定义版本化、确定性的标识符规范化规则；同一生成集合出现碰撞时直接诊断失败，不追加不稳定序号。
- [Risk] generated C++ field类型与Engine math/RHI ref类型变化会抬高生成格式version → 生成格式version进入输出与构建依赖；破坏性变化clean rebuild，不保留旧generated header兼容层。
- [Risk] Material完整schema可能强持有当前variant未使用资源 → Material owner本就持有asset/resource引用；GPU command payload仍只保活Pipeline active subset。
- [Risk] Object frame-local复用会增加SceneRenderer临时表 → 第一版只缓存可见Primitive且随frame销毁；不引入跨帧generation管理或GPU Scene。
- [Risk] 一次迁移会同时触及ShaderCompiler、RenderCore、RHI与RenderScene → 按metadata/codegen、encoder/RHI缩减、owner迁移、旧路径删除的顺序施工，但只在全部调用方迁移后合入，不保留正式双轨。

## Migration Plan

1. 为ShaderFormat定义完整schema record与generated-metadata identity，提升相关格式version；扩展ShaderCompiler codegen和确定性/碰撞/clean-build测试。
2. 生成内建View/Object与Tonemap/ImGui Pass parameter headers，在RenderCore实现metadata、encoder和`create_transient_shader_binding()`，此时暂不切换正式调用点。
3. 缩减RHI transient uniform descriptor/slice，更新Vulkan与测试fake；由RenderCore统一把ABI identity附加到logical value。
4. 迁移Tonemap、ImGui、View和Object，建立frame-local Object复用；删除手写ID/layout/hash和旧View/Object serializer入口。
5. 扩展Material完整schema runtime record，将GT setter迁移为name-based validation/内部ID投递，将RT materialization从Program active bindings切换到完整schema。
6. 迁移BasePass并以Shadow-style fixture验证Pass只组合owner-provided bindings；全仓搜索禁止RenderScene直接构造底层binding descriptor或ABI字段。
7. clean生成并部署Shader artifacts/generated headers，同步Active设计文档，完成Windows Debug全量构建、CTest和Vulkan validation Cube冒烟。

回滚必须整体回退本change的生成格式、generated headers、RenderCore入口、RHI descriptor和全部调用方；不得恢复双版本reader、compatibility wrapper或半迁移的手写metadata路径。
