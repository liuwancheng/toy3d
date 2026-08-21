# Memory record format

Write records in Chinese. Keep code identifiers, paths, commands, API names, and necessary technical terms unchanged.

```markdown
# <简短主题>

- 归档时间：YYYY-MM-DD HH:mm（Asia/Shanghai）
- 状态：已完成 | 进行中 | 阻塞 | 仅讨论
- 历史记录：`.codex/memory/archive/YYYY-MM-DD-HHmm-<topic>.md`

## 目标

<本次对话要解决的问题与边界。>

## 已确认

- <事实、约束、实际改动或验证结果。>

## 决策

- <决定及简短原因；没有则写“无”。>

## 待处理

- <未完成项、风险或需要用户决定的事项；没有则写“无”。>

## 下一步

<新对话中最合适的第一步，包含必要路径或命令。>
```

`已确认` 中的构建或测试记录必须包含实际命令和结果。路径只列与恢复工作直接相关的文件。若仓库状态可能在归档后变化，在相关条目中明确提醒重新验证。
