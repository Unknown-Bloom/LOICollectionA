# LCUI Studio

LOICollectionA `.lcui` 可视化编辑器，支持表单、枚举、组件三类文档与多文档项目，纯静态 Web 应用，手机与电脑浏览器均可直接使用，无需构建。

## 功能

- **项目化编辑**：一个项目可包含多个文档（表单 / 枚举 / 组件），顶部 `项目` 或底栏 `项目` 打开项目面板；文档卡片显示类型、文件名、校验状态，可新建、切换、删除
- **表单**：`CustomForm` / `MessageBox` / `PaginatedForm` / `MenuForm` / `ShopForm`，组件目录与原生 UI 一一对应（`label` / `header` / `textField` / `dropdown` / `toggle` / `slider` / `button` / `divider` / `spacer` / 分页按钮 / 消息框按钮 / 菜单按钮 / 商品条目）
- **枚举**：编辑枚举名与变体列表，每行 `Variant` 或 `Variant(int, string)`，支持任意可用类型；同名变体、重名声明会在项目校验中报错
- **组件**：编辑组件名与参数列表，组件内可直接放置控件；控件参数支持 `$param` 引用组件参数、`=表达式` 原样输出、普通文本；`on:` 回调可绑定到组件参数（如 `onConfirm`），生成 `on: onConfirm`
- **组件调用**：表单或组件中插入 `组件调用`，选择目标组件并填写参数（`$param` / `=表达式` / 文本）；导出时自动补上目标文件的 `import`，并校验参数个数
- **导入图**：项目面板按依赖分层绘制文档节点，实线为组件调用、虚线为显式导入；循环依赖以红色边与箭头标出，并列出 `A → B → A` 路径
- **项目校验**：重复文件名、重复组件/枚举名、跨枚举重名变体、缺失的调用目标、参数数量不符、组件自调用、导入可执行表单、变量名冲突、循环依赖一次性汇总
- **多文件导入**：可一次选择多个 `.lcui`，或粘贴源码；每个文件解析为一个文档，自动去重文件名并做项目校验
- **ZIP 导出**：把项目内全部文档按 `properties.file` 打包为存储型 ZIP，文件名即导入路径，解压到目标目录即可
- 菜单表单：按钮生成 `MenuItemData`，可配置权限、执行命令与 `ScoreRequirement`；商店表单：由 `ShopData` 与 `ShopItemData` 驱动，画布按每页 10 条预览
- 所见即所得画布：Minecraft 基岩版表单风格预览，点击选中、拖拽重排（桌面）
- 属性检查器：直接参数与 `*Options` 选项字段分组编辑，值组件自动生成 `Observable*` 辅助变量
- **拼图式回调编辑器**：为按钮 `on:` 回调与表单 `show(func(result))` 回调图形化编写逻辑（表单导航 / 数据交互 / 控制流 / 运算 / 变量）
- 撤销 / 重做（输入合并）、实时校验、localStorage 自动保存；每个文档独立撤销栈
- 源码面板：实时生成当前文档的 `.lcui` 脚本（含 `import` 头与回调体），一键复制 / 下载
- 导入解析器接受编辑器自身生成的子集：`import`、辅助变量、表单绑定、`enum`、`component` 与组件调用；外来脚本给出带行号的错误提示

## 使用

文档站点左侧栏 **LCUI Studio** 分组可直接进入；或部署为静态资源后经 `/lcui-studio/` 访问。本地预览：

```sh
cd docs/public/lcui-studio
python3 -m http.server 8080
```

## 生成的脚本形态

表单：

```lcui
import "component.lcui";

let main_toggle_1_value = new ObservableBoolean(false, true);

let main = new CustomForm("example.main", "示例表单") {
    label("一段说明", new TextOptions());
    toggle("开关", main_toggle_1_value, new ToggleOptions());
    ConfirmBar("确定", func () -> void {
        main.close();
    });
    show();
};
```

枚举：

```lcui
enum Shape {
    Point(int, int)
    Empty
}
```

组件：

```lcui
component ConfirmBar(confirmText, onConfirm) {
    button(confirmText, on: onConfirm, new ButtonOptions());
}
```

导出的表单文件放入服务端 `plugins/LOICollectionA/gui/` 即可被 `GUIManager` 加载；`menu.lcui` 与 `shop.lcui` 需要放入 `plugins/LOICollectionA/config/`。枚举与组件文件作为定义库，由表单或其他组件通过 `import` 引入。

## 文件结构

```
index.html          入口、项目面板与各类对话框
css/style.css       响应式布局、MC 风格画布、拼图编辑器与项目面板样式
js/schema.js        组件与属性目录（对齐原生 UI API），文档类型元数据
js/model.js         项目/文档模型、校验、导入图、撤销重做、回调存储
js/codegen.js       .lcui 发射器与导入解析器（含回调体、枚举、组件往返）
js/zip.js           存储型 ZIP 打包与下载
js/blocks.js        拼图块定义、树操作与 .lcui 生成
js/blockview.js     拼图交互引擎（调色板 / 拖拽吸附 / 选中操作）
js/ui.js            界面渲染与交互、项目管理、导入导出
```
