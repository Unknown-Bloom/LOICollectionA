# LCUI Studio

LOICollectionA `.lcui` 表单可视化编辑器，纯静态 Web 应用，手机与电脑浏览器均可直接使用，无需构建。

## 功能

- 五类表单：`CustomForm` / `MessageBox` / `PaginatedForm` / `MenuForm` / `ShopForm`，组件目录与原生 UI 一一对应（`label` / `header` / `textField` / `dropdown` / `toggle` / `slider` / `button` / `divider` / `spacer` / 分页按钮 / 消息框按钮 / 菜单按钮 / 商品条目）
- 菜单表单：按钮生成 `MenuItemData`，可配置权限、执行命令与 `ScoreRequirement`；商店表单：由 `ShopData` 与 `ShopItemData` 驱动，画布按每页 10 条预览，导出 `shop.lcui`
- 所见即所得画布：Minecraft 基岩版表单风格预览，点击选中、拖拽重排（桌面）
- 属性检查器：直接参数与 `*Options` 选项字段分组编辑，值组件自动生成 `Observable*` 辅助变量
- **拼图式回调编辑器**：为按钮 `on:` 回调与表单 `show(func(result))` 回调图形化编写逻辑（表单导航 / 数据交互 / 控制流 / 运算 / 变量）
- 撤销 / 重做（输入合并）、实时校验、localStorage 自动保存
- 源码面板：实时生成 `.lcui` 声明式脚本（含回调体），一键复制 / 下载
- 导入：粘贴或上传 `.lcui`，仅接受编辑器自身生成的结构，外来脚本给出带行号的错误提示

## 使用

文档站点左侧栏 **LCUI Studio** 分组可直接进入；或部署为静态资源后经 `/lcui-studio/` 访问。本地预览：

```sh
cd lcui-studio
python3 -m http.server 8080
```

构建文档时会由 `scripts/copy-studio.mjs` 自动将本目录同步到 `docs/public/lcui-studio/`。

## 生成的脚本形态

```lcui
let form_toggle_1_value = new ObservableBoolean(false, true);

let form = new CustomForm("example.main", "示例表单") {
    label("一段说明", new TextOptions());
    toggle("开关", form_toggle_1_value, new ToggleOptions());
    button("确定", on: func () -> void {
        this.close();
    }, new ButtonOptions());
    show();
};
```

导出的文件放入服务端 `plugins/LOICollectionA/gui/` 即可被 `GUIManager` 加载；`menu.lcui` 与 `shop.lcui` 需要放入 `plugins/LOICollectionA/config/`。

## 文件结构

```
index.html          入口
css/style.css       响应式布局与 MC 风格画布、拼图编辑器样式
js/schema.js        组件与属性目录（对齐原生 UI API）
js/model.js         文档模型、校验、撤销重做、回调存储
js/codegen.js       .lcui 发射器与导入解析器（含回调体往返）
js/blocks.js        拼图块定义、树操作与 .lcui 生成
js/blockview.js     拼图交互引擎（调色板 / 拖拽吸附 / 选中操作）
js/ui.js            界面渲染与交互
```
