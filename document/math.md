# Math：坐标、变换与几何

## 定位与基线

target Toy3dCore，公共头 engine/core/math/math.h 及同目录专项头；测试 engine/core/tests/math_tests.cpp。Shader、场景、导入、Editor、RHI 共用：

| 项 | 固定语义 |
| --- | --- |
| 坐标 | left-handed，+X right、+Y up、+Z forward |
| 单位 | **1 unit = 1 centimeter**，位置/距离/裁剪/灯光范围/阴影距离均厘米 |
| 无量纲 | scale、方向、比例 |
| 矩阵 | column-vector、column-major，HLSL mul(matrix, vector) |
| 复合 | parent * local，右侧先执行 |
| clip depth | 0..1 reversed-Z：near=1、far=0、clear=0.0、默认 GreaterEqual |
| front face | 公共 CCW，Vulkan negative viewport height 并修正 native front face |

Shader 不手写平台翻转；源单位在导入边界显式转换，不在 runtime 隐式乘 100。

## 类型与失败

- Radians/Degrees 显式角度，不把任意 float 当有单位值。
- 矩阵索引 (column,row)；点含平移、方向不含平移；非均匀 scale 法线不能按普通向量变换。
- Quaternion xyzw，q/-q 旋转等价不等于逐分量相等；旋转前满足有限/单位长度约束。
- checked/try 入口验证非有限、退化、奇异；失败不污染输出。unchecked 仅用于已保证的热路径，不能接外部输入。
- tolerance 分长度、角度、矩阵、scale 用途，不用一个 epsilon 覆盖全部。

## Transform、投影与几何

Transform 是 positive-scale TRS；分解拒绝 shear、镜像/负 scale、零 scale、非有限。任意 affine matrix 不保证可无损 TRS，附着 world/local 转换失败应原子保留旧状态。

投影统一用 matrix_construction.h 的 try_make_view_matrix、try_make_look_at_view_matrix、try_make_perspective_projection、try_make_infinite_perspective_projection、try_make_orthographic_projection，不在 Camera/backend 复制公式。

Perspective 默认 fov=π/3 rad、aspect=1、near=10 cm、far=100000 cm；无限远仍有正 near。Orthographic 边界/near/far 是厘米且非退化。Plane 正半空间为 inside，有限 reversed-Z frustum 六平面、无限远五平面，不硬套传统深度公式；AABB 接触判 intersect。

当前 math.h 仍公开部分 GLM aliases，Toy3dCore 传播 glm；这是实现边界未完全封闭，不应继续直接扩散第三方算法或宣称公共层已脱离 GLM。

实际投影调用片段（matrix_construction.h；失败时调用方必须停止接管该 View，不使用未生成的矩阵）：

```cpp
toy3d::PerspectiveProjectionDesc desc;
desc.aspect = 16.0f / 9.0f;
desc.near_clip = 10.0f;       // cm
desc.far_clip = 100000.0f;    // cm
toy3d::Matrix4 projection{};
if (!toy3d::try_make_perspective_projection(desc, projection))
{
    // 报告非法投影候选，保留原 View。
}
```

修改同时核对 CPU/HLSL、Camera、源单位、光空间、frustum、Vulkan viewport。用 math_tests.cpp 的完整用例验证退化输入/输出不变、TRS round-trip、near/far、有限/无限视锥、非均匀 scale 与容差边界。
