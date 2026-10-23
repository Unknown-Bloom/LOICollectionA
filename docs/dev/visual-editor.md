# 表单可视化编辑器

LCUI Studio 是随文档站点一同发布的 `.lcui` 表单可视化编辑器，纯静态实现，手机与电脑浏览器均可直接使用：[打开编辑器](/LOICollectionA/lcui-studio/index.html)。

## 能力范围

- 三类表单（`CustomForm` / `MessageBox` / `PaginatedForm`）与全部原生控件（`label` / `header` / `textField` / `dropdown` / `toggle` / `slider` / `button` / `divider` / `spacer` / 分页按钮 / 消息框按钮）
- 所见即所得画布按基岩版表单观感预览，属性检查器区分「直接参数」与 `*Options` 选项字段
- 值类控件自动生成 `ObservableString` / `ObservableBoolean` / `ObservableNumber` 辅助变量，下拉选项生成 `DropdownItem` 序列，与手写脚本的惯用法一致
- 撤销 / 重做、实时校验、自动保存、一键导出 `.lcui`

## 导入约定

编辑器只回读自身生成的脚本结构：顶层 `let` 辅助声明 + 单个表单声明式块 + 空回调占位 + `show();` 收尾。手写脚本（含 `import`、`tr()`、控制流等）会被拒绝并给出带行号的提示——导出的文件是继续手写的起点，而非逆向工程手写脚本的入口。

## 产物部署

导出的 `.lcui` 文件放入服务端 `plugins/LOICollectionA/gui/` 即可被 `GUIManager` 加载；执行 `/xxx reload` 后生效。按钮 `on:` 回调体需在导出后补充，编辑器只负责结构与外观。
