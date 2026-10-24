# 表单可视化编辑器

LCUI Studio 是随文档站点一同发布的 `.lcui` 表单可视化编辑器，纯静态实现，手机与电脑浏览器均可直接使用。除本页底部的链接外，站点左侧栏的 **LCUI Studio** 分组也提供了直达入口：[打开编辑器](https://loitietu.github.io/LOICollectionA/lcui-studio/)。

## 能力范围

- 三类表单（`CustomForm` / `MessageBox` / `PaginatedForm`）与全部原生控件（`label` / `header` / `textField` / `dropdown` / `toggle` / `slider` / `button` / `divider` / `spacer` / 分页按钮 / 消息框按钮）
- 所见即所得画布按基岩版表单观感预览，属性检查器区分「直接参数」与 `*Options` 选项字段
- 值类控件自动生成 `ObservableString` / `ObservableBoolean` / `ObservableNumber` 辅助变量，下拉选项生成 `DropdownItem` 序列，与手写脚本的惯用法一致
- 撤销 / 重做、实时校验、自动保存、一键导出 `.lcui`
- **拼图式回调编辑器**：为按钮 `on:` 回调与表单 `show(func(result))` 回调编写逻辑，无需导出后手写

## 拼图逻辑编辑器

选中带回调的控件（如按钮）或表单后，在属性面板点击「编辑 on 回调 / 编辑 show 回调」，即可打开积木工作区：

- 左侧积木库按「表单导航 / 数据交互 / 控制流 / 运算 / 变量」分类，点击或拖拽到右侧画布
- 语句块首尾相接构成执行序列；`如果 / 当…重复 / 计数循环 / 遍历循环` 等 C 形块内嵌子语句；表达式槽（圆角框）接受运算类积木
- 单击积木选中后可删除、复制、上下移、缩进 / 取消缩进；移动端以点击添加与按钮兜底（拖拽同样可用）
- 所有改动实时同步到 `.lcui` 源码预览，并纳入撤销 / 重做与自动保存

## 导入约定

编辑器可回读自身生成的脚本结构：顶层 `let` 辅助声明 + 单个表单声明式块 + 由拼图生成的回调体 + `show(func(result) -> void { … })` 收尾。手写脚本（含 `import`、`tr()`、未被编辑器支持的语法）会被拒绝并给出带行号的提示——导出的文件仍是继续手写的起点。

## 产物部署

导出的 `.lcui` 文件放入服务端 `plugins/LOICollectionA/gui/` 即可被 `GUIManager` 加载；执行 `/xxx reload` 后生效。
