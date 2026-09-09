## Why

当前 `RHIBindingSet` 以完整 Shader Program `RHIBindingLayout` 和 target slot 为身份，使 View、Material、Object 等逻辑资源所有者反向依赖具体消费 Program；相同数据在不同 Program layout 下需要 adapter 或重新物化。同时，高频 Object 常量仍按 draw 创建独立 GPU buffer，Vulkan physical packet 还以单 packet 单 descriptor pool 的方式分配，现有模型无法自然承载跨 Pass 复用和后续 ShadowPass。

## What Changes

- **BREAKING** 将 `RHIBindingSet` 重定义为由一个 logical Binding Group 拥有、按稳定 `ShaderParameterId` 索引、与 Program layout 和 target slot 无关的不可变资源快照。
- **BREAKING** 从 `RHIBindingSetDesc` 删除完整 layout，从 `RHIBindingValue` 删除 slot；为 `RHIBindingLayoutEntry`、Shader reflection 和 runtime Program metadata补齐稳定 binding identity、constant-buffer size 与完整数据 layout hash。
- **BREAKING** 旧 ShaderMapEntry/target mapping 产物不保留兼容 reader；提升必要格式与 mapping version，旧产物明确拒绝并全量重编。
- 由当前 Pipeline layout 在 draw/dispatch 前解析 active binding ID、类型、数组和 constant ABI；logical BindingSet 可以是 superset，未被当前 Program 使用的资源不得参与 transition、native binding 或 command-list GPU payload。
- 删除 backend-specific logical BindingSet 创建路径；公共 RHI frontend 创建和验证 logical set，Vulkan/D3D11/D3D12 backend 只物化当前 Pipeline 所需的 native binding packet。
- 为 Global、View、Pass、Object 等高频常量增加 recording-scoped transient uniform allocation，返回跨后端的 buffer slice 语义；Vulkan/D3D12 使用分页 upload/uniform storage，D3D11 FL11_0 可退化为 pooled standalone constant buffer。
- Vulkan 将 descriptor pool 收敛为按 command-list/completion 生命周期管理的分页 arena，以 physical-set layout signature 和 active resources 缓存 packet，并在满足 limits 时用 dynamic uniform offsets 排除高频 slice offset 对 descriptor packet identity 的影响。
- 一次迁移 View、Material、Object、Tonemap、ImGui 及后续 mesh pass 的 binding 构建方式；删除 View adapter cache、Material per-layout cache、per-draw 独立 uniform buffer helper 和全部旧正式入口，不保留 alias、wrapper 或双轨路径。
- 保持五个 logical Binding Group 以及 Vulkan portable 的四 physical sets 映射不变；公共 contract 同时满足 Vulkan 1.1/移动端 ES3.1 profile、D3D11 FL11_0/SM5 和 D3D12。

## Capabilities

### New Capabilities

- `rendercore-shader-bindings`: 定义稳定 Shader binding identity、constant-buffer ABI identity、Program active layout、target mapping 与破坏性产物版本迁移。
- `rhi-logical-bindings`: 定义 layout-independent logical BindingSet、Pipeline-driven active resource resolution、五组绑定快照、backend native materialization、验证和生命周期。
- `rhi-transient-uniform-data`: 定义 recording-scoped transient uniform allocation、buffer slice、对齐、失败、command-list 保活与三后端实现边界。

### Modified Capabilities

- `game-render-framework/view-render-flow`: View 每帧只准备一个 canonical uniform slice 和一个 logical View BindingSet，不再按 Program layout 建立 adapter。
- `game-render-framework/material-updates`: Material binding cache 改由参数/resource/schema generation 决定，可跨兼容 Shader Pass/variant 复用，不再持有或比较 Program binding layout。

## Impact

- 公共接口主要影响 `engine/runtime/drivers/rhi/` 的 binding descriptor、resource、device frontend、command validation 和 pipeline layout contract。
- Vulkan 影响 binding creation/materialization、graphics state、command context、descriptor pool/packet 生命周期与测试 observation；D3D11/D3D12 尚未实现的后端必须由公共测试和设计保持可实现性。
- ShaderCompiler、ShaderFormat、ShaderMap loader/program/cache、reflection、Entry fixtures 和生成 shader 产物需要同步提升 identity/layout metadata 与版本。
- RenderCore/RenderScene 影响 View uniform、primitive uniform、`MaterialRenderProxy`、BasePass、Tonemap 和 ImGui binding 路径；未来 ShadowPass 直接消费相同 logical binding contract。
- `document/rhi-design.md`、`document/rhi-binding-aggregation-design.md` 和 `document/shader-system-design.md` 的 Active contract 必须同批更新；已完成的 `modularize-scene-mesh-passes` 中 adapter 描述由本 change 的新 contract 取代。
