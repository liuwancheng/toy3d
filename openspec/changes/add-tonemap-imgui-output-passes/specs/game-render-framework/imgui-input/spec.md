## Purpose

定义主窗口平台事件、Runtime Input 与 Dear ImGui IO 之间的投递和消费顺序，使 ImGui 获得鼠标、键盘、文本、焦点及 DPI 信息，同时避免重复输入系统、卡键、错误 capture 和平台或图形 backend 耦合。

## Type Contracts

| 类型 | 性质与职责 | 所有权与线程 | 错误语义与复用理由 |
| --- | --- | --- | --- |
| `TextInputEvent` | 新增 `InputEvent` value；保存一个已验证Unicode code point，不等同物理Key事件 | platform在GT/window事件线程构造，Input/ImGui同步只读消费 | 无效Unicode scalar拒绝并诊断；不能复用`KeyEvent`，因为文本输入受布局、IME和组合影响 |
| `WindowFocusEvent` | 新增 `InputEvent` value；保存主窗口获得或失去输入焦点的事实 | platform在GT/window事件线程构造，Input/ImGui同步消费 | focus丢失必须清理瞬时输入，不能用某个KeyReleased或Window resize替代 |

## ADDED Requirements

### Requirement: 平台事件先更新设备状态再执行 capture policy
主窗口输入事件 MUST先更新Runtime Input设备的物理状态，再投递ImGui IO；随后依据事件类别和ImGui capture意图决定是否执行gameplay binding callbacks。UI capture MUST只抑制业务映射，不得阻止release/focus事件更新设备状态，否则会产生卡键或卡住鼠标按钮。

#### Scenario: UI 捕获按键释放
- **WHEN** gameplay先观察到按键按下，随后UI开始捕获键盘并收到同一按键释放
- **THEN** KeyboardDevice MUST更新为released，但该release对应的gameplay action callback MUST按capture policy被抑制

### Requirement: 鼠标键盘与文本 capture 分类别
鼠标移动、按钮和滚轮 MUST依据mouse capture处理；物理键与modifier MUST依据keyboard capture处理；Unicode文本输入 MUST投递ImGui text input并依据text-input意图处理。系统 MUST不以单个全局布尔值吞掉所有输入类别。

#### Scenario: UI 只捕获鼠标
- **WHEN** ImGui请求mouse capture但不请求keyboard capture
- **THEN** UI窗口内鼠标业务映射 MUST被抑制，而未被UI使用的keyboard gameplay mapping MUST继续工作

#### Scenario: 文本输入
- **WHEN** 平台产生一个有效Unicode文本code point
- **THEN** ImGui IO MUST接收文本事件，Runtime物理Key映射不得把该code point伪装成KeyCode

### Requirement: Focus 丢失清除瞬时状态
主窗口失去focus时，ImGui IO与Runtime Input device MUST清除按下键、鼠标按钮、modifier及需要focus维持的瞬时状态；重新获得focus不得恢复失焦前的pressed状态。

#### Scenario: 按键按住时切走窗口
- **WHEN** key处于pressed/hold状态时主窗口失去focus且平台不再发送对应release
- **THEN** 下一次Input update与ImGui frame MUST视该key为released，gameplay Hold不得继续触发

### Requirement: Display 与 framebuffer 尺度显式提供
每个ImGui frame MUST从主窗口取得逻辑display size、framebuffer size或等价scale，并形成有限正值的 `DisplaySize` 与 `DisplayFramebufferScale`。resize、DPI变化和最小化 MUST在下一有效frame反映；零extent时不得生成可提交UI frame。

#### Scenario: DPI 在窗口移动后变化
- **WHEN** 主窗口移动到不同DPI显示器且framebuffer scale改变
- **THEN** 后续ImGui draw payload MUST携带新scale，clip/scissor与字体/UI尺寸策略不得继续使用陈旧值

#### Scenario: 窗口最小化
- **WHEN** 主窗口逻辑尺寸或framebuffer extent为零
- **THEN** UI system MUST不生成可渲染payload，Renderer MUST沿既有viewport `NotReady`策略跳过frame

### Requirement: 输入桥不依赖图形 backend
平台适配 MUST只产生Runtime Input/UI可理解的窗口与输入语义，不得访问RHIDevice、swapchain或图形API类型。ImGui输入桥 MUST不调用Vulkan、D3D11或D3D12 backend，也不得另建与InputSystem并行的设备状态和业务binding系统。

#### Scenario: 切换图形 backend
- **WHEN** 同一平台从Vulkan切换到D3D11或D3D12
- **THEN** ImGui input事件、capture和focus行为 MUST保持不变，不得选择另一套平台输入路径

### Requirement: 未完成的平台适配不得伪装完整交互
构建目标若尚不能提供文本、focus、鼠标、键盘和scale的最低事件集合，启用交互式ImGui MUST在初始化阶段返回可诊断 `Unsupported` 或由配置明确禁用；不得只渲染UI却静默宣称交互能力完整。

#### Scenario: 平台缺少文本输入
- **WHEN** 当前平台适配不能产生Unicode文本事件而配置要求交互式ImGui
- **THEN** 初始化 MUST明确报告该能力缺失，不能让输入框无诊断地丢弃字符
