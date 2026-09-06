# RHI Binding 聚合设计

## 1. 状态与目的

本文定义 Shader Runtime 收尾阶段的公共 Binding 聚合 contract。它细化
`rhi-design.md` 已确认的逻辑 Binding Group 与后端 physical binding 分层，解决
`Vulkan ES3.1 profile` 中 Global 与 View 必须共享 physical set 0，而运行时仍需要按不同
所有权和更新频率维护二者的问题。

本文已确认并完成首个 Vulkan vertical slice 的代码、产物和自动测试闭环。实现删除旧入口后
已同步更新 `shader-system-design.md` 的当前进度；可正常退出并刷新日志的独立 Editor/Vulkan
draw/present 冒烟仍是最终运行时验收项。本文继续保留为 contract 和测试依据。

## 2. 用例与非目标

### 2.1 用例

- renderscene 在 pass 或 draw 录制前分别提供 Global、View、Pass、Material、Object
  logical binding set。
- 一个 Shader Program 可以同时引用 Global 和 View；Vulkan 必须用一次完整的
  descriptor-set materialization 和 bind 提交 set 0，不能让两次逻辑绑定互相覆盖。
- D3D11 仍可把 logical binding set 展开为每个 shader stage、register class 和 slot 的
  binding packet。
- D3D12 仍可把 logical binding set 编译为 descriptor table、root descriptor 或 root
  constants，而不要求公共层暴露 register space 或 root parameter。
- command list 必须保活录制工作引用的 logical binding set、后端 physical packet、资源和
  descriptor allocation，直到 queue completion。

### 2.2 非目标

- 本批次不定义 Global/View 参数的具体字段，不把 camera、lighting 或前向渲染策略下沉到
  RHI。
- 本批次不实现 bindless、descriptor indexing、push constants、root constants 优化或
  persistent descriptor cache。
- 本批次不实现 Material、Forward BasePass、RDG 或多线程 pass 录制。
- 本批次不改变 Shader target mapping；Vulkan 仍固定 set 0=Global+View、set 1=Pass、
  set 2=Material、set 3=Object。

## 3. 公共接口与分层

`RHIBindingSet` 继续只表示一个 logical group。`RHIBindingSetDesc::group` 不改为 physical
set，也不允许调用方为了 Vulkan 合并 Global 与 View。这样 Global 和 View 可以由不同的
上层所有者独立创建和更新。

公共图形绑定快照使用五个具名 logical 引用：

```cpp
struct RHIGraphicsBindings
{
    RHIBindingSetRef global;
    RHIBindingSetRef view;
    RHIBindingSetRef pass;
    RHIBindingSetRef material;
    RHIBindingSetRef object;
};
```

`RHIGraphicsCommandContext` 提供一个原子入口：

```cpp
RHIStatus bind_graphics_bindings(const RHIGraphicsBindings& bindings);
```

该入口表达“替换本次 draw 使用的完整 logical binding 快照”。空引用只允许对应 group
未被当前 graphics pipeline layout 使用；是否缺少 required group 在公共 validation 或
draw 前检查中返回可诊断错误。

公共入口采用 NVI：

1. 检查每个非空 set 的 `group()` 与其字段一致；
2. 检查 set 的 binding layout 与当前 graphics pipeline layout 兼容；在 device ownership
   identity 完成前至少比较规范化 layout value，不能仅比较裸指针；
3. 检查当前 pipeline 所需的 logical group 均已提供；
4. 保留完整快照并调用 backend `bind_graphics_bindings_impl()`；
5. backend 只负责 native materialization 和命令翻译，不重新定义缺失 group 的成功语义。

当前 `bind_binding_set()` 在迁移期只存在于同一批代码修改内，不形成对外双轨。所有调用方
迁移到原子入口、测试通过后删除旧虚函数和实现。

## 4. Vulkan 实现

### 4.1 Logical set 与 physical packet

`VulkanBindingSet` 保存一个 logical group 的已验证资源值，不再把“一个 logical group”
等同于“一个 `VkDescriptorSet`”。命令录制阶段由 Vulkan backend 根据当前 pipeline layout
和完整 `RHIGraphicsBindings` 构造不可变的 physical packet：

```text
physical packet 0 <- Global + View
physical packet 1 <- Pass
physical packet 2 <- Material
physical packet 3 <- Object
```

set 0 的 descriptor pool、descriptor set 和全部 writes 必须在一次 materialization 中完成；
Global 或 View 任一方缺失、layout 不兼容或 write 不完整都失败，不录制半成品 bind。

第一版可在 recording context 内按以下 transient key 去重：

```text
pipeline binding-layout identity
+ physical set index
+ ordered logical binding-set object identities
```

该 key 只用于一次 context 录制期间的临时复用，不进入持久 cache、Shader ABI 或 Cook
产物。context 持有强引用，避免对象地址在 key 有效期间被复用。后续若引入稳定 RHI owner
identity，可替换对象地址而不改变公共接口。

### 4.2 生命周期

- logical set 的 CPU 所有者仍是 renderscene/RenderCore；set 强持有其 buffer、view 和
  sampler。
- recording context 创建 physical packet；packet 强持有组成它的 logical sets、
  `VkDescriptorPool` 和 `VkDescriptorSet`。
- `finish_recording()` 后 command list 接管 packet 强引用。
- queue submit 成功后 frame slot 保活 command list，直到对应 completion fence 完成。
- command list 丢弃、录制失败或 submit 失败时，未进入 GPU 的 packet 随 command list
  正常释放；已提交 packet 不得提前销毁 descriptor pool。
- device shutdown 先停止新录制和创建，等待 GPU idle、释放 in-flight command lists，最后
  销毁 native device。

### 4.3 状态与错误

- physical set 只在绑定快照或 pipeline layout 变化后重新 materialize/bind。
- sampled texture 与 uniform buffer 的 resource-state validation 仍在 draw 前执行，覆盖组成
  physical packet 的全部 logical sets。
- 缺少 required logical group、group 字段错位、layout 不兼容返回 `InvalidArgument`。
- profile limits 或尚未支持的 storage/buffer-view path 返回 `Unsupported`。
- descriptor pool/set 分配和 update 前置操作失败返回对应 backend failure；失败后不录制
  `vkCmdBindDescriptorSets`。
- 禁止以空 descriptor、部分 write 或无操作方式返回成功。

## 5. D3D11、D3D12 与移动端映射

### 5.1 D3D11

D3D11 backend 接收同一 `RHIGraphicsBindings`，按 Shader target mapping 展开为 VS/PS/未来
CS 各 stage 的 CBV、SRV、sampler 和 UAV slots。Global 与 View 不需要 native 聚合，但必须
以同一次公共绑定快照参与 validation。冲突 SRV/UAV/RTV 在 backend state tracker 中解除或
返回诊断错误。Feature Level 基线保持 11_0、Shader Model 5.0。

### 5.2 D3D12

D3D12 backend 可按 pipeline layout 把一个或多个 logical groups materialize 为 descriptor
table/root bindings。descriptor allocation 与组成它的 logical sets 一并由 command list 保活
到 fence completion。公共接口不固定 register space、descriptor heap offset 或 root parameter。

### 5.3 Vulkan ES3.1 profile 与移动端

默认 profile 保持 Vulkan 1.1、SPIR-V 1.3 和最多四个 bound descriptor sets。Global+View
共享 set 0 是 profile contract，不依赖 descriptor indexing、update-after-bind 或其他可选
feature。Cook 和 runtime 继续验证每 stage、每 set 与 pipeline 总 limits；聚合不能绕过
uniform buffer、sampled image 和 sampler 数量限制。

## 6. 所有权、线程与安全边界

- 一个 command context 只能由一个线程录制；其 transient physical-packet cache 不共享。
- immutable Shader、layout、logical binding set 可跨 context 只读共享。
- device 级 descriptor allocator 若后续共享，必须内部同步；第一版优先使用
  context/frame-local allocation，避免新增不可替换的全局单例。
- 公共 descriptor 不包含 `Vk*`、`ID3D11*`、`ID3D12*`、descriptor set index、heap handle
  或 root parameter。
- backend 必须检查资源来自兼容 device；公共 owner identity 完成前保留现有 backend
  类型检查，并把同类型跨 device 校验列为既有 RHI P1，不在本批次伪装解决。

## 7. 测试矩阵

### 7.1 公共 validation

- 正确的五 group 快照；
- pipeline 只使用部分 group 时允许其余字段为空；
- required Global 或 View 缺失；
- set 放入错误字段；
- layout value 不兼容；
- empty layout 和无 binding 的合法 pass；
- binding set 资源不完整、重复 slot/array 或错误 resource type。

### 7.2 Vulkan

- 仅 Material 的现有 test pass 保持通过；
- 同一 Program 同时引用 Global 与 View resource，set 0 的 native binding 连续且一次 bind
  完成；Global/View constant schema 在 renderer parameter contract 定型后补充，不改变聚合接口；
- 只更新 View 后生成新 set 0，旧 packet 在 GPU completion 前仍存活；
- Global/View layout mismatch、缺失一方和 descriptor 分配失败不录制 draw；
- 多 draw 重复相同快照复用 recording-local packet；
- command list 丢弃、提交失败和多 frame-in-flight 不提前销毁 descriptor pool；
- validation layer 下无 descriptor 未写、错误 set 覆盖和 lifetime error。

### 7.3 后续后端

- D3D11：VS/PS 分 stage 与 register class 的 Global/View slot 均正确，不比较 Vulkan
  native slot；
- D3D12：root signature/table 映射覆盖完整逻辑身份，descriptor allocation 延迟回收；
- 三 target reflection parity 只比较逻辑身份、类型、数组、constant layout 和 stage
  visibility。

## 8. 迁移顺序与旧实现删除条件

1. 增加 `RHIGraphicsBindings`、公共 NVI validation 和单元测试。
2. 将 Vulkan graphics state 改为保存完整 logical snapshot，引入 recording-local physical
   packet materializer 和 command-list lifetime root。
3. 迁移现有 test pass 到 `bind_graphics_bindings()`，确认 Material-only 路径不回退。
4. 增加同时使用 Global/View resource binding 的 Shader 与 runtime 测试，完成 Vulkan set 0
   聚合闭环；constant buffer 数据 contract 随后由 renderscene/RenderCore 独立定义。
5. 删除 `bind_binding_set()`、Vulkan“一个 logical group 等于一个 physical set”的创建路径
   和 `Global+View ... not implemented` 诊断。
6. 更新 Shader 系统当前进度；随后进入 Cook `ShaderCodeLibraryLoader`。

只有以下条件全部满足才删除旧入口：公共 validation 测试、Shader compiler/layout 测试、
ShaderMap/Loader 测试、Vulkan Global+View 测试、`Toy3dEditor` 构建和实际 Vulkan
render/present 冒烟均通过。D3D11/D3D12 后端未接入期间，公共接口与测试 contract 必须保留
其可实现性，不能把 Vulkan physical set 术语带入调用方。
