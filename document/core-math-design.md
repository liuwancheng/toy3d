# Toy3d Core Math 设计

## 1. 文档状态与核心决定

本文定义 Toy3d 面向 Runtime、Editor、Tools、RenderScene 与公共 RHI 的共享数学 contract。实现位于 `engine/core/math/`，CMake target 继续使用 `Toy3dMath`。本文确认以下长期决定：

- Toy3d 第一方模块只使用 Toy3d 公共数学类型和函数，不直接包含或调用 GLM；GLM 是 `Toy3dMath` 的实现依赖，不是引擎公共 API。
- 公共类型采用 Toy3d 自有名称、稳定标量存储和显式布局，不使用 `using Vector3 = glm::vec3`，也不在公共接口暴露 `glm::*` 转换。
- 全引擎空间 contract 固定为 left-handed、`+X` right、`+Y` up、`+Z` forward、1 unit = 1 centimeter、column vector、column-major storage、HLSL `mul(matrix, vector)`。
- clip depth 固定为 0..1 reversed-Z：near = 1、far = 0。Vulkan viewport Y 修正仍由 backend 完成，Core Math 不生成后端专用投影。
- `Transform`、方向到 rotation、LookAt、投影矩阵和通用几何值属于 Core Math；Scene hierarchy、Camera 行为、Editor orbit/undo、RenderScene View 组装仍属于各自业务模块。
- 非法或退化构造使用显式 checked API，失败不写入输出、不记录日志、不静默选择备用方向。
- 迁移按可独立验证的小批次进行；旧 GLM aliases 与新类型只在受控迁移期并存，并设置删除条件，不建立长期双轨入口。

本文是实施前设计基线。若后续实现需要改变公共类型布局、坐标约定、错误模型或依赖方向，必须先更新本文。

位置、网格顶点、bounds、长度、裁剪距离、灯光范围和阴影距离均以厘米保存并计算；Editor 直接显示厘米。Transform scale、单位方向、角度、UV、颜色与淡出比例不随单位改变。矩阵和 RHI ABI 不做额外单位换算。相机默认 near/far 为 `10/100000 cm`，方向光默认动态阴影距离为 `10000 cm`。有物理长度含义的最小距离/padding 使用厘米值；归一化、角度和矩阵可逆性容差保持各自数学语义，不能统一乘以 100。

## 2. 设计来源与取舍

### 2.1 UE4.27Plus 源码结论

本设计核对了本机 `D:/ue4.27plus/Engine/Source` 中以下 UE4.27Plus 实现：

- `Runtime/Core/Public/Math/UnrealMathUtility.h`：标量、容差、插值与通用算法；
- `Vector.h`、`Quat.h`、`Matrix.h`：值类型自身提供 finite、normalize、position/vector 变换等语义；
- `TransformNonVectorized.h`：`FTransform` 统一 translation、rotation、scale，并显式区分 `TransformPosition`、`TransformVector` 与逆变换；
- `RotationMatrix.h`：`MakeFromX`、`MakeFromXZ` 等接口从一个或两个约束轴构造正交基；
- `PerspectiveMatrix.h`、`OrthoMatrix.h`：普通与 reversed-Z 投影是 Core Math 的具名构造；
- `CameraTypes.h`、`CameraComponent.cpp`：Camera 保存位置、旋转与投影策略，形成 `FMinimalViewInfo`；
- `SceneView.h`、`SceneView.cpp`：View 层组合 view/projection 及其逆矩阵，不把 Camera 行为下沉到 Core Math。

Toy3d 借鉴这些稳定职责，不复制 UE 的类型前缀、宏、反射、行向量布局、默认前向轴、历史兼容层或完整 API 规模。Toy3d 的矩阵公式必须按自身 column-vector 与 `+Z` forward contract 独立测试，不能通过名称相似直接移植 UE 公式。

### 2.2 为什么不能继续公开 GLM alias

当前 `math/math.h` 使用 `vec3 = glm::vec3`、`mat4x4 = glm::mat4x4` 等 aliases，导致：

- 任意业务模块都能绕过 Toy3d contract 调用 `glm::lookAt`、`glm::perspective` 或不同 handedness/depth 版本；
- GLM 配置宏、alignment、构造行为和头文件依赖成为公共 ABI 与编译 contract；
- position、direction、normal 等不同空间语义只能靠调用者记忆；
- finite check、safe normalize、inverse 和矩阵构造在业务模块重复实现；
- 将来调整 SIMD、存储或第三方实现需要全仓迁移。

因此只给 GLM 函数增加一层 free-function wrapper 只能作为短期迁移手段，不能作为最终设计。

### 2.3 GLM 的保留方式

公共值类型以 `float`、`std::uint32_t` 等稳定标量保存数据。简单、可 `constexpr`、对性能敏感的逐分量操作直接实现；四元数插值、矩阵求逆、decompose 等复杂算法可以在 `engine/core/math/detail/` 或 `.cpp` 中转换为 GLM 类型计算，再转换回 Toy3d 类型。

`detail/` 中的转换不是公共逃生口：

- 业务 target 不获得 `detail/` 独立 include 入口；
- 公共头文件不声明 `glm::*` 参数、返回值或成员；
- 不提供 `native()`、公开 conversion operator 或可长期依赖的 GLM bridge；
- RHI backend 与 Shader upload 通过 Toy3d 类型的 `data()`、具名分量访问和布局断言读取数据。

这保留 GLM 作为成熟算法实现，同时由 Toy3d 持有 API、语义、布局与迁移权。

## 3. 用例与非目标

### 3.1 必须支持的用例

- GameScene 的 local/world transform、attachment、相机方向和对象朝向；
- Editor gizmo、orbit/fly camera、grid/angle snapping、selection ray 与坐标空间转换；
- RenderScene 的 view/projection、frustum、bounds、normal transform 与 culling；
- RHI 描述中的向量、颜色、矩阵等纯值，不泄漏任何图形后端类型；
- Tools 的资源导入、坐标转换、bounds 计算和确定性离线处理；
- CPU 与 HLSL 之间明确、可测试的矩阵与向量布局；
- 非法输入、退化方向和奇异矩阵的可诊断失败；
- Debug、Release、Windows、macOS、Linux 与 Android 上一致的基础语义。

### 3.2 第一阶段非目标

- 不照搬 UE 全部 Math、Geometry、Large World Coordinates 或 SIMD abstraction；
- 不在第一阶段实现 double-precision world、fixed-point、任意维模板向量或表达式模板；
- 不提供可在运行时切换的 handedness、clip depth 或 row/column convention；
- 不在 Core Math 实现 Scene hierarchy、Camera controller、Editor undo、physics 或 animation policy；
- 不把所有数学失败变为异常，不在热路径分配字符串；
- 不立即一次性迁移全仓所有 aliases 和 GLM 调用；
- 不承诺公共值类型与 GLM 类型二进制兼容。

## 4. 模块、目录与 CMake target

目标目录如下：

```text
engine/core/math/
├── math.h
├── math_constants.h
├── scalar_math.h
├── angle.h
├── vector2.h
├── vector3.h
├── vector4.h
├── integer_vector.h
├── quaternion.h
├── matrix3.h
├── matrix4.h
├── transform.h
├── matrix_construction.h
├── color.h
├── random.h
├── geometry/
│   ├── ray.h
│   ├── plane.h
│   ├── sphere.h
│   ├── axis_aligned_box.h
│   └── frustum.h
└── detail/
    ├── glm_types.h
    └── glm_conversion.h
```

文件按实施批次逐步加入，不要求第一批创建所有空壳。`math/math.h` 是稳定聚合入口；Core、RHI 等对编译时间敏感的模块可包含具体公共头文件。`matrix_construction.h` 只接收输出为 Matrix3/Matrix4、且不属于 Transform、Quaternion 等更具体值类型的纯语义构造；matrix algebra、Camera policy 和 backend correction 不得进入。`random.h` 不再由 `math.h` 强制包含，随机数与空间数学保持可独立使用。

`Toy3dMath` 保持独立第一方 target：

```cmake
add_library(Toy3dMath STATIC)
target_compile_features(Toy3dMath PUBLIC cxx_std_17)
set_target_properties(Toy3dMath PROPERTIES CXX_EXTENSIONS OFF)
target_link_libraries(Toy3dMath PRIVATE glm::glm)
```

只有当公共头文件已不包含 GLM 时，`glm::glm` 才能真正收敛为 `PRIVATE`。迁移期间可以暂时保持 `PUBLIC`，但每个批次必须减少直接 GLM 调用，不能以兼容为由新增调用点。

依赖方向固定为：

```text
Runtime / Editor / Tools / RenderScene / RHI
                    |
                Toy3dMath
                    |
              glm::glm (PRIVATE)
```

`Toy3dMath` 不依赖 GameScene、RenderScene、RHI、Editor、平台窗口或图形后端。

## 5. 公共命名与类型集合

第一方类型继续遵守 PascalCase，不采用 UE 的 `F`/`E`/`T`/`I` 前缀：

```cpp
Vector2
Vector3
Vector4
UIntVector2
UIntVector3
UIntVector4
Quaternion
Matrix3
Matrix4
Transform
Radians
Degrees
LinearColor
```

第一阶段不引入通用 `Vector<T, N>` 公共模板。显式类型更容易控制布局、构造、错误语义、调试显示和 Shader ABI。

`Color` 不能继续只是 `Vector4` 的无区别 alias。长期使用 `LinearColor` 表达线性浮点颜色；8-bit sRGB/packed color 在出现明确资产或 UI 用例时单独设计。

所有类型必须：

- 默认构造为确定值；向量为零、Quaternion 与 Matrix/Transform 为 identity；
- 支持复制、移动和值比较所需的最小操作；
- 提供 `is_finite()` 或统一的 `is_finite(value)`；
- 不提供隐式 GLM 转换；
- 用 `static_assert` 锁定 size、alignment、standard-layout/trivially-copyable 等实际需要的性质；
- 不使用未记录的 compiler packing 或依赖 GLM alignment；
- 对 GPU upload 需要的类型明确 padding，并验证 padding 初始化为零。

## 6. 标量、常量与容差

现有 `PI`、`INVIRSE_PI`、`EPSILON` 等全局常量需要迁移为正确拼写的 `inline constexpr`。不再用一个 `EPSILON` 同时承担所有判断。

建议至少区分：

```cpp
inline constexpr float k_pi = 3.14159265358979323846f;
inline constexpr float k_two_pi = 2.0f * k_pi;
inline constexpr float k_half_pi = 0.5f * k_pi;
inline constexpr float k_default_float_tolerance = 1.0e-6f;
inline constexpr float k_normalization_tolerance_squared = 1.0e-12f;
inline constexpr float k_matrix_inverse_tolerance = 1.0e-8f;
```

具体数值在实现批次中由测试和现有场景数据确认，但名字必须表达用途。几何比较默认采用 absolute tolerance；需要跨大尺度工作时再设计 combined absolute/relative comparison，不能悄悄改变既有调用语义。

`ScalarMath` 或 namespace free functions 提供：

```cpp
abs
min
max
clamp
lerp
square
sqrt
inverse_sqrt
is_finite
is_nearly_zero
is_nearly_equal
```

不保留需要构造对象才能设置全局状态的 `Math`。若继续使用 `Math::` 风格，所有函数也必须是无状态 static utility；不得保留可变默认角度单位。

## 7. 角度 contract

角度使用显式单位类型：

```cpp
class Radians;
class Degrees;

Radians to_radians(Degrees value);
Degrees to_degrees(Radians value);
```

规则如下：

- 裸 `float` 不在 degree 与 radian 之间隐式转换；
- 三角函数接收 `Radians`；
- Editor 属性和 Camera 可公开 `Degrees`，投影构造统一接收 `Radians`；
- `Radians * scalar` 与 `Radians / scalar` 合法；两个角度相乘或 `scalar / angle` 不返回角度；
- 删除全局 `AngleUnit`、模糊 `Angle` 与 `get_angle()`；
- exact equality 只用于确有需要的值语义，几何判断使用具名 tolerance API。

## 8. 向量、Quaternion 与 Matrix contract

### 8.1 向量

基础运算包括逐分量加减乘除、标量乘除、dot、cross、长度、距离、finite check 和近似比较。归一化必须区分：

```cpp
bool try_normalize(const Vector3& value, Vector3& result);
Vector3 normalized_or_zero(const Vector3& value);
Vector3 normalize_unchecked(const Vector3& value);
```

- `try_normalize()` 用于 View、rotation、physics query 等必须区分退化输入的构造；失败不修改 `result`。
- `normalized_or_zero()` 只用于调用方明确接受零结果的算法，名称必须暴露回退行为。
- `normalize_unchecked()` 只用于前置条件已由附近代码或类型 invariant 保证的热路径；Debug 构建应断言。

### 8.2 Quaternion

Quaternion 表达 rotation，identity 固定为无旋转。公共构造包括 axis-angle、Euler、rotation matrix 与两个方向之间的旋转。必须提供 normalize、inverse、rotate/unrotate vector、slerp 与近似旋转相等。

Quaternion 的四分量顺序必须在公共类型注释、`data()` 和 Shader/序列化边界中固定；构造调用不得依赖 GLM 构造参数顺序。`q` 与 `-q` 表达相同 rotation，`is_nearly_same_rotation()` 必须处理这一点。

Euler 只作为输入、显示和序列化边界，不作为 SceneComponent 内部累计 rotation 的主要表示。Euler 顺序、正方向和返回范围必须在新增 API 前单独锁定测试。

### 8.3 Matrix

`Matrix3` 与 `Matrix4` 使用 column-major storage 和 column-vector multiplication。访问接口明确为：

```cpp
float& at(std::size_t column, std::size_t row);
const float& at(std::size_t column, std::size_t row) const;
const float* data() const;
```

不得提供含糊的 `operator[]` 作为唯一公共访问方式。乘法满足：

```text
clip_position = projection * view * world * local_position
child_world = parent_world * child_local
```

矩阵操作至少包括 identity、transpose、determinant、checked inverse、矩阵乘法和矩阵-Vector4 乘法。具名空间变换必须区分：

```cpp
Vector3 transform_position(const Matrix4&, const Vector3&);
Vector3 transform_vector(const Matrix4&, const Vector3&);
bool try_transform_normal(const Matrix4&, const Vector3&, Vector3& result);
```

`transform_position` 使用 `w = 1`；`transform_vector` 使用 `w = 0`；normal 使用 linear transform 的 inverse-transpose，并在奇异矩阵时失败。

## 9. Transform contract

`Transform` 是跨 Game、Editor、Tools 与 Renderer 的共享纯值：

```cpp
struct Transform
{
    Vector3 translation{0.0f, 0.0f, 0.0f};
    Quaternion rotation = Quaternion::identity();
    Vector3 scale{1.0f, 1.0f, 1.0f};
};
```

它提供：

```cpp
Matrix4 to_matrix(const Transform& transform);
bool try_decompose_transform(const Matrix4& matrix, Transform& result);

Vector3 transform_position(const Transform&, const Vector3&);
Vector3 transform_vector(const Transform&, const Vector3&);
Vector3 transform_direction(const Transform&, const Vector3&);
Vector3 inverse_transform_position(const Transform&, const Vector3&);
Vector3 inverse_transform_vector(const Transform&, const Vector3&);

Vector3 right(const Transform&);
Vector3 up(const Transform&);
Vector3 forward(const Transform&);
```

其中 `transform_vector` 包含 scale，`transform_direction` 只应用 normalized rotation。Camera axes、light direction 和 Editor gizmo orientation 使用 direction 语义，不能从带非均匀 scale/shear 的矩阵列直接假定为单位方向。

第一阶段沿用项目当前 positive non-zero scale 约束。负 scale、mirror 与 zero scale 的支持会影响 decomposition、front face、normal 和 attachment，必须作为后续独立设计，不通过放宽验证悄悄引入。

TRS 无法表示任意 shear。`try_decompose_transform()` 遇到无法用当前 Transform contract 重建的矩阵时失败。Scene hierarchy 是否保存 matrix 以保留 parent rotation + non-uniform scale 产生的 shear，仍由 GameScene 设计决定；Core Math 只提供可验证的值运算，不改变 hierarchy 策略。

## 10. 方向构造与 LookAt

### 10.1 Forward/up rotation

Core Math 不公开独立 basis 值类型。right、up、forward 三轴只作为 direction-to-rotation 构造中的局部中间值，避免让与正交 rotation matrix 数据重叠的类型进入公共 API。left-handed 正交化顺序固定为：

```text
forward = normalize(input_forward)
right   = normalize(cross(requested_up, forward))
up      = cross(forward, right)
```

公共 checked API 直接输出 rotation：

```cpp
bool try_make_rotation_from_forward_up(
    const Vector3& forward,
    const Vector3& requested_up,
    Quaternion& result);
```

测试必须通过 rotation 后的 canonical axes 验证 `cross(right, up) == forward`、三个轴为单位向量且两两正交。

### 10.2 View

已有 position/orientation 时构造 world-to-view：

```cpp
bool try_make_view_matrix(
    const Vector3& position,
    const Quaternion& orientation,
    Matrix4& result);
```

LookAt 是 target-based convenience：

```cpp
bool try_make_look_at_view_matrix(
    const Vector3& eye,
    const Vector3& target,
    const Vector3& requested_up,
    Matrix4& result);
```

其语义严格为 world-to-view matrix，不修改任何 Scene 对象。Camera 已有 orientation 时必须使用 `try_make_view_matrix()`，不能构造临时 target 再调用 LookAt。

以下情况失败并保持输出不变：

- 任一输入包含 NaN/Inf；
- `eye` 与 `target` 距离低于 normalization tolerance；
- requested up 为零；
- requested up 与 forward 平行或近似平行；
- orientation 非有限或无法在容差内归一化。

Core Math 不自动选择 world X/Z 作为备用 up，因为这会引入 roll、连续性和交互策略。Editor orbit camera 或 Game targeting 如需回退，由对应 controller 明确实现。

`SceneComponent::look_at()`、Camera smoothing、pitch/yaw/roll lock、parent-space 转换、dirty propagation 与 Editor undo/redo 均不属于 Core Math。

## 11. Projection contract

公共投影函数不在名称中重复 `reversed_z`，因为 reversed-Z 是 Toy3d 唯一合法公共 contract，而不是调用方选项：

```cpp
struct PerspectiveProjectionDesc
{
    Radians vertical_fov;
    float aspect = 1.0f;
    float near_clip = 10.0f;
    float far_clip = 100000.0f;
};

bool try_make_perspective_projection(
    const PerspectiveProjectionDesc& desc,
    Matrix4& result);

struct InfinitePerspectiveProjectionDesc
{
    Radians vertical_fov;
    float aspect = 1.0f;
    float near_clip = 10.0f;
};

bool try_make_infinite_perspective_projection(
    const InfinitePerspectiveProjectionDesc& desc,
    Matrix4& result);
```

有限投影要求 finite、`0 < fov < pi`、`aspect > 0`、`0 < near < far`。无限投影要求 finite、`0 < fov < pi`、`aspect > 0`、`near > 0`。

正交投影在 CameraComponent 真正接入前完成具体 descriptor 设计，至少明确 width/height 或 bounds、near/far、ViewRect/aspect policy。不能只为占位提前公开未经用例验证的参数集合。

投影结果必须满足：

- view-space `+Z` 为前方；
- near 映射到 NDC depth 1；
- finite far 映射到 NDC depth 0；
- infinite far 在 `z -> infinity` 时趋近 0；
- projection 不进行 Vulkan Y flip；
- projection 不根据 RHI backend 或 OS 分支；
- HLSL 使用 `mul(projection, position)` 等价于 CPU column-vector contract。

CameraComponent 保存 projection mode、FOV、near/far 等业务配置；ViewRect 决定 aspect；SceneView builder 调用 Core Math 并组合 inverse matrices。Core Math 不依赖 CameraComponent 或 SceneViewRect。

## 12. Geometry 的后续边界

Game、Editor、Tools 与 RenderScene 共同需要的纯几何值应逐步进入 `math/geometry/`：

- `Ray`；
- `Plane`；
- `Sphere`；
- `AxisAlignedBox`；
- `Frustum`。

相交、包含、closest-point 与距离函数保持无业务状态。RenderScene 的 visibility policy、Game collision response、Editor selection priority 不下沉。

当前 `runtime/rendercore/geometry/axis_aligned_bounds.h` 在迁移前继续使用；只有在公共类型、调用方和测试明确后才移动，不能先复制一套 `AxisAlignedBox` 长期共存。

screen projection/deprojection 预计由 View 相关共享函数组合 `Matrix4` 与 viewport rectangle 实现；viewport rectangle 的通用类型归属在 Editor 实际接入前确认，不把 RenderScene ID 或输出资源引入 Core Math。

## 13. 错误模型与日志边界

数学运算分为三类：

1. 对全部输入都有定义的 total operation，直接返回值，例如 vector addition、dot、clamp。
2. 有明确安全回退且函数名暴露回退的 operation，直接返回值，例如 `normalized_or_zero()`。
3. 可能因非有限、退化或奇异输入失败的 construction，使用 `try_*` 和输出参数。

第一阶段 checked API 统一采用：

```cpp
bool try_operation(const Input& input, Output& result);
```

失败时 `result` 保持原值，函数不分配字符串、不抛异常、不记录日志。调用方掌握对象身份和业务上下文，由调用方形成 `RHIStatus`、diagnostic 或日志。

若实现中确认调用方必须区分 `NonFiniteInput`、`DegenerateInput`、`SingularMatrix` 等原因，再引入专用 `MathError`；不得预先创建包含字符串、堆分配或通用基础设施的复杂 result system。

`unchecked` 函数只在性能分析证明需要且前置条件稳定时增加；Debug 断言不能替代公开 checked path。

## 14. 所有权、生命周期与线程模型

Core Math 类型均为 owned value，没有外部资源、引用计数、全局初始化或 composition root 生命周期。只读运算和只使用局部变量的函数天然可并发调用。

禁止在 `Toy3dMath` 增加：

- 可变全局 AngleUnit、tolerance 或坐标模式；
- 隐式线程本地 scratch state；
- 不可替换随机全局单例；
- lazy 初始化且未同步的 lookup table；
- 依赖 Logger、TaskGraph 或平台窗口的行为。

随机数生成器持有自己的 engine state，由 owner 管理生命周期；跨线程共享同一 generator 需要调用方同步。`random.h` 的现有 `CHAOS_*` 残留和无效 owning pointer 必须在随机模块批次单独清理。

## 15. 平台、精度与安全边界

- 第一阶段公共 world math 使用 IEEE-754 `float`；不根据平台切换为 `double` 或 `half`。
- 禁止依赖 fast-math 下 NaN/Inf 检测仍可用；若未来启用 fast-math，必须单独验证 checked API。
- 不使用 C++ 未定义行为实现 type punning、union alias 或假定 GLM layout。
- `data()` 指向 Toy3d 类型自身连续标量布局，生命周期只覆盖对象本身。
- 矩阵索引、数组长度和循环边界由具名常量推导。
- GPU buffer packing 由 Shader ABI 类型负责；不能因为 `Vector3` 逻辑上有三个 float 就假定任意 constant-buffer stride 为 12 bytes。
- 序列化按具名分量写入，不直接 dump C++ object bytes；版本、endianness 与 padding 不属于内存布局的隐式承诺。
- D3D11、D3D12、Vulkan 与移动 Vulkan 使用同一 CPU 数学结果，backend 只处理 native viewport、front face 和资源 API 差异。

## 16. API 使用示例

Camera/View 构造：

```cpp
Matrix4 view_matrix;
if (!try_make_view_matrix(camera_position, camera_orientation, view_matrix))
{
    return false;
}

PerspectiveProjectionDesc projection_desc;
projection_desc.vertical_fov = to_radians(camera_fov);
projection_desc.aspect = viewport_aspect;
projection_desc.near_clip = near_clip;
projection_desc.far_clip = far_clip;

Matrix4 projection_matrix;
if (!try_make_perspective_projection(projection_desc, projection_matrix))
{
    return false;
}

const Matrix4 view_projection = projection_matrix * view_matrix;
```

Editor LookAt rotation：

```cpp
const Vector3 forward = target - position;
Quaternion orientation;
if (!try_make_rotation_from_forward_up(forward, world_up, orientation))
{
    return false;
}

// Editor command 在这里处理 parent space、undo 和 dirty state。
```

Core Math 不提供修改 SceneComponent 的重载。

## 17. 测试矩阵

新增 `engine/core/tests/math_tests.cpp` 与 `Toy3dMathTests`，测试至少覆盖：

### 17.1 类型与布局

- 默认值、identity、size、alignment、连续 data；
- Vector、Quaternion、Matrix 的复制与初始化；
- 所有 padding 为确定值；
- 公共头文件可独立包含；
- 公共头文件不包含 GLM，不暴露 `glm::`。

### 17.2 标量与角度

- degree/radian 往返；
- pi、半圈、整圈与负角；
- near-equal tolerance 边界；
- NaN、正负 Inf；
- clamp 与插值端点。

### 17.3 Vector 与 Quaternion

- dot/cross 与 left-handed basis；
- zero、near-zero 和非有限 normalize；
- axis-angle 与 rotate/unrotate；
- `q` 与 `-q` 的相同 rotation；
- slerp 端点、近共线和 opposite rotation；
- forward/up 构造的正交性和退化失败原子性。

### 17.4 Matrix 与 Transform

- identity、translation、rotation、scale 与 `T * R * S`；
- `parent_world * child_local`；
- position/vector/normal 的不同结果；
- inverse 往返与 singular/non-finite 失败；
- Transform matrix round-trip；
- non-uniform scale、无法表示的 shear 与 decomposition 失败；
- Camera direction 不受 scale 影响。

### 17.5 View 与 Projection

- identity Camera 位于原点并朝 `+Z`；
- 任意 eye/orientation 的 world-to-view；
- LookAt 将 eye 映射到原点、target 映射到 view `+Z`；
- eye==target、zero up、parallel up、NaN/Inf 失败且输出不变；
- finite perspective near=1、far=0；
- infinite perspective near=1、远处趋近 0；
- aspect 与 vertical FOV 边界；
- CPU matrix 与等价 HLSL column-vector golden values；
- Vulkan 不在 projection 中额外翻转 Y。

### 17.6 集成与平台

- `Toy3dSceneViewTests` 迁移后继续覆盖 ViewRect、inverse matrices 和失败原子性；
- `Toy3dGameSceneTests` 覆盖 hierarchy 与 Transform；
- 未来 viewport/player 侧的 View 构建测试覆盖 Camera snapshot；
- MSVC Debug/Release；
- Clang/GCC；
- x64 与 Android ARM64；
- D3D11、D3D12、Vulkan shader matrix golden test。

每个实施批次至少运行 `Toy3dMathTests`、受影响集成测试和 `Toy3dEditor`；最终批次运行完整 CTest。

## 18. 分阶段迁移

### 批次 A：锁定 contract 与现有行为

- 增加本文和 `Toy3dMathTests`；
- 用 golden tests 锁定现有 left-handed、column-vector、reversed-Z 结果；
- 盘点并建立业务模块直接 `glm::` 调用清单；
- 暂不改变现有 aliases，避免测试与实现同时失去基准。

当前第一方 GLM 迁移基线如下，新增代码不得扩大该清单：

| 区域 | 直接依赖 | 计划收敛批次 |
| --- | --- | --- |
| `engine/core/math/math.h` | 公共 GLM include 与旧 aliases | C、G |
| `runtime/renderscene/view/scene_view.cpp` | dot/cross/length/inverse 与 View/Projection 公式 | D |
| `runtime/gamescene/component/scene_transform.*` | Quaternion include 与 TRS matrix 构造 | E |
| `runtime/gamescene/component/scene_component.cpp` | normalize、determinant、inverse 与 decomposition | E |
| `runtime/rendercore/geometry/static_mesh.cpp` | 向量逐分量 min/max | C 或 F |
| `runtime/tests/gamescene_tests.cpp` | matrix transform 测试辅助 | 随生产调用方迁移 |
| `engine/runtime/CMakeLists.txt` | Runtime 对 `glm::glm` 的 PUBLIC 链接 | G |

`Toy3dMathTests` 在批次 A 允许用 GLM aliases 复现当前基准；引入 Toy3d 自有类型后，测试改为公共 API golden tests，GLM 对照代码只保留在明确标注的迁移测试中。

### 批次 B：标量、角度与基础检查

- 增加 constants、scalar、`Radians`、`Degrees`；
- 删除可变 `AngleUnit`、模糊 `Angle` 和量纲错误运算；
- 增加统一 finite/nearly-equal/normalize checked API；
- 迁移散落的 `is_finite` 与简单 GLM helpers。

当前状态：已实现 constants、scalar、`Radians` 与 `Degrees`，并删除旧 `AngleUnit`、`Angle`、`Degree`、`Radian` 和 `Math`。标量 finite/nearly-equal 已进入公共 API；Vector normalize/finite 需等待批次 C 的自有 Vector 类型，避免为 GLM aliases 新增长期 overload。Runtime 尚在使用的 `DEG2RAD`、`EPSILON` 等名称暂时映射到新 `k_*` 常量，分别在批次 D、E 迁移调用方后删除。

### 批次 C：Toy3d Vector、Quaternion 与 Matrix

- 引入自有公共类型和稳定 scalar layout；
- 在 detail 中实现 GLM 转换；
- 添加布局、运算、inverse 与 Shader golden tests；
- 新代码停止使用 `vec*`、`mat*`、`quat` aliases。

此批次按调用链纵向迁移，不能只创建新类型而不迁移任何真实调用方。

当前状态：C1 已实现 `Vector2`、`Vector3`、`Vector4` 与 `UIntVector2/3/4`，包括固定 scalar layout、逐分量/标量运算、dot/cross、距离、finite、近似比较和三种 normalize 语义。C2 已实现 `Matrix3`、`Matrix4`，包括 column-major scalar storage、矩阵/向量乘法、transpose、determinant、checked inverse 和 position/vector/normal 变换。C3 已实现固定 `x, y, z, w` 布局的 `Quaternion`、axis-angle、rotation-matrix、direction-to-direction、normalize/inverse、rotate/unrotate、matrix conversion 与 slerp；Euler 顺序尚未单独设计，因此没有提前公开 Euler API。`Toy3dMathTests` 已迁移全部基础类型 golden values。旧 GLM aliases 仍承载 Runtime 的 `SceneTransform` 等生产调用链，这些调用方随批次 D/E 纵向迁移并删除旧入口；在此之前不宣告批次 C 完成。

### 批次 D：Basis、View 与 Projection

- 实现 basis、rotation-from-forward-up、view、LookAt；
- 实现 finite/infinite perspective；
- 将 `renderscene/view/scene_view.cpp` 的私有公式迁移到 Core Math；
- 保留 SceneView 的 ViewRect、projection mode、组合与 diagnostic 策略。

当前状态：已实现 forward/up rotation、orientation/LookAt view matrix，以及 finite/infinite reversed-Z perspective。正交轴保持为 Quaternion 构造的实现细节，不公开独立 basis 类型；View 与 Projection 的公共构造统一由 `matrix_construction.h` 提供。有限投影的真实调用链已纵向接入 `SceneView`：ViewRect/aspect 与 diagnostic 策略仍由 RenderScene 持有，矩阵构造、checked inverse 和 Toy3d 值类型由 Core Math 提供；该调用链不再直接包含或调用 GLM。

### 批次 E：共享 Transform

- 引入 Core `Transform` 与 decomposition；
- 迁移 `SceneTransform`、SceneComponent 和 Camera axes；
- 明确 hierarchy shear 与 positive-scale 既有策略；
- 删除 GameScene 内重复 TRS 构造和直接 GLM includes。

当前状态：已实现 Core `Transform`、TRS matrix、checked decomposition、position/vector/direction 与 inverse 变换。GameScene 已删除 `SceneTransform` 和私有 GLM decomposition/TRS 构造；`SceneComponent` 继续保存 world `Matrix4` 以保留 hierarchy shear，同时单独组合 `world_rotation`，使 Camera axes 不受 local/parent scale 影响。`KeepWorld` 仅在相对矩阵可表示为 positive-scale TRS 时提交 attachment 变更，shear 或不可逆 parent 会原子失败。Primitive/Light render snapshot 的 world matrix 已同步迁移到 `Matrix4`；GameScene 生产代码不再直接包含或调用 GLM。

### 批次 F：Geometry 与 Editor 接入

- 按 selection ray、gizmo、culling 的真实调用顺序迁移 Ray、Plane、AABB、Frustum；
- 接入 Editor orbit/fly camera 和 object LookAt policy；
- 迁移 `rendercore/geometry` 中已成为跨模块公共值的类型；
- 不下沉 Editor/Game/Render policy。

### 批次 G：关闭 GLM 公共边界

- 删除旧 aliases 与已替代的 `Math` 入口；
- 将 `glm::glm` 收敛为 `Toy3dMath` 的 `PRIVATE` 依赖；
- Runtime、Editor、Tools、RHI 不再直接链接或包含 GLM；
- 增加仓库检查，禁止允许目录外出现 `#include <glm/...>`、`#include "glm/..."` 或 `glm::`。

## 19. 旧实现删除条件

满足以下条件后删除 `vec2/vec3/vec4/uvec*/mat*/quat/color` aliases：

- 第一方源码调用已迁移到 Toy3d 类型；
- RHI 描述、Shader upload、GameScene、RenderScene、Editor 和 Tools 已通过对应测试；
- 没有公共头文件暴露 GLM；
- CMake 中除 `Toy3dMath` 外没有第一方 target 依赖 `glm::glm`；
- 仓库静态搜索仅在 `engine/core/math/detail/`、实现文件、测试对照代码和 thirdparty 中命中 GLM；
- 完整 CTest 与 Editor 构建通过。

满足以下条件后删除 SceneView 私有 view/projection 构造：

- Core Math golden tests 覆盖现有矩阵结果；
- SceneView 使用 Core checked API；
- finite perspective、invalid input 和 inverse matrices 集成测试通过；
- 不存在新旧两个正式投影入口。

满足以下条件后删除 `SceneTransform`：

- Core `Transform` 表达当前 translation、Quaternion rotation、positive scale contract；
- attachment、KeepWorld、hierarchy dirty 与 Camera 测试全部迁移通过；
- Render update snapshot 与 bounds 结果不变；
- GameScene 不再直接包含 GLM。

## 20. 验收标准

Core Math 第一阶段完成的最低标准是：

- `Toy3dMath` 有独立、可执行的测试目标；
- 坐标、矩阵、角度、容差、错误与布局 contract 均有测试；
- LookAt、Camera orientation、View matrix 和 projection 的职责不混淆；
- SceneView 不再自行实现共享矩阵公式；
- GameScene、Editor、RenderScene 和 RHI 使用同一套 Toy3d 数学类型；
- GLM 不出现在第一方上层公共接口；
- reversed-Z 与 Vulkan Y 修正没有第二套竞争实现；
- 每个旧入口都有明确删除条件，迁移完成后不存在长期 aliases 或 bridge。
