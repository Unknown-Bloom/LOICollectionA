var LCUI = globalThis.LCUI = globalThis.LCUI || {};

LCUI.Schema = (function () {
    'use strict';

    function text(key, label, initial, required) {
        return { key: key, label: label, type: 'text', initial: initial, required: !!required, argument: true };
    }

    function number(key, label, initial, min, max, step, required) {
        return { key: key, label: label, type: 'number', initial: initial, min: min, max: max, step: step, required: !!required, argument: true };
    }

    function items(key, label) {
        return { key: key, label: label, type: 'items', initial: '', required: false, argument: true };
    }

    function flag(key, label) {
        return { key: key, label: label, type: 'boolean', initial: false, required: false, argument: false };
    }

    function optionText(key, label, initial, required) {
        return { key: key, label: label, type: 'text', initial: initial === undefined ? '' : initial, required: !!required, argument: false };
    }

    function optionNumber(key, label, initial, min, max, step) {
        return { key: key, label: label, type: 'number', initial: initial, min: min, max: max, step: step, required: false, argument: false };
    }

    function optionItems(key, label) {
        return { key: key, label: label, type: 'items', initial: '', required: false, argument: false };
    }

    function scores(key, label) {
        return { key: key, label: label, type: 'scores', initial: '', required: false, argument: false };
    }

    var controls = [
        { kind: 'Label', name: 'label', label: '标签', category: '基础', optionsClass: 'TextOptions', observable: null, hasCallback: false, unique: false, properties: [
            text('text', '文本', '', true), flag('visible', '可见')
        ] },
        { kind: 'Header', name: 'header', label: '节标题', category: '基础', optionsClass: 'TextOptions', observable: null, hasCallback: false, unique: false, properties: [
            text('text', '文本', '', true), flag('visible', '可见')
        ] },
        { kind: 'TextField', name: 'textField', label: '文本输入', category: '输入', optionsClass: 'TextFieldOptions', observable: 'String', observableKey: 'text', hasCallback: false, unique: false, properties: [
            text('label', '标题', '', true), text('text', '默认内容', '', false),
            optionText('description', '描述'), flag('disabled', '禁用'), flag('visible', '可见')
        ] },
        { kind: 'Dropdown', name: 'dropdown', label: '下拉选择', category: '输入', optionsClass: 'DropdownOptions', observable: 'Number', observableKey: 'value', hasCallback: false, unique: false, properties: [
            text('label', '标题', '', true), number('value', '选中项索引', 0, 0, 63, 1, false), items('items', '选项列表'),
            optionText('description', '描述'), flag('disabled', '禁用'), flag('visible', '可见')
        ] },
        { kind: 'Toggle', name: 'toggle', label: '开关', category: '输入', optionsClass: 'ToggleOptions', observable: 'Boolean', observableKey: 'toggled', hasCallback: false, unique: false, properties: [
            text('label', '标题', '', true), { key: 'toggled', label: '默认开启', type: 'boolean', initial: false, required: false, argument: true },
            optionText('description', '描述'), flag('disabled', '禁用'), flag('visible', '可见')
        ] },
        { kind: 'Slider', name: 'slider', label: '滑块', category: '输入', optionsClass: 'SliderOptions', observable: 'Number', observableKey: 'value', hasCallback: false, unique: false, properties: [
            text('label', '标题', '', true), number('value', '默认值', 0, -1e9, 1e9, 1, false),
            number('min', '最小值', 0, -1e9, 1e9, 1, true), number('max', '最大值', 100, -1e9, 1e9, 1, true),
            optionNumber('step', '步长', 1, 0.01, 1e6, 0.01), optionText('description', '描述'), flag('disabled', '禁用'), flag('visible', '可见')
        ] },
        { kind: 'Button', name: 'button', label: '按钮', category: '基础', optionsClass: 'ButtonOptions', observable: null, hasCallback: true, unique: false, properties: [
            text('text', '文本', '', true), optionText('tooltip', '提示文本'), flag('disabled', '禁用'), flag('visible', '可见')
        ] },
        { kind: 'MenuButton', name: 'button', label: '菜单按钮', category: '菜单', optionsClass: 'ButtonOptions', observable: null, hasCallback: true, unique: false, properties: [
            text('text', '文本', '', true), optionText('actionId', '动作标识'), optionText('actionType', '动作类型', 'button'),
            optionNumber('permission', '权限等级', 0, 0, 4, 1), optionItems('run', '执行命令'), scores('scores', 'Score 需求'),
            optionText('tooltip', '提示文本'), flag('disabled', '禁用'), flag('visible', '可见')
        ] },
        { kind: 'ShopItem', name: 'item', label: '商品条目', category: '商店', optionsClass: null, observable: null, hasCallback: false, unique: false, properties: [
            optionText('type', '类型', 'commodity'), optionText('title', '名称', '', true), optionText('id', '物品 / 目标 ID'),
            optionText('introduce', '介绍'), optionText('number', '数量提示'), optionText('nbt', 'NBT'),
            optionText('confirmButton', '确认按钮'), optionText('cancelButton', '取消按钮'),
            optionNumber('time', '限时（秒）', 0, 0, 1e9, 1), scores('scores', 'Score 需求')
        ] },
        { kind: 'Divider', name: 'divider', label: '分隔线', category: '基础', optionsClass: 'DividerOptions', observable: null, hasCallback: false, unique: false, properties: [
            flag('visible', '可见')
        ] },
        { kind: 'Spacer', name: 'spacer', label: '占位空白', category: '基础', optionsClass: 'SpacingOptions', observable: null, hasCallback: false, unique: false, properties: [
            flag('visible', '可见')
        ] },
        { kind: 'Body', name: 'body', label: '正文', category: '消息框', optionsClass: null, observable: null, hasCallback: false, unique: true, properties: [
            text('text', '正文文本', '', true)
        ] },
        { kind: 'Button1', name: 'button1', label: '按钮一', category: '消息框', optionsClass: null, observable: null, hasCallback: false, unique: true, properties: [
            text('text', '文本', '', true), text('tooltip', '提示文本', '', false)
        ] },
        { kind: 'Button2', name: 'button2', label: '按钮二', category: '消息框', optionsClass: null, observable: null, hasCallback: false, unique: true, properties: [
            text('text', '文本', '', true), text('tooltip', '提示文本', '', false)
        ] },
        { kind: 'PreviousButton', name: 'previousButton', label: '上一页按钮', category: '分页', optionsClass: null, observable: null, hasCallback: false, unique: true, properties: [
            text('text', '文本', '上一页', true)
        ] },
        { kind: 'NextButton', name: 'nextButton', label: '下一页按钮', category: '分页', optionsClass: null, observable: null, hasCallback: false, unique: true, properties: [
            text('text', '文本', '下一页', true)
        ] },
        { kind: 'ChooseButton', name: 'chooseButton', label: '跳页按钮', category: '分页', optionsClass: null, observable: null, hasCallback: false, unique: true, properties: [
            text('text', '文本', '跳转', true), text('prompt', '输入提示', '', false)
        ] },
        { kind: 'CloseButton', name: 'closeButton', label: '关闭按钮', category: '分页', optionsClass: null, observable: null, hasCallback: false, unique: true, properties: [] }
    ];

    var genericControls = ['Label', 'Header', 'TextField', 'Dropdown', 'Toggle', 'Slider', 'Button', 'Divider', 'Spacer', 'CloseButton'];
    var paginatedControls = ['Label', 'Header', 'TextField', 'Dropdown', 'Toggle', 'Slider', 'Button', 'Divider', 'Spacer', 'PreviousButton', 'NextButton', 'ChooseButton', 'CloseButton'];
    var menuControls = ['Label', 'Header', 'TextField', 'Dropdown', 'Toggle', 'Slider', 'Button', 'MenuButton', 'Divider', 'Spacer', 'CloseButton'];
    var shopControls = ['ShopItem'];

    var forms = [
        { kind: 'Custom', name: 'CustomForm', label: '自定义表单', hint: '逐行排布的通用表单，适合设置页与数据提交。', controls: genericControls, properties: [
            text('id', '表单标识', '', true), text('title', '表单标题', '', true), text('variable', '脚本变量名', 'form', true)
        ] },
        { kind: 'MessageBox', name: 'MessageBox', label: '消息框', hint: '正文加两个按钮的确认对话框。', controls: ['Body', 'Button1', 'Button2'], properties: [
            text('id', '表单标识', '', true), text('title', '表单标题', '', true), text('variable', '脚本变量名', 'form', true)
        ] },
        { kind: 'Paginated', name: 'PaginatedForm', label: '分页列表', hint: '按页展示条目，附带翻页与跳页按钮。', controls: paginatedControls, properties: [
            text('id', '表单标识', '', true), text('title', '表单标题', '', true), text('variable', '脚本变量名', 'form', true),
            items('pages', '条目列表'), number('pageSize', '每页条数', 4, 1, 64, 1, true)
        ] },
        { kind: 'Menu', name: 'MenuForm', label: '菜单表单', hint: '菜单按钮可配置权限与 Score，生成 MenuForm 业务表单。', controls: menuControls, file: 'menu.lcui', properties: [
            text('id', '表单标识', '', true), text('title', '表单标题', '', true), text('variable', '脚本变量名', 'form', true)
        ] },
        { kind: 'Shop', name: 'ShopForm', label: '商店表单', hint: '由商品条目驱动的 ShopForm，生成可直接部署的商店脚本。', controls: shopControls, file: 'shop.lcui', properties: [
            text('id', '表单标识', '', true), text('title', '表单标题', '', true), text('variable', '脚本变量名', 'form', true),
            optionText('type', '商店类型', 'buy'), optionText('content', '商店介绍'),
            optionText('exitCommand', '退出命令'), optionText('scoreCommand', '分数不足命令'),
            optionText('titleCommand', '称号购买命令'), optionText('itemCommand', '物品购买命令')
        ] }
    ];

    var controlIndex = controls.reduce(function (map, entry) { map[entry.kind] = entry; return map; }, {});
    var formIndex = forms.reduce(function (map, entry) { map[entry.kind] = entry; return map; }, {});

    function findControl(kind) { return controlIndex[kind] || null; }
    function findControlByName(name, formKind) {
        return controls.filter(function (entry) {
            return entry.name === name && (!formKind || formAcceptsControl(formKind, entry.kind));
        })[0] || null;
    }
    function findForm(kind) { return formIndex[kind] || null; }
    function findFormByName(name) { return forms.filter(function (entry) { return entry.name === name; })[0] || null; }

    function formAcceptsControl(formKind, controlKind) {
        var form = findForm(formKind);
        return !!form && form.controls.indexOf(controlKind) !== -1;
    }

    function propertyOf(controlKind, key) {
        var control = findControl(controlKind);
        if (!control) return null;
        return control.properties.filter(function (entry) { return entry.key === key; })[0] || null;
    }

    function formPropertyOf(formKind, key) {
        var form = findForm(formKind);
        if (!form) return null;
        return form.properties.filter(function (entry) { return entry.key === key; })[0] || null;
    }

    function initialValue(descriptor) {
        return descriptor.initial;
    }

    return {
        forms: forms,
        controls: controls,
        findControl: findControl,
        findControlByName: findControlByName,
        findForm: findForm,
        findFormByName: findFormByName,
        formAcceptsControl: formAcceptsControl,
        propertyOf: propertyOf,
        formPropertyOf: formPropertyOf,
        initialValue: initialValue
    };
})();
