# LCUI Studio

LOICollectionA `.lcui` 表单可视化编辑器，纯静态 Web 应用，手机与电脑浏览器均可直接使用，无需构建。

## 功能

- 三类表单：`CustomForm` / `MessageBox` / `PaginatedForm`，组件目录与原生 UI 一一对应（`label` / `header` / `textField` / `dropdown` / `toggle` / `slider` / `button` / `divider` / `spacer` / 分页按钮 / 消息框按钮）
- 所见即所得画布：Minecraft 基岩版表单风格预览，点击选中、拖拽重排（桌面）
- 属性检查器：直接参数与 `*Options` 选项字段分组编辑，值组件自动生成 `Observable*` 辅助变量
- 撤销 / 重做（输入合并）、实时校验、localStorage 自动保存
- 源码面板：实时生成 `.lcui` 声明式脚本，一键复制 / 下载
- 导入：粘贴或上传 `.lcui`，仅接受编辑器自身生成的结构，外来脚本给出带行号的错误提示

## 使用

直接用浏览器打开 `index.html`，或部署到任意静态服务器：

```sh
cd lcui-studio
python3 -m http.server 8080
```

## 生成的脚本形态

```lcui
let form_toggle_1_value = new ObservableBoolean(false, true);

let form = new CustomForm("example.main", "示例表单") {
    label("一段说明", new TextOptions());
    toggle("开关", form_toggle_1_value, new ToggleOptions());
    button("确定", on: func () -> void {
    }, new ButtonOptions());
    show();
};
```

导出的文件放入服务端 `plugins/LOICollectionA/gui/` 即可被 `GUIManager` 加载。

## 文件结构

```
index.html      入口
css/style.css   响应式布局与 MC 风格画布
js/schema.js    组件与属性目录（对齐原生 UI API）
js/model.js     文档模型、校验、撤销重做
js/codegen.js   .lcui 发射器与导入解析器
js/ui.js        界面渲染与交互
```

回调逻辑（按钮 `on:` 内的代码体）需在导出后于脚本中补充，编辑器仅生成空占位。
