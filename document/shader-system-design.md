# Toy3d Shader 系统设计

## 1. 文档目的

本文定义 Toy3d Shader 系统的长期架构、Shader 资产语法、跨图形 API 编译链、参数布局、Binding、缓存、调试、运行时加载以及与后续材质系统和 RDG 的边界，并在文档末尾给出分阶段实现步骤。

目标图形后端为 Vulkan、Direct3D 10 和 Direct3D 12。开发者只维护 HLSL 和 Toy3d Shader 资产描述，不维护 GLSL、SPIR-V、DXBC 或 DXIL 源文件。发布运行时只消费离线编译产物，Editor/Development 构建可以按配置从源码重新编译。

本文中的产品边界已经确认：

- 使用类似 Unity ShaderLab、但范围更小的 `.shader` 容器；
- `Properties` 自动生成 Material 参数布局和 HLSL 声明；
- `Pass` 可以声明跨 API 的 raster、depth/stencil、blend 等 PSO 模板状态；
- attachment、load/store、transition、资源依赖和执行顺序仍属于 renderscene/RDG；
- `Global`、`View`、`Pass`、`Material`、`Object` 是稳定的逻辑 Binding Group；
- `b/t/s/u` 是每个 Shader Program 确定性、紧凑分配的物理 slot，不是全局固定 ABI；
- ShaderCompiler 在编译前生成显式 register，编译后使用 reflection 验证三个目标的一致性；
- constant buffer 使用 Toy3d 自己定义的 HLSL ABI，不把 `std140` 或 `std430` 暴露为公共规则；
- baseline 支持 `float`，`half` 通过 capability 安全降级，且第一阶段不进入公开参数存储 ABI；
- Shader 支持预编译、源码重编译和 RenderDoc 源码级调试。

## 2. 目标与非目标

### 2.1 目标

- 一份 Shader 源码自动生成 Vulkan、D3D10、D3D12 的目标字节码；
- 自动生成、序列化和验证参数布局与 Binding Layout；
- 不在 renderscene、材质或 pass 代码中硬编码物理 slot；
- 支持多个 entry point、graphics/compute program、permutation 和 capability 裁剪；
- 支持 Debug、Development、Shipping 三种 Shader 调试与优化策略；
- 支持 include 依赖追踪、增量编译、热重载和稳定缓存键；
- 为 Material、MaterialInstance、Material Graph 和 RDG 留出长期兼容边界；
- 编译失败、不支持能力、反射不一致和运行时加载失败均可诊断。

### 2.2 第一阶段非目标

- 不实现 Unity Surface Shader 的自动光照代码生成；
- 不实现 UE 的 ShaderCompileWorker 多进程协议、完整 Derived Data Cache 或宏注册体系；
- 不实现 backend-specific `SubShader`、`Fallback` 或源码中的 Vulkan/D3D 分支；
- 不实现 bindless、ray tracing、mesh shader、VRS 和 async compute；
- 不让 Shader `Pass` 表达 RDG pass、attachment、resource transition 或调度关系；
- 不在 Shipping 运行时携带 Shader 编译器；
- 不把 glslang、ShaderConductor 或 SPIRV-Cross 引入第一阶段主编译链。

## 3. 参考引擎结论

### 3.1 FlaxEngine

FlaxEngine 使用一份 HLSL 风格 `.shader` 源码声明多个 entry point 和 permutation，并为目标 `ShaderProfile` 生成平台缓存。D3D10/11 使用 FXC 路径，Vulkan 使用独立的 HLSL 到 SPIR-V 编译路径。Toy3d 借鉴其“一个 Shader 资产对应多个程序、排列和平台产物”的组织方式，但不复制其 HLSL 宏解析器和 Vulkan stage-set 布局。

### 3.2 UE4.27

UE4.27 使用 C++ `FShaderParametersMetadata` 描述参数结构，编译前生成 `/Engine/Generated/UniformBuffers/*.ush` 虚拟 include，编译后通过 `FShaderParameterMap` 保存参数名到物理位置的映射。D3D 后端读取 reflection 的 `BindPoint`，Vulkan ShaderCompiler 保存自己的 descriptor binding 映射。

UE 并未把 `View`、`Material` 等 Uniform Buffer 简单固定为全工程统一的 `b0/b1/...`。这些名称首先是逻辑资源身份，最终物理位置属于具体 Shader 和具体平台编译产物。Toy3d 采用相同的逻辑/物理解耦，但增加编译前的确定性 slot 分配和编译后的跨目标 reflection parity 检查。

## 4. 总体架构

```text
.shader / .hlsli / Material Graph
                |
                v
        ToyShader Frontend
        - ShaderLab parser
        - Properties/Resources schema
        - Pass/entry/variant/capability
                |
                v
       Shader Layout Compiler
       - ToyShaderABI packing
       - logical binding groups
       - deterministic b/t/s/u allocation
       - generated virtual HLSL includes
                |
                v
        Shader Compile Service
        - include/dependency tracking
        - permutation expansion/culling
        - diagnostics/debug source
          |          |          |
          v          v          v
         FXC        DXC        DXC
       DXBC SM4   DXIL SM6   SPIR-V
        D3D10      D3D12      Vulkan
          |          |          |
          +----------+----------+
                     |
                     v
          Normalized Reflection
          - parity validation
          - stable hashes/layout hashes
                     |
                     v
             ToyShaderPackage
                     |
                     v
 ShaderLibrary / Material / Renderscene
                     |
                     v
        RHIShader + RHIBindingLayout
                     |
                     v
        Vulkan / D3D10 / D3D12 backend
```

职责边界：

- Shader Frontend、layout、codegen 和目标编译器只属于 `engine/tools/shader_compiler/`；ShaderPackage loader、ShaderLibrary 和参数运行时属于 RenderCore，二者都不属于 RHI；
- RHI 不读取 `.shader`、不处理 include、不展开 permutation、不理解 Material；
- RHI 只接收目标字节码、entry point、精简 reflection、稳定 content hash 和 Binding Layout；
- Material 系统操作参数 ID、逻辑分组和 Shader permutation，不操作物理 slot；
- renderscene/RDG 选择 Shader `Pass`，并补充实际 attachment format、sample count 和 render-pass scope；
- backend 只负责把公共 RHI Shader、Binding 和 PSO 翻译为原生对象。

### 4.1 工程目录与模块边界

Shader 源资产、离线工具链和 Shipping runtime 必须分开，推荐目录结构如下：

```text
engine/
├── shader/                              # 第一方 Shader 源资产与 Cook 入口
│   ├── CMakeLists.txt
│   ├── builtin/                         # 引擎内置 .shader
│   │   ├── surface/
│   │   │   └── unlit.shader
│   │   └── post_process/
│   └── include/                         # 引擎共享 HLSL include
│       ├── ToyCommon.hlsli
│       ├── ToyView.hlsli
│       └── ToySurface.hlsli
├── tools/
│   ├── CMakeLists.txt
│   └── shader_compiler/                 # 只在开发机/Cook 构建的工具
│       ├── CMakeLists.txt
│       ├── frontend/                    # lexer、parser、AST、diagnostics
│       ├── layout/                      # ToyShaderABI、Binding allocator
│       ├── codegen/                     # generated HLSL 与 target decoration
│       ├── compiler/                    # DXC、FXC adapter 与 compile service
│       ├── reflection/                  # SPIR-V、DXBC、DXIL reflection
│       ├── package/                     # ShaderPackage writer
│       ├── cache/                       # include graph 与 derived-data cache
│       ├── tests/
│       │   └── data/                    # parser/compiler 正反例 .shader
│       └── main.cpp                     # Toy3dShaderCompiler CLI
├── runtime/
│   └── rendercore/
│       └── shader/                      # Shipping 可用、无 compiler 依赖
│           ├── shader_types.h
│           ├── shader_parameter.h/.cpp
│           ├── shader_package_format.h
│           ├── shader_package.h/.cpp    # 只读、校验、反序列化
│           └── shader_library.h/.cpp    # target 选择、缓存、热替换
└── thirdparty/                          # 锁定版本的 DXC 等上游依赖
```

目录职责固定如下：

| 内容 | 位置 | 是否进入 Shipping runtime |
|---|---|---|
| 引擎内置 `.shader` | `engine/shader/builtin/` | 源码默认不进入；只进入 Cook package |
| 公共 `.hlsli` | `engine/shader/include/` | 同上；Full debug 模式可作为 debug artifact 携带 |
| parser、AST、layout、codegen | `engine/tools/shader_compiler/` | 否 |
| DXC/FXC、reflection、package writer | `engine/tools/shader_compiler/` | 否 |
| package format、loader、ShaderLibrary | `engine/runtime/rendercore/shader/` | 是 |
| RHI Shader/Binding 公共接口 | `engine/runtime/drivers/rhi/` | 是 |
| Vulkan/D3D 原生 Shader 对象 | 对应 `engine/runtime/drivers/<backend>/` | 是，仅加入所选 backend |
| 编译器测试 Shader | `engine/tools/shader_compiler/tests/data/` | 否 |

`engine/runtime/rendercore/shader/` 不包含 `dxcapi.h`、`d3dcompiler.h`、SPIRV-Reflect 或任何后端原生类型。`shader_package_format.h` 只定义版本化的定宽磁盘格式和共享枚举；writer 位于工具侧，runtime 只有 reader，避免把编译器依赖链接进 `Toy3dRuntime`。

当前 `engine/shader/test_pass.vert` 和 `test_pass.frag` 是 Vulkan 闭环使用的临时 GLSL。迁移阶段保留它们；阶段 3 完成后由 `engine/shader/builtin/.../*.shader` 及生成的 package 替换，随后删除旧 GLSL 和 `glslangValidator` 自定义命令。禁止继续在 `engine/shader/` 根目录新增业务 Shader。

### 4.2 `.shader` 资产位置与虚拟路径

第一方引擎 Shader 源文件统一提交到：

```text
engine/shader/builtin/<domain>/<name>.shader
```

共享 include 统一提交到：

```text
engine/shader/include/<name>.hlsli
```

工具链将它们映射为稳定虚拟路径：

```text
/Engine/Shaders/<domain>/<name>.shader
/Engine/ShaderIncludes/<name>.hlsli
```

源码中的 include 必须使用虚拟路径，例如 `#include "/Engine/ShaderIncludes/ToySurface.hlsli"`，不得写本机绝对路径或相对构建目录。这样 cache key、diagnostics、RenderDoc 源码映射和不同平台 Cook 才保持一致。

未来引入独立游戏工程后，项目 Shader 放在 `<project>/asset/shaders/`，映射为 `/Project/Shaders/`；项目目录不能反向写入 `engine/shader/`。在项目资产系统落地前，Toy3d 仓库内只启用 `/Engine/...` 根。

以下位置禁止保存手写 Shader 源码：

- `engine/runtime/generated/`：仅兼容现有生成头文件，Shader 生成物不得写入源码树；
- `engine/asset/`：保留图标、字体等普通运行资源，不与 Shader 源码混放；
- `bin/` 和 `build/`：只保存可再生成的 package、cache、debug artifact 或中间文件。

### 4.3 构建目录、Cook 产物与运行时部署

所有生成物写入构建目录或 `saved`，不写回 `engine/shader/`：

```text
<build>/generated/shader/<compile-key>/
├── ToyBindings.generated.hlsli          # target-specific 虚拟 include 的落盘副本
├── preprocessed.hlsl                    # 仅 Symbols/Full 策略保留
├── shader.spv|shader.dxbc|shader.dxil   # 中间 target binary
└── reflection.json                      # 可选诊断副本，不是运行时权威格式

<build>/generated/shader/packages/<platform>/
└── builtin.toyshaderpackage             # Cook 输出

bin/shader/<platform>/
└── builtin.toyshaderpackage             # POST_BUILD 部署，runtime 只读

bin/saved/shader_cache/                   # Editor/Development 编译缓存
bin/saved/shader_debug/<compile-key>/     # 可选源码、PDB/debug artifact
```

`ShaderPackage` 是运行时唯一权威输入。Loose `.shader`、generated HLSL、reflection JSON 和单独的 `.spv/.dxbc/.dxil` 都不能成为 Shipping 加载路径。

### 4.4 CMake target 与依赖方向

建议建立以下 target：

```text
Toy3dShaderCompilerCore   STATIC   parser/layout/codegen/compiler/reflection/writer
Toy3dShaderCompiler       EXECUTABLE，链接 Toy3dShaderCompilerCore
Toy3dShaders              CUSTOM，调用 Toy3dShaderCompiler Cook 内置资产
Toy3dRuntime              仅包含 rendercore/shader reader/library，并依赖 Toy3dShaders
Toy3dEditor               链接 Toy3dRuntime；需要源码编译时启动 compiler 进程
```

依赖方向为：

```text
Toy3dShaderCompiler -> ShaderPackage format contract <- Toy3dRuntime
Toy3dShaders -> Toy3dShaderCompiler
Toy3dRuntime -> Toy3dShaders
Toy3dEditor -> Toy3dRuntime
```

Editor 第一阶段通过子进程调用 `Toy3dShaderCompiler`，不直接链接 `Toy3dShaderCompilerCore`。这样 Shipping runtime 不携带 compiler DLL，Editor 崩溃边界更清晰，后续也能自然演进为类似 UE ShaderCompileWorker 的独立进程池。

`engine/CMakeLists.txt` 的长期子目录顺序应为 `thirdparty`、`tools`、`shader`、`runtime`、`editor`。`engine/shader/CMakeLists.txt` 只登记 Shader 源依赖和 Cook custom command；不得包含 parser/compiler C++ 实现。所有 custom command 使用 `$<TARGET_FILE:Toy3dShaderCompiler>`、`${CMAKE_COMMAND} -E` 和构建目录输出，并把 `.shader`、递归 include、compiler target 及版本清单列入依赖。

## 5. Shader 资产格式

### 5.1 文件类型

- `.shader`：Shader 资产容器，包含 Properties、Resources、Variants、Pass 和 HLSL block；
- `.hlsli`：共享 HLSL include；
- `.hlsl`：可选的外部纯 HLSL 实现文件；
- `.toyshaderpackage`：Cook 后运行时产物；v1 使用该扩展名，binary header 另有独立的 format version。

`.shader` 使用专用 tokenizer/parser。`HLSLPROGRAM`、`HLSLINCLUDE` 到对应结束标记之间的内容作为不透明文本处理，Frontend 不解析完整 HLSL 语法，只提取 Toy3d 自己定义的 `#pragma`。

### 5.2 最小完整示例

```hlsl
Shader "Toy3d/Surface/Unlit"
{
    Version 1

    Properties
    {
        base_color
        (
            "Base Color",
            Color
        ) = (1.0, 1.0, 1.0, 1.0)

        base_color_texture
        (
            "Base Color Texture",
            Texture2D
        ) = "white"

        material_sampler
        (
            "Material Sampler",
            Sampler
        ) = LinearWrap

        roughness
        (
            "Roughness",
            Range(0.0, 1.0)
        ) = 0.5
    }

    Variants
    {
        USE_VERTEX_COLOR : bool = false
    }

    Pass "Forward"
    {
        Requires GraphicsBaseline

        Cull Back
        DepthTest LessEqual
        DepthWrite On
        Blend Off

        HLSLPROGRAM

        #pragma vertex vs_main
        #pragma pixel ps_main

        #include "/Engine/ShaderIncludes/ToySurface.hlsli"

        struct vertex_input
        {
            float3 position : POSITION0;
            float3 normal : NORMAL0;
            float2 texcoord : TEXCOORD0;

        #if USE_VERTEX_COLOR
            float4 color : COLOR0;
        #endif
        };

        struct vertex_output
        {
            float4 position : SV_Position;
            float3 world_position : TEXCOORD0;
            float3 world_normal : TEXCOORD1;
            float2 texcoord : TEXCOORD2;

        #if USE_VERTEX_COLOR
            float4 color : COLOR0;
        #endif
        };

        vertex_output vs_main(vertex_input input)
        {
            vertex_output output;
            const float4 world_position =
                mul(toy_object_to_world, float4(input.position, 1.0));

            output.position = toy_to_clip_space(
                mul(toy_view_projection, world_position));
            output.world_position = world_position.xyz;
            output.world_normal = normalize(
                mul((float3x3)toy_object_to_world, input.normal));
            output.texcoord = input.texcoord;

        #if USE_VERTEX_COLOR
            output.color = input.color;
        #endif

            return output;
        }

        float4 ps_main(vertex_output input) : SV_Target0
        {
            const float4 texture_color = base_color_texture.Sample(
                material_sampler,
                input.texcoord);

            toy_half3 final_color =
                (toy_half3)(texture_color.rgb * base_color.rgb);

        #if USE_VERTEX_COLOR
            final_color *= (toy_half3)input.color.rgb;
        #endif

            return float4(
                (float3)final_color,
                texture_color.a * base_color.a);
        }

        ENDHLSL
    }
}
```

### 5.3 `Properties`

`Properties` 同时描述：

- Material Editor/Inspector 中的显示名称、类型、范围和默认值；
- MaterialInstance 的稳定参数身份；
- Material constant buffer 的值成员；
- Material texture、buffer 和 sampler 资源；
- ShaderCompiler 生成 HLSL 声明所需的逻辑 schema。

第一阶段支持：

```text
Float
Float2
Float3
Float4
Color
Matrix4x4
Range(min, max)
Texture2D
TextureCube
Sampler
```

`Float/Vector/Color/Matrix/Range` 进入 Material constant buffer。Texture 和 sampler 进入 `Material` Binding Group，但不占用 constant buffer 字节。后续增加 buffer、texture array 和 storage resource 时必须 capability-gated。

属性使用稳定 `ShaderParameterId`。ID 的确切 hash 算法在实现第一阶段锁定并版本化；重命名默认视为破坏参数身份，后续可以增加显式 GUID 或 alias 迁移机制。

### 5.4 `Resources`

非材质资源通过逻辑分组声明：

```hlsl
Resources
{
    Pass
    {
        source_texture : Texture2D
        source_sampler : Sampler = LinearClamp
    }

    Object
    {
        skinning_matrices : StructuredBuffer<float4x4>
    }
}
```

第一阶段优先支持 `Pass` 资源。`Global`、`View`、`Object` 的公共常量数据由引擎 `.hlsli` 和对应 schema 提供，避免每个 Shader 重复声明。高级 Shader 可以声明额外资源，但所有资源仍必须进入确定性布局编译和 reflection 验证。

### 5.5 `Variants`

第一阶段提供有类型的 permutation domain：

```hlsl
Variants
{
    USE_NORMAL_MAP : bool = false

    LIGHTING_MODEL : enum
    {
        Unlit,
        DefaultLit,
        ClearCoat
    } = DefaultLit
}
```

只有影响生成代码、entry 可用性或资源布局的条件进入 permutation。材质数值、纹理对象和可以低成本由 uniform 控制的普通分支不得制造 permutation。

后续可以增加类似 Unity 的两种策略：

- `ShaderFeature`：只 Cook 实际被 Material 使用的组合；
- `MultiCompile`：Cook 引擎要求的全部组合。

第一阶段统一按普通 `Variants` 处理，但序列化格式和 cache key 需要允许后续加入策略字段。

### 5.6 `Pass`

Shader `Pass` 是一个 Shader Program 与跨 API PSO 模板，不是 RDG pass。一个 `Pass` 至少描述：

- 名称；
- vertex/pixel 或 compute entry；
- capability requirement；
- permutation domain；
- topology、rasterization、depth/stencil、blend 和 color write mask 等可移植状态。

允许进入 Shader `Pass` 的状态包括：

```text
PrimitiveTopology
Cull
FrontFace
Fill
DepthTest
DepthWrite
Stencil
Blend
ColorWrite
```

不允许进入 Shader `Pass`：

```text
attachment resource
attachment format
sample count
load/store
clear value
resolve target
resource transition
RDG dependency
queue selection
执行顺序
```

最终 `RHIGraphicsPipelineDesc` 由 Shader Pass template 与 renderscene/RDG 提供的 attachment compatibility 合并而成。

### 5.7 HLSL block 与 pragma

第一阶段支持：

```hlsl
#pragma vertex entry_name
#pragma pixel entry_name
#pragma compute entry_name
```

graphics pass 必须声明 vertex 和 pixel；compute pass 只声明 compute。重复 stage、缺失 entry、未知 pragma 或 entry reflection 与声明 stage 不一致均为编译错误。

`HLSLINCLUDE` 定义 Shader 内多个 Pass 共享的 HLSL，`HLSLPROGRAM` 定义当前 Pass 的 HLSL。Frontend 合并公共 block、当前 Pass block、generated include 和外部 include，并使用 `#line` 保持诊断映射。

## 6. Properties 与 Resources 自动生成

### 6.1 生成流程

以一个使用 `View + Material + Object` 的 graphics pass 为例：

1. Frontend 解析 `Properties`、`Resources`、Pass 和 entry；
2. 参数布局器按 ToyShaderABI 打包 Material 数值属性；
3. Binding Layout Compiler 收集当前 Program 需要的所有逻辑 Binding；
4. 分别为 `b/t/s/u` 寄存器类别紧凑分配 slot；
5. 为每个 target 生成带显式 `register()`、必要 `packoffset()` 和目标 Binding decoration 的虚拟 `.hlsli`；
6. 将虚拟 include 注入最终 HLSL；
7. 对每个 target/permutation 编译；
8. 从最终字节码提取 reflection；
9. 验证 reflection 与预期布局及其他目标一致；
10. 写入 ShaderPackage。

### 6.2 示例布局

输入属性：

```text
base_color : Color
roughness  : Float
base_color_texture : Texture2D
material_sampler   : Sampler
```

逻辑 Material constant buffer：

```text
base_color offset 0, size 16
roughness  offset 16, size 4
total size 32
```

如果当前 Program 只使用 `View + Material + Object`，可以生成：

```text
b0 = View
b1 = Material
b2 = Object
t0 = base_color_texture
s0 = material_sampler
```

另一个只使用 Material 的 Program 可以生成：

```text
b0 = Material
t0 = source_texture
s0 = source_sampler
```

因此逻辑 group 稳定，`b/t/s/u` slot 只需在当前 Program 内确定，不需要跨 Program 固定。

### 6.3 生成的虚拟 HLSL

生成文件写入构建或缓存目录，不写入源码目录：

```text
build/generated/shader/<shader-hash>/ToyBindings.generated.hlsli
```

示例内容：

```hlsl
#ifndef TOY_BINDINGS_GENERATED
#define TOY_BINDINGS_GENERATED

cbuffer toy_view_data : register(b0)
{
    column_major float4x4 toy_view_projection;
    float3 toy_camera_position;
    float toy_view_padding;
};

cbuffer toy_material_data : register(b1)
{
    float4 base_color : packoffset(c0);
    float roughness : packoffset(c1.x);
};

cbuffer toy_object_data : register(b2)
{
    column_major float4x4 toy_object_to_world;
};

Texture2D<float4> base_color_texture : register(t0);
SamplerState material_sampler : register(s0);

#endif
```

实际生成器必须依据目标编译器语义生成合法 HLSL。D3D10/D3D12 版本直接使用上述 `b/t/s/u` register；Vulkan 版本还必须显式生成与 Vulkan backend ABI 一致的 descriptor set/binding decoration。Debug 输出保留各 target 的最终生成文件，便于 RenderDoc 和编译诊断展示。

例如 `base_color_texture` 的归一化位置是 `Material + t0`。D3D target 生成：

```hlsl
Texture2D<float4> base_color_texture : register(t0);
```

若 `Material` 的稳定 `group_index` 为 3，Vulkan target 则生成：

```hlsl
[[vk::binding(256, 3)]]
Texture2D<float4> base_color_texture : register(t0);
```

这里的 `register(t0)` 保留公共 register 身份，`vk::binding` 只控制 SPIR-V 的原生 decoration。

## 7. Binding 模型与 slot 分配

### 7.1 逻辑 Binding Group

```text
Global   跨 view、pass、material 和 object 的低频全局数据
View     camera/view 数据
Pass     当前渲染 pass 数据与输入资源
Material Material/MaterialInstance 参数与资源
Object   当前 draw/object 数据
```

Group 只表达更新频率、所有权和运行时数据来源，不创建独立寄存器命名空间。公共 RHI 不暴露 Vulkan descriptor set、D3D12 register space 或 root parameter。

### 7.2 物理 slot 规则

- 每个 Shader Program、permutation、stage 组合独立编译布局；
- `b/t/s/u` 分别拥有独立 slot 序列；
- 分配顺序按规范化后的逻辑 group、资源类型、稳定参数 ID 和数组索引确定；
- 只为当前 Program 声明需要的 Binding，避免浪费 D3D10 slot；
- 同一 stage、同一寄存器类别的范围禁止重叠，即使逻辑 group 不同；
- Shader stage 可以复用相同数字 slot，因为 D3D10/D3D12 stage slot 本身独立，但第一阶段布局合并器可以优先选择跨 graphics stage 相同编号以简化实现；
- array 占用连续 slot 范围；
- 分配前检查三个后端共同 limits；
- 归一化 Binding 位置是 `group + register class + slot`，其中 register class 为 `b/t/s/u`；不能只用一个无类型数字描述 Binding；
- 归一化 layout hash 覆盖 `group + register class + slot` 和布局策略版本，但不混入 Vulkan native binding；目标 Binding 映射版本另行进入对应 target 的 compile cache key 和 package metadata。

第一阶段不允许普通业务 HLSL 绕过 Frontend 随意声明未登记资源。高级 escape hatch 必须显式标记、提供逻辑 group，并接受同样的 reflection 与冲突验证。

### 7.3 D3D register 与 Vulkan descriptor binding 映射

D3D 和 Vulkan 的原生 Binding 命名空间不同：

- D3D10/D3D12 的 `b`、`t`、`s`、`u` 是互相独立的 register class，因此 `b0`、`t0`、`s0` 和 `u0` 可以同时存在；
- Vulkan 的同一个 descriptor set 内只有一套 `binding` 数字命名空间，因此上述四个位置不能全部直接映射成 `binding = 0`；
- Toy3d normalized reflection 和公共 `RHIBindingLayout` 继续保存 `group + resource type/register class + slot`，不保存 `VkDescriptorSet`、native binding 或 D3D12 root parameter。

当前 Vulkan backend 的目标映射为：

| 归一化位置 | D3D10/D3D12 | Vulkan 原生位置 |
|---|---|---|
| `group, bN` | `register(bN)` | `set = group_index, binding = 0 + N` |
| `group, tN` | `register(tN)` | `set = group_index, binding = 256 + N` |
| `group, sN` | `register(sN)` | `set = group_index, binding = 512 + N` |
| `group, uN` | `register(uN)` | `set = group_index, binding = 768 + N` |

`group -> descriptor set` 和 `0/256/512/768` 只属于 Vulkan backend ABI。它们不是公共 Shader 语法，也不意味着 `RHIBindingGroup` 在所有后端都是 descriptor set。ShaderCompiler 的 Vulkan target generator 必须使用 `[[vk::binding(native_binding, group_index)]]` 等明确 decoration，或者使用锁定版本且等价的 DXC `-fvk-*-shift` 配置；第一阶段优先生成显式 decoration，避免 compiler flag 的隐式映射难以审查。

该映射必须集中定义并版本化，ShaderCompiler 与 Vulkan backend 使用同一份规则或生成数据，禁止复制两套常量。`VulkanBindingMappingVersion`、group 顺序和 class base 都进入 Vulkan target cache key 与 ShaderPackage target metadata。加载时检查 package mapping version 与 backend 支持版本；不一致时返回可诊断错误，不能继续创建 pipeline。

Vulkan SPIR-V reflection 先读取原生 `set/binding`，再按 package 中的目标映射反归一化为公共 `group + register class + slot`，之后才与 DXBC/DXIL 做 parity。所谓“三目标 slot 一致”始终指归一化后的公共位置一致，不要求 Vulkan native binding 数字等于 D3D `BindPoint`。

### 7.4 上层绑定

上层通过稳定参数 ID 写入：

```cpp
material_parameters.set_texture(
    shader_parameter_id("base_color_texture"),
    base_color_texture);

material_parameters.set_float(
    shader_parameter_id("roughness"),
    roughness);
```

禁止 renderscene 和 Material 代码直接使用：

```cpp
// 禁止：上层依赖物理 slot。
binding_set->set_texture(0, base_color_texture);
```

ShaderPackage 中的参数 schema 将参数 ID 映射到逻辑 group、constant-buffer offset 或资源 Binding。RenderCore 据此构造 `RHIBindingSetDesc`，RHI 和 backend 才使用最终物理 slot。

## 8. ToyShaderABI

### 8.1 Constant buffer ABI

公共规则称为 `ToyShaderABI`，不称为 `std140` 或 `std430`。第一版以 HLSL cbuffer packing 为基线，并限制为 Vulkan、D3D10、D3D12 可稳定实现的交集。

规则：

- 基本 packing unit 为 16-byte register；
- `float` 对齐 4 bytes；
- `float2` 对齐 8 bytes；
- `float3` 占 12 bytes，但不得跨 16-byte register；
- `float4` 对齐 16 bytes；
- 数组元素 stride 向上对齐到 16 bytes；
- struct 起始和总大小对齐到 16 bytes；
- matrix 显式使用 `column_major`，禁止依赖编译器默认值；
- 生成器在需要时写出 `packoffset()`；
- cbuffer 禁止 `bool`、平台相关整数宽度和未定义布局的嵌套类型；
- 第一阶段 cbuffer 禁止 `half/min16float` 存储；
- CPU 侧不依赖普通 C++ struct 的自然 padding，使用生成 schema/parameter writer 写入；
- 三目标 reflection 的 buffer size、member offset、array stride、matrix stride 必须一致。

### 8.2 Structured/Storage buffer ABI

Storage buffer 不复用 cbuffer packing。后续引入时必须：

- 显式记录 `structure_stride`；
- 使用明确位宽类型；
- 禁止隐式依赖 C++ padding；
- 为 `float3`、数组、matrix 和嵌套 struct 定义明确规则；
- 对 DXIL/DXBC/SPIR-V reflection 的成员 offset 和 stride 做 parity 检查；
- D3D10 不支持的 UAV/storage 能力通过 capability 返回 `Unsupported`。

## 9. `float`、`half` 与 capability

`float` 是所有后端的 baseline。引擎公共 include 提供：

```hlsl
#if TOY_NATIVE_FLOAT16
    #define toy_half  half
    #define toy_half2 half2
    #define toy_half3 half3
    #define toy_half4 half4
#else
    #define toy_half  float
    #define toy_half2 float2
    #define toy_half3 float3
    #define toy_half4 float4
#endif
```

规则：

- `toy_half*` 第一阶段只用于局部变量和临时算术；
- Material/Global/View/Pass/Object 的公开参数存储统一为 FP32；
- FP16 texture format 与 HLSL 运算精度是两个独立能力；
- D3D10 baseline 将 `toy_half*` 安全降级为 `float*`；
- D3D12 native 16-bit 由 Shader Model 和 device capability 决定；
- Vulkan native 16-bit 由 `shaderFloat16` 和相关 storage capability 决定；
- 同一个 Material 参数 layout 不得因为目标支持 native FP16 而改变；
- 后续若支持 FP16 storage，必须使用独立 feature tier 和 layout hash。

建议 capability 拆分为：

```cpp
struct RHIShaderCapabilities
{
    bool native_float16_arithmetic = false;
    bool native_float16_storage_buffer = false;
    bool native_float16_uniform_buffer = false;
};
```

## 10. 编译目标与工具链

| 目标后端 | 编译器 | Shader Model/目标 | 产物 |
|---|---|---|---|
| Vulkan | DXC | HLSL + `-spirv` | SPIR-V |
| D3D10 | FXC/`D3DCompile` | SM4 | DXBC |
| D3D12 | DXC | SM6 | DXIL |

D3D10 需要 FXC/Windows SDK，DXC 不能替代 FXC 生成 SM4 DXBC。完整三后端 Cook 在 Windows 构建机或 CI 执行；macOS Editor 可以编译 Vulkan 目标，但不能把本机缺少 FXC 伪装成完整平台 Cook 成功。

编译器接口建议：

```cpp
enum class ShaderTarget
{
    VulkanSpirV,
    D3D10Dxbc,
    D3D12Dxil
};

struct ShaderCompileRequest
{
    std::string virtual_source_path;
    std::string entry_point;
    RHIShaderStage stage;
    ShaderTarget target;
    std::vector<ShaderDefine> defines;
    ShaderCompileOptions options;
};

struct ShaderCompileOutput
{
    std::vector<std::uint8_t> bytecode;
    ShaderReflection reflection;
    ShaderDependencyList dependencies;
    ShaderDiagnosticList diagnostics;
    ShaderContentHash content_hash;
};
```

具体 compiler release、flags 和目标版本必须进入 cache key。升级编译器时不得复用旧缓存。

## 11. Reflection 与一致性验证

Reflection 必须来自最终目标字节码，而不是只解析 HLSL 源码。

统一 reflection 至少包含：

```cpp
struct ShaderResourceBinding
{
    ShaderParameterId parameter_id;
    std::string name;
    RHIBindingGroup group;
    RHIResourceBindingType type;
    std::uint32_t slot;
    std::uint32_t array_count;
    RHIShaderStageFlags stages;
};

struct ShaderConstantMember
{
    ShaderParameterId parameter_id;
    std::string name;
    ShaderValueType type;
    std::uint32_t offset;
    std::uint32_t size;
    std::uint32_t array_stride;
    std::uint32_t matrix_stride;
};
```

还需要记录 constant buffer size、vertex input semantic、compute thread-group size、stage input/output 和 capability requirement。

Cook 必须拒绝：

- 预期 Binding 在最终字节码中类型、slot 或数组长度不符；
- DXBC、DXIL、SPIR-V 的 normalized constant layout 不一致；
- vertex output 与 pixel input 不兼容；
- permutation 在某个必需 target 编译失败；
- 目标使用未声明或不支持的 capability；
- 同一 stage 的 Binding 范围冲突；
- Vulkan 原生 `set/binding` 无法按声明的映射版本反归一化，或与预期公共位置不一致；
- Material schema 与生成 HLSL reflection 不一致。

编译器可能消除未使用资源。布局验证器应区分“schema 中声明但该 stage 未使用”与“使用了但 reflection 缺失”；最终 stage visibility 以 reflection 为准，Program layout 以各 stage 的使用并集为准。

## 12. ShaderPackage

Cook 产物至少包含：

```text
package format version
ToyShaderABI version
target binding mapping version
shader asset identity
program/pass table
permutation table
capability requirements
portable PSO templates
parameter schemas/default values
logical binding groups
normalized reflection
binding layout hashes
target bytecode records
source/include dependency hashes
compiler identity/version/flags
content hashes
optional debug artifacts
```

每个目标字节码记录至少包含：

```text
target binary format
stage
entry point
permutation key
bytecode
bytecode content hash
target reflection hash
```

序列化不能直接 dump 编译器或平台结构体；所有字段使用版本化、明确宽度和可验证边界。加载时检查 package version、ABI version、target、hash、字节码非空和 reflection 完整性。

## 13. 缓存与增量编译

Shader 编译缓存 key 至少覆盖：

- `.shader` 规范化语法树或内容；
- 当前 HLSL block；
- 所有递归 include 内容；
- generated binding include；
- target binding mapping version、group index 和 register-class base；
- entry point 和 stage；
- canonical permutation；
- target、Shader Model 和 capability tier；
- compiler 名称、版本和 flags；
- Shader Frontend、layout allocator、package 和 ToyShaderABI 版本；
- debug/optimization policy。

建议分两层：

- `ShaderDerivedDataCache`：编译任务 key 到单目标输出，用于 Editor 增量编译；
- `ShaderLibrary`：Cook 后面向运行时的平台字节码库，按 content hash 去重。

include dependency graph 记录真实文件与虚拟 include。文件变更只使受影响 Shader/permutation 失效。并发 cache miss 使用 single-flight，避免同一任务重复编译。

## 14. 调试、RenderDoc 与热重载

### 14.1 加载和调试模式

```cpp
enum class ShaderLoadMode
{
    Precompiled,
    RuntimeCompile,
    Auto
};

enum class ShaderDebugMode
{
    None,
    Symbols,
    Full
};
```

建议：

| 构建模式 | LoadMode | DebugMode |
|---|---|---|
| Debug Editor | `Auto` | `Full` |
| Development | `Precompiled` 或 `Auto` | `Symbols` |
| Shipping | `Precompiled` | `None` |

GPU 始终执行 binary。“源码模式”表示 Editor/Development 可以从 HLSL 重新生成 binary；“源码调试”表示 binary 或外部 symbol 中保留源码、行号和调试映射。

Debug artifact 保存：

- 原始 `.shader`；
- 提取后的 HLSL；
- generated `.hlsli`；
- 预处理后的最终 HLSL；
- `#line` 映射；
- DXBC/DXIL/SPIR-V debug symbols；
- binary content hash。

第一阶段优先使用 embedded debug info；后续可将 `.toyshadersymbols` 与运行 binary 分离，并按 content hash 查找。

### 14.2 热重载

- 编译在 Shader compile worker thread/pool 执行，不阻塞 Render Thread；
- 编译失败时保留旧 Shader/PSO，输出完整诊断；
- 编译成功后在安全点原子替换 Shader Program；
- 新旧 layout hash 相同可以保留兼容参数数据；
- layout hash 改变时重建 Binding Layout、Binding Set 和相关 PSO；
- 旧 RHI Shader、Binding 和 PSO 由 command list 强引用及 queue completion value 延迟释放；
- Shipping 不启用文件监听和运行时编译。

## 15. 与 Material 系统的边界

Material Graph 不生成 SPIR-V、DXBC 或 DXIL，只生成 Material HLSL 函数、Properties schema 和 static permutation。

```text
Material Graph
    |
    +-- parameter schema/default values
    +-- static switches/permutation
    +-- generated material HLSL
                    |
                    v
          Material Template .shader/.hlsli
                    |
                    v
             ShaderCompiler
```

约束：

- Material 持有 Shader 资产、permutation key 和参数默认值；
- MaterialInstance 只覆盖动态参数，不因普通数值变化重新编译；
- static switch 才产生新的 permutation；
- Material 参数通过稳定 ID 写入，不使用物理 slot；
- Material Shader 描述程序和逻辑资源布局；
- PSO 由 Shader Pass template 与实际 render target compatibility 共同创建；
- Material 不拥有 Vulkan descriptor set 或 D3D12 root signature；
- RDG pass 只请求某个 Shader Pass/permutation，不参与 Shader 源码编译。

## 16. 与 RHI 的接口

当前 `RHIShaderDesc` 已具备 stage、bytecode、entry point、reflection 和 content hash 的基础方向。后续建议：

```cpp
enum class RHIShaderBinaryFormat
{
    SpirV,
    DxBc,
    DxIl
};
```

用 `RHIShaderBinaryFormat` 替代自由字符串 target。RHI reflection 只保留创建设备对象、验证 Binding Layout 和 pipeline 所需的精简信息；完整参数 member、默认值、Material schema 和 source debug metadata 留在 RenderCore/ShaderPackage。

`RHIDevice::create_shader()` 最终使用 NVI frontend，统一执行：

- binary format 与 backend/capability 匹配；
- stage、entry 和字节码合法性；
- content hash 非零且稳定；
- reflection 与 Binding Layout 一致；
- device owner identity；
- Shader object cache 去重；
- backend 创建失败返回可诊断错误。

`RHIBindingLayout` 由 Shader Program 各 stage reflection 合并生成，renderscene 不手写 layout。Shader 对象 hash、Binding Layout hash、PSO hash 和 Material 参数值 hash 必须分离。

## 17. 三后端映射

| 公共语义 | Vulkan | D3D10 | D3D12 |
|---|---|---|---|
| Shader binary | SPIR-V module | DXBC SM4 | DXIL SM6 |
| Constant buffer | uniform buffer descriptor | constant buffer stage slot | CBV/root binding |
| Texture/SRV | sampled image/buffer descriptor | SRV stage slot | SRV descriptor/root binding |
| Sampler | sampler descriptor | sampler stage slot | sampler descriptor |
| UAV/storage | capability-gated descriptor | baseline `Unsupported` | UAV descriptor |
| Binding Layout | backend pipeline/descriptor layout | logical stage-slot validation | root signature/descriptor layout |
| 归一化 Binding 位置 | normalized `group + class + slot` | reflected `BindPoint` + register class | reflected register + register class |
| 原生 Binding | `set = group_index`，`binding = class_base + slot` | `b/t/s/u` stage slot | `b/t/s/u` register，root mapping 由后端生成 |
| Debug info | SPIR-V debug/source mapping | DXBC debug info | DXIL debug info/PDB or embedded |

Vulkan backend 当前把每个 group 映射为一个 descriptor set，并用 class base 避免 `b/t/s/u` 在单一 Vulkan binding 命名空间内冲突；这仍是后端实现 ABI，不是公共 group 的定义。D3D12 backend 可以生成 root signature，但 root parameter 不进入 ShaderPackage 公共 schema。D3D10 对不支持的 compute/UAV/storage permutation 明确返回 `Unsupported`。

## 18. 错误模型与验证

错误至少区分：

```text
ParseError
InvalidShaderAsset
InvalidPropertyLayout
BindingConflict
UnsupportedCapability
CompilerUnavailable
CompileFailed
ReflectionFailed
ReflectionMismatch
PackageVersionMismatch
PackageCorrupt
BackendRejectedBinary
HotReloadIncompatible
```

诊断包含 Shader 虚拟路径、Pass、stage、entry、permutation、target、include stack、源码行列、compiler 原始消息和 Toy3d 上下文。Release/Shipping 不能用 assert 代替可检查错误路径。

## 19. 外部依赖与下载来源

所有依赖锁定具体版本、平台包和 SHA-256；CMake 不依赖浮动 `latest`。第三方源码或 binary 放入 `engine/thirdparty/` 或由明确的依赖安装步骤提供，除依赖升级外不修改上游代码。

### 19.1 DirectX Shader Compiler

用途：D3D12 DXIL、Vulkan SPIR-V、DXIL reflection 和 debug info。

- Releases: <https://github.com/microsoft/DirectXShaderCompiler/releases>
- Command Guide: <https://github.com/microsoft/DirectXShaderCompiler/blob/main/docs/CommandGuide.md>
- SPIR-V: <https://github.com/microsoft/DirectXShaderCompiler/blob/main/docs/SPIR-V.rst>
- 16-bit types: <https://github.com/microsoft/DirectXShaderCompiler/wiki/16-Bit-Scalar-Types>

Windows 通常需要与锁定 release 匹配的 `dxcompiler.dll`，并按该 release 说明处理 `dxil.dll`；macOS/Linux 使用对应动态库。具体文件清单在选定版本后记录到依赖清单。

### 19.2 Windows SDK / FXC

用途：D3D10 SM4 DXBC、`D3DCompile`、`D3DReflect` 和 `D3DStripShader`。

- Windows SDK: <https://learn.microsoft.com/en-us/windows/apps/windows-sdk/>
- Windows SDK downloads: <https://developer.microsoft.com/en-us/windows/downloads/windows-sdk/>
- Windows SDK archive: <https://developer.microsoft.com/en-us/windows/downloads/sdk-archive/>
- `D3DCompile`: <https://learn.microsoft.com/en-us/windows/win32/api/d3dcompiler/nf-d3dcompiler-d3dcompile>
- HLSL packing: <https://learn.microsoft.com/en-us/windows/win32/direct3dhlsl/dx-graphics-hlsl-packing-rules>

FXC 不从非官方独立 binary 包下载，直接使用 Windows SDK。

### 19.3 SPIRV-Reflect

用途：从最终 SPIR-V 提取统一 reflection。

- <https://github.com/KhronosGroup/SPIRV-Reflect>

建议静态链接到 ShaderCompiler 工具，不进入 Shipping runtime。

### 19.4 Vulkan SDK / SPIR-V Tools

用途：`spirv-val`、可选 `spirv-opt`、Vulkan headers/loader 和 validation layers。

- Vulkan SDK: <https://vulkan.lunarg.com/sdk/home>
- SPIRV-Tools: <https://github.com/KhronosGroup/SPIRV-Tools>

第一阶段 Cook 后至少运行 `spirv-val`；`spirv-opt` 可后置。

### 19.5 RenderDoc

- Releases: <https://github.com/baldurk/renderdoc/releases>
- Documentation: <https://renderdoc.org/docs/index.html>
- Shader debugging: <https://renderdoc.org/docs/how/how_debug_shader.html>

## 20. 待实现时锁定的技术细节

以下细节不改变本文架构，但必须在对应阶段实现前锁定并版本化：

- `.shader` grammar 的精确 EBNF 和错误恢复策略；
- `ShaderParameterId` 的 hash/GUID 规则与重命名迁移；
- binding allocator 的完整 canonical ordering；
- ShaderPackage binary serialization 格式、大小限制和 endian 策略；
- DXC、Windows SDK、SPIRV-Reflect 和 SPIR-V Tools 的具体版本与 SHA-256；
- DXC SPIR-V layout/debug flags 及目标 Vulkan/SPIR-V 版本；
- Vulkan `group -> set`、register-class base 和 decoration 生成方式；
- DXIL/DXBC/SPIR-V debug artifact 采用 embedded 还是 external 的平台细节；
- generated include 的 Editor IntelliSense/virtual-file 映射；
- ShaderLibrary 的磁盘分包和平台 Cook 目录结构。

这些细节若导致公共 Shader 语法、Material 参数身份、RHI Binding 语义或支持平台范围变化，必须先更新本文并重新确认；仅实现选择可以在对应阶段设计记录中确定。

## 21. 实现步骤

### 阶段 0：锁定格式和依赖

工作内容：

1. 创建 `engine/tools/shader_compiler/`、`engine/runtime/rendercore/shader/`、`engine/shader/builtin/` 和 `engine/shader/include/` 目录边界；
2. 建立 `Toy3dShaderCompilerCore`、`Toy3dShaderCompiler` 和新版 `Toy3dShaders` target 骨架，但暂不移除现有 GLSL 闭环；
3. 为 `.shader` v1 写精确 EBNF；
4. 锁定 `ShaderParameterId`、ToyShaderABI、binding canonical ordering 和 package version 规则；
5. 选择并记录 DXC、SPIRV-Reflect、SPIR-V Tools 和 Windows SDK 版本、下载地址、许可证与 SHA-256；
6. 明确 macOS 本地 Vulkan 编译和 Windows 三目标 Cook 的工具发现规则；
7. 在 `engine/tools/shader_compiler/tests/data/` 准备最小 vertex/pixel Shader 和错误用例语料。

验收：相同输入在同一工具版本下产生稳定 AST、参数 schema、layout hash 和 compile request；缺失编译器时配置或工具运行明确失败。

### 阶段 1：ShaderLab Frontend

工作内容：

1. 在 `engine/tools/shader_compiler/frontend/` 实现 `.shader` tokenizer/parser、AST 和 diagnostics；
2. parser 只依赖 ShaderCompiler 公共工具类型，不依赖 `Toy3dRuntime` 或任一 RHI backend；
3. 支持 `Shader`、`Version`、`Properties`、`Resources`、`Variants`、`Pass`、`Requires`、基础 PSO state、`HLSLINCLUDE` 和 `HLSLPROGRAM`；
4. 提取 `#pragma vertex/pixel/compute`；
5. 生成带源码位置的 AST 和 diagnostics；
6. 为合法、非法和边界语法编写单元测试。

验收：示例 `.shader` 可解析为规范化 AST；未知字段、重复属性、缺失 entry、block 未闭合和非法状态都返回精确行列错误。

### 阶段 2：参数布局与 Binding Layout Compiler

工作内容：

1. 在 `engine/tools/shader_compiler/layout/` 实现 ToyShaderABI constant-buffer packer；
2. 生成 `ShaderParameterLayout`、默认值和稳定 parameter ID；
3. 实现逻辑 Binding Group 合并；
4. 实现 `b/t/s/u` 确定性紧凑分配和 limits 验证；
5. 定义并版本化 Vulkan `group -> set` 与 `class_base + slot` 目标映射；
6. 在 `engine/tools/shader_compiler/codegen/` 为 D3D 和 Vulkan 分别生成带 `register()`、`packoffset()`、`#line` 及所需 target decoration 的虚拟 `.hlsli`；
7. 分别计算 normalized layout hash，以及包含 target binding mapping version 的 target compile cache key；
8. 对 packing、数组、matrix、冲突、D3D10 limits 和 Vulkan namespace collision 编写测试。

验收：相同逻辑 schema 与 permutation 始终产生相同 generated HLSL 和 layout hash；不同声明顺序经过 canonicalization 后按规定得到一致或明确不同的结果。

### 阶段 3：Vulkan 编译闭环

工作内容：

1. 在 `engine/tools/shader_compiler/compiler/` 接入锁定版本 DXC；
2. 实现 include resolver、virtual path、dependency tracking 和 diagnostics；
3. 用显式 Vulkan binding decoration 编译 HLSL 到 SPIR-V；
4. 使用 `spirv-val` 验证；
5. 在 `engine/tools/shader_compiler/reflection/` 使用 SPIRV-Reflect 生成 normalized reflection；
6. 将 SPIR-V `set/binding` 反归一化为公共位置，验证 reflection 与预期 schema/layout；
7. 在 `engine/tools/shader_compiler/package/` 实现 ShaderPackage v1 writer，在 `engine/runtime/rendercore/shader/` 实现 reader；
8. 用 ShaderPackage 替换当前 test pass 对裸 `.spv`、手写 reflection、手写 hash 和手写 Binding Layout 的读取。

验收：单个 `.shader` 经 Cook 后在 Vulkan 完成无 validation error 的 render pass/draw/present；同一 group 内的 `b0/t0/s0/u0` 映射到互不冲突的 native binding；ShaderCompiler 生成结果与 Vulkan backend 映射逐项一致；renderscene 不再读取裸 `.spv`，也不再手写 Shader reflection 或 Binding Layout。

### 阶段 4：Shader Runtime 与 RHI 收敛

工作内容：

1. 在 `engine/runtime/rendercore/shader/` 新增 `ShaderLibrary`、ShaderPackage loader 和 target selection；
2. 增加 `RHIShaderBinaryFormat`；
3. 将 `create_shader()`、`create_binding_layout()` 逐步迁移到公共 NVI validation/cache；
4. 从 Program reflection 自动合并 `RHIBindingLayoutDesc`；
5. 实现 parameter ID 到 group/offset/resource binding 的运行时查找；
6. 确保 command list 保活 Shader、Binding Layout、Binding Set 和 PSO；
7. 增加 package corrupt、target mismatch 和 unsupported capability 测试。

验收：运行时只根据当前 backend 从 package 选择 binary；上层不出现物理 slot；错误 package 和错误 backend target 可诊断失败。

### 阶段 5：D3D10 与 D3D12 编译目标

工作内容：

1. Windows 接入 FXC/`D3DCompile` SM4；
2. 接入 DXC DXIL SM6；
3. 分别实现 DXBC 和 DXIL reflection；
4. 对三个目标执行 normalized reflection parity，比较公共 `group + register class + slot` 而非 Vulkan native binding 数字；
5. 将 compiler identity/version/flags 纳入 cache key；
6. 建立 Windows CI 三目标 Shader Cook；
7. 为 capability 差异和 D3D10 不支持路径添加测试。

验收：同一测试 `.shader` 在三目标生成合法 binary；constant layout、资源类型、slot、array count 和 stage visibility 符合预期；不支持的 compute/UAV permutation 对 D3D10 明确返回 `Unsupported`。

### 阶段 6：调试、RenderDoc 与热重载

工作内容：

1. 实现 `ShaderLoadMode` 和 `ShaderDebugMode`；
2. 支持 Debug/Development 的 embedded source、line mapping 和 symbols；
3. 在 RenderDoc 中验证 Vulkan、D3D10、D3D12 的 HLSL 显示和 Shader 调试；
4. 实现 include 文件监听、依赖失效和异步重编译；
5. 实现成功后的安全原子替换和失败保留旧 Shader；
6. layout 变化时重建 Binding/PSO，并按 queue completion 延迟释放旧对象；
7. Shipping 构建剥离源码、symbols、compiler 和文件监听。

验收：修改 HLSL 后 Editor 可热重载；编译失败不破坏当前画面；RenderDoc 能将 binary 映射到原始或生成 HLSL；Shipping package 不包含编译器和调试源码。

### 阶段 7：Global Shader、permutation 与缓存

工作内容：

1. 建立轻量 Shader program registry/GlobalShaderMap；
2. 实现 typed permutation key、capability 裁剪和 canonical serialization；
3. 实现 `ShaderDerivedDataCache` single-flight；
4. 实现 Cook `ShaderLibrary` 与字节码 content-hash 去重；
5. 支持 `ShaderFeature` 与 `MultiCompile` 策略；
6. 将 fullscreen、copy、depth 等内置 pass 迁移到新系统。

验收：只编译/Cook 实际需要的排列；相同字节码去重；include 或 compiler 版本变化能准确使相关缓存失效。

### 阶段 8：Material 与 RDG 接入

工作内容：

1. 实现 Material、MaterialInstance 和参数覆盖；
2. 实现 Material template 与生成 HLSL include；
3. static switch 接入 Shader permutation；
4. 为 Forward、DepthOnly、Shadow 等建立 Shader Pass family；
5. RDG 选择 Shader Pass/permutation，并补充 attachment compatibility；
6. Material 参数和资源通过 parameter ID 构建 Binding Set；
7. 后续 Material Graph 只生成 HLSL、schema 和 permutation，不接触 backend binary。

验收：MaterialInstance 改变动态参数无需重编译；static switch 只切换或触发对应 permutation；RDG、Material 和 Shader 均不引用 backend 类型或物理 slot。
