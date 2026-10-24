(function () {
    'use strict';

    var Schema = LCUI.Schema;
    var Model = LCUI.Model;
    var Codegen = LCUI.Codegen;

    var Store = {
        key: 'lcui-studio-document-v1',
        session: null,
        selection: null,
        activeTab: 'canvasPanel',
        lastSource: '',
        justAdded: null,
        logicTarget: null,
        logicOpen: false
    };

    var BlockView = LCUI.BlockView;

    var MC_COLORS = {
        '0': '#000000', '1': '#0000aa', '2': '#00aa00', '3': '#00aaaa',
        '4': '#aa0000', '5': '#aa00aa', '6': '#ffaa00', '7': '#aaaaaa',
        '8': '#555555', '9': '#5555ff', 'a': '#55ff55', 'b': '#55ffff',
        'c': '#ff5555', 'd': '#ff55ff', 'e': '#ffff55', 'f': '#ffffff'
    };

    function richText(text) {
        var container = el('span', 'mc-rich');
        var value = text == null ? '' : String(text);
        if (!value) return container;
        var state = { color: null, weight: '', italic: false, line: false, under: false, magic: false };
        var buffer = '';

        function flush() {
            if (!buffer) return;
            var span = document.createElement('span');
            if (state.color) span.style.color = state.color;
            if (state.weight) span.style.fontWeight = state.weight;
            if (state.italic) span.style.fontStyle = 'italic';
            if (state.line && state.under) span.style.textDecoration = 'line-through underline';
            else if (state.line) span.style.textDecoration = 'line-through';
            else if (state.under) span.style.textDecoration = 'underline';
            if (state.magic) span.className = 'mc-magic';
            span.textContent = buffer;
            container.appendChild(span);
            buffer = '';
        }

        for (var i = 0; i < value.length; i++) {
            var ch = value[i];
            if (ch === '§' && i + 1 < value.length) {
                var code = value[i + 1].toLowerCase();
                if (MC_COLORS[code] !== undefined) { flush(); state.color = MC_COLORS[code]; i++; continue; }
                if (code === 'r') { flush(); state = { color: null, weight: '', italic: false, line: false, under: false, magic: false }; i++; continue; }
                if (code === 'l') { flush(); state.weight = '700'; i++; continue; }
                if (code === 'o') { flush(); state.italic = true; i++; continue; }
                if (code === 'm') { flush(); state.line = true; i++; continue; }
                if (code === 'n') { flush(); state.under = true; i++; continue; }
                if (code === 'k') { flush(); state.magic = true; i++; continue; }
            }
            buffer += ch;
        }
        flush();
        return container;
    }

    function richEl(tag, className, text) {
        var node = el(tag, className);
        if (text) node.appendChild(richText(text));
        return node;
    }

    function byId(id) {
        return document.getElementById(id);
    }

    function el(tag, className, text) {
        var node = document.createElement(tag);
        if (className) node.className = className;
        if (text !== undefined && text !== null) node.textContent = text;
        return node;
    }

    function toast(message) {
        var node = byId('toast');
        node.textContent = message;
        node.hidden = false;
        clearTimeout(toast.timer);
        toast.timer = setTimeout(function () { node.hidden = true; }, 2400);
    }

    function documentSummary(control) {
        var properties = control.properties;
        var source = properties.text || properties.label || properties.prompt || '';
        var value = String(source || '').trim();
        return value.length ? value : Schema.findControl(control.kind).label;
    }

    function clampSelection() {
        if (Store.selection !== null) {
            var size = Store.session.document.controls.length;
            if (Store.selection >= size) Store.selection = size ? size - 1 : null;
        }
    }

    function sampleDocument(formKind) {
        var kind = formKind || 'Custom';
        var document = Model.makeDocument(kind);

        if (kind === 'MessageBox') {
            document.properties.id = 'example.confirm';
            document.properties.title = '操作确认';

            var body = Model.makeControl('Body');
            body.properties.text = '确定要执行此操作吗？该操作无法撤销。';
            var first = Model.makeControl('Button1');
            first.properties.text = '确定';
            var second = Model.makeControl('Button2');
            second.properties.text = '取消';
            document.controls = [body, first, second];
            return document;
        }

        if (kind === 'Paginated') {
            document.properties.id = 'example.list';
            document.properties.title = '商品列表';
            document.properties.pages = '苹果\n香蕉\n樱桃\n芒果\n西瓜\n葡萄';

            var previous = Model.makeControl('PreviousButton');
            previous.properties.text = '上一页';
            var next = Model.makeControl('NextButton');
            next.properties.text = '下一页';
            var choose = Model.makeControl('ChooseButton');
            choose.properties.text = '跳转';
            choose.properties.prompt = '页码';
            document.controls = [previous, next, choose, Model.makeControl('CloseButton')];
            return document;
        }

        document.properties.id = 'example.main';
        document.properties.title = '示例表单';

        var controls = [
            Model.makeControl('Label'),
            Model.makeControl('Header'),
            Model.makeControl('TextField'),
            Model.makeControl('Dropdown'),
            Model.makeControl('Toggle'),
            Model.makeControl('Slider'),
            Model.makeControl('Button')
        ];
        controls[0].properties.text = '欢迎使用 LCUI Studio，点击左侧组件即可添加。';
        controls[1].properties.text = '基本信息';
        controls[2].properties.label = '昵称';
        controls[3].properties.label = '服务器';
        controls[3].properties.items = '生存服\n创造服\n模组服';
        controls[4].properties.label = '记住选择';
        controls[4].properties.toggled = true;
        controls[5].properties.label = '音量';
        controls[5].properties.value = 50;
        controls[6].properties.text = '保存设置';
        document.controls = controls;
        return document;
    }

    function defaultDocument(formKind) {
        var document = Model.makeDocument(formKind);
        if (formKind === 'MessageBox') {
            document.properties.id = 'example.confirm';
            document.properties.title = '确认操作';
            document.controls = [Model.makeControl('Body'), Model.makeControl('Button1'), Model.makeControl('Button2')];
            document.controls[0].properties.text = '确定要执行此操作吗？';
            document.controls[1].properties.text = '确定';
            document.controls[2].properties.text = '取消';
        } else if (formKind === 'Paginated') {
            document.properties.id = 'example.list';
            document.properties.title = '条目列表';
            document.properties.pages = '苹果\n香蕉\n樱桃';
            document.controls = [Model.makeControl('PreviousButton'), Model.makeControl('NextButton'), Model.makeControl('ChooseButton'), Model.makeControl('CloseButton')];
            document.controls[0].properties.text = '上一页';
            document.controls[1].properties.text = '下一页';
            document.controls[2].properties.text = '跳转';
        } else {
            document.properties.id = 'example.main';
            document.properties.title = '示例表单';
            document.controls = [Model.makeControl('Label'), Model.makeControl('Button')];
            document.controls[0].properties.text = '欢迎使用 LCUI Studio';
            document.controls[1].properties.text = '确定';
        }
        return document;
    }

    function loadStored() {
        try {
            var raw = window.localStorage.getItem(Store.key);
            if (!raw) return null;
            var data = JSON.parse(raw);
            if (!data || !data.document || !data.document.properties) return null;
            var document = Model.makeDocument(data.document.kind);
            document.properties = data.document.properties;
            document.controls = data.document.controls || [];
            Store.selection = data.selection === undefined ? null : data.selection;
            return document;
        } catch (ignored) {
            return null;
        }
    }

    var storeTimer = null;

    function saveStore() {
        clearTimeout(storeTimer);
        storeTimer = setTimeout(function () {
            try {
                window.localStorage.setItem(Store.key, JSON.stringify({ document: Store.session.document, selection: Store.selection }));
            } catch (ignored) {}
        }, 300);
    }

    function renderStatus() {
        var document = Store.session.document;
        byId('formKindBadge').textContent = Schema.findForm(document.kind).name;
        byId('btnUndo').disabled = !Store.session.canUndo();
        byId('btnRedo').disabled = !Store.session.canRedo();
        byId('canvasTitle').textContent = document.properties.title || '（未命名表单）';
        document.title = (Store.session.isModified() ? '● ' : '') + 'LCUI Studio · .lcui 可视化编辑器';
    }

    function renderPalette() {
        var host = byId('palette');
        host.textContent = '';
        var kinds = ['Label', 'Header', 'TextField', 'Dropdown', 'Toggle', 'Slider', 'Button', 'Divider', 'Spacer', 'Body', 'Button1', 'Button2', 'PreviousButton', 'NextButton', 'ChooseButton', 'CloseButton'];
        var document = Store.session.document;
        var groups = { '基础': [], '输入': [], '操作': [], '分页': [], '消息框': [] };

        kinds.forEach(function (kind) {
            var descriptor = Schema.findControl(kind);
            if (!Schema.formAcceptsControl(document.kind, kind)) return;
            if (descriptor.unique && document.controls.some(function (control) { return control.kind === kind; })) return;
            var group = groups[descriptor.category] || (groups[descriptor.category] = []);
            group.push(descriptor);
        });

        Object.keys(groups).forEach(function (name) {
            if (!groups[name].length) return;
            host.appendChild(el('div', 'palette-group-title', name));
            groups[name].forEach(function (descriptor) {
                var item = el('div', 'palette-item');
                item.setAttribute('data-kind', descriptor.kind);
                item.appendChild(el('span', 'palette-glyph', glyphOf(descriptor.kind)));
                item.appendChild(el('span', 'palette-label', descriptor.label));
                item.addEventListener('click', function () { addControl(descriptor.kind); });
                item.setAttribute('draggable', 'true');
                item.addEventListener('dragstart', function (event) {
                    event.dataTransfer.setData('text/x-lcui-kind', descriptor.kind);
                    event.dataTransfer.effectAllowed = 'copy';
                });
                host.appendChild(item);
            });
        });
    }

    function glyphOf(kind) {
        var glyphs = { Label: 'T', Header: 'H', TextField: '⌨', Dropdown: '▾', Toggle: '◑', Slider: '⇿', Button: '▭', Divider: '—', Spacer: '␣', Body: '¶', Button1: '1', Button2: '2', PreviousButton: '◀', NextButton: '▶', ChooseButton: '↷', CloseButton: '✕' };
        return glyphs[kind] || '▪';
    }

    function addControl(kind) {
        var index = Store.selection === null ? Store.session.document.controls.length : Store.selection + 1;
        if (!Store.session.insertControl(index, kind)) return;
        Store.selection = index;
        Store.justAdded = index;
        renderAll();
        switchTab('canvasPanel');
    }

    function renderOutline() {
        var host = byId('outline');
        host.textContent = '';
        var controls = Store.session.document.controls;

        if (!controls.length) {
            host.appendChild(el('div', 'outline-empty', '暂无组件，点击上方组件库添加'));
            return;
        }

        controls.forEach(function (control, index) {
            var row = el('div', 'outline-row' + (Store.selection === index ? ' selected' : ''));
            var descriptor = Schema.findControl(control.kind);
            row.appendChild(el('span', 'outline-glyph', glyphOf(control.kind)));
            row.appendChild(el('span', 'outline-text', (index + 1) + '. ' + descriptor.label + ' · ' + documentSummary(control)));
            row.addEventListener('click', function () {
                Store.selection = index;
                renderAll();
            });
            host.appendChild(row);
        });
    }

    function renderCanvas() {
        var mock = byId('formMock');
        mock.textContent = '';

        var document = Store.session.document;
        var properties = document.properties;
        var invalid = new Set();
        Model.validate(document).forEach(function (issue) {
            if (issue.type === 'control' && issue.index !== undefined) invalid.add(issue.index);
        });

        var head = el('div', 'mock-head');
        head.appendChild(richEl('span', 'mock-title', properties.title || '未命名'));
        head.appendChild(el('span', 'mock-close', '✕'));
        mock.appendChild(head);

        var body = el('div', 'mock-body');
        if (document.kind === 'MessageBox') {
            document.controls.forEach(function (control, index) { body.appendChild(renderWidget(control, index, invalid)); });
        } else if (document.kind === 'Paginated') {
            document.controls.forEach(function (control, index) {
                if (['PreviousButton', 'NextButton', 'ChooseButton', 'CloseButton'].indexOf(control.kind) !== -1) return;
                body.appendChild(renderWidget(control, index, invalid));
            });
            var pages = Model.splitLines(properties.pages);
            var size = Math.max(1, Number(properties.pageSize) || 1);
            pages.slice(0, size).forEach(function (page, position) {
                var row = el('div', 'mock-page-row');
                row.appendChild(el('span', 'mock-page-bullet', String(position + 1)));
                row.appendChild(richEl('span', 'mock-page-text', page));
                body.appendChild(row);
            });
            if (pages.length > size) body.appendChild(el('div', 'mock-more', '… 共 ' + pages.length + ' 条 / ' + Math.ceil(pages.length / size) + ' 页'));
        } else {
            document.controls.forEach(function (control, index) { body.appendChild(renderWidget(control, index, invalid)); });
        }
        mock.appendChild(body);

        if (document.kind === 'MessageBox') {
            var actions = el('div', 'mock-actions');
            var first = richEl('div', 'mc-button half', button1Text(document));
            var second = richEl('div', 'mc-button half', button2Text(document));
            actions.appendChild(first);
            actions.appendChild(second);
            mock.appendChild(actions);
        } else if (document.kind === 'Paginated') {
            var pager = el('div', 'mock-actions pager');
            var previous = richEl('div', 'mc-button small', footerText(document, 'PreviousButton', '◀ 上一页'));
            var choose = richEl('div', 'mc-button small', footerText(document, 'ChooseButton', '跳转'));
            var next = richEl('div', 'mc-button small', footerText(document, 'NextButton', '下一页 ▶'));
            pager.appendChild(previous);
            pager.appendChild(choose);
            pager.appendChild(next);
            mock.appendChild(pager);
        }

        renderSelectionBar(mock);
    }

    function button1Text(document) {
        var control = document.controls.filter(function (entry) { return entry.kind === 'Button1'; })[0];
        return control ? (control.properties.text || '按钮1') : '按钮1';
    }

    function button2Text(document) {
        var control = document.controls.filter(function (entry) { return entry.kind === 'Button2'; })[0];
        return control ? (control.properties.text || '按钮2') : '按钮2';
    }

    function footerText(document, kind, fallback) {
        var control = document.controls.filter(function (entry) { return entry.kind === kind; })[0];
        return control && control.properties.text ? control.properties.text : fallback;
    }

    function renderWidget(control, index, invalid) {
        var node = el('div', 'widget' + (Store.selection === index ? ' selected' : '') + (invalid.has(index) ? ' invalid' : ''));
        node.setAttribute('data-index', String(index));
        node.setAttribute('draggable', 'true');

        if (Store.justAdded === index) {
            node.classList.add('widget-enter');
            Store.justAdded = null;
        }

        var properties = control.properties;
        var kind = control.kind;

        if (kind === 'Label') node.appendChild(richEl('div', 'mc-label', properties.text || ''));
        else if (kind === 'Header') node.appendChild(richEl('div', 'mc-header', properties.text || ''));
        else if (kind === 'Body') node.appendChild(richEl('div', 'mc-body', properties.text || ''));
        else if (kind === 'Divider') node.appendChild(el('div', 'mc-divider'));
        else if (kind === 'Spacer') node.appendChild(el('div', 'mc-spacer'));
        else if (kind === 'Button') node.appendChild(richEl('div', 'mc-button', properties.text || '按钮'));
        else if (kind === 'Button1' || kind === 'Button2') node.appendChild(richEl('div', 'mc-button half', properties.text || '按钮'));
        else if (kind === 'PreviousButton' || kind === 'NextButton' || kind === 'ChooseButton') node.appendChild(richEl('div', 'mc-button small', properties.text || '按钮'));
        else if (kind === 'CloseButton') node.appendChild(el('div', 'mc-close-chip', '✕ 关闭按钮'));
        else if (kind === 'TextField') {
            node.appendChild(richEl('div', 'mc-field-label', properties.label || ''));
            node.appendChild(richEl('div', 'mc-input', properties.text || ''));
        } else if (kind === 'Dropdown') {
            node.appendChild(richEl('div', 'mc-field-label', properties.label || ''));
            var pick = el('div', 'mc-select');
            var lines = Model.splitLines(properties.items);
            pick.appendChild(richEl('span', 'mc-select-text', lines[Number(properties.value) || 0] || '（无选项）'));
            pick.appendChild(el('span', 'mc-select-caret', '▾'));
            node.appendChild(pick);
        } else if (kind === 'Toggle') {
            var row = el('div', 'mc-toggle');
            row.appendChild(richEl('span', 'mc-toggle-label', properties.label || ''));
            var track = el('span', 'mc-switch' + (properties.toggled ? ' on' : ''));
            track.appendChild(el('span', 'mc-knob'));
            row.appendChild(track);
            node.appendChild(row);
        } else if (kind === 'Slider') {
            node.appendChild(richEl('div', 'mc-field-label', (properties.label || '') + '：' + (properties.value || 0)));
            var slider = el('div', 'mc-slider');
            var min = Number(properties.min) || 0;
            var max = Number(properties.max) || 100;
            var value = Math.min(Math.max(Number(properties.value) || 0, min), max);
            var fill = el('div', 'mc-slider-fill');
            var knob = el('div', 'mc-slider-knob');
            var ratio = max > min ? (value - min) / (max - min) : 0;
            fill.style.width = (ratio * 100).toFixed(1) + '%';
            knob.style.left = (ratio * 100).toFixed(1) + '%';
            slider.appendChild(fill);
            slider.appendChild(knob);
            node.appendChild(slider);
        }

        node.addEventListener('click', function () {
            Store.selection = index;
            renderAll();
        });

        node.addEventListener('dragstart', function (event) {
            event.dataTransfer.setData('text/x-lcui-index', String(index));
            event.dataTransfer.effectAllowed = 'move';
        });

        node.addEventListener('dragover', function (event) {
            if (event.dataTransfer.types.indexOf('text/x-lcui-kind') !== -1 || event.dataTransfer.types.indexOf('text/x-lcui-index') !== -1) {
                event.preventDefault();
                node.classList.add('drop-target');
            }
        });

        node.addEventListener('dragleave', function () { node.classList.remove('drop-target'); });

        node.addEventListener('drop', function (event) {
            event.preventDefault();
            node.classList.remove('drop-target');
            var kind = event.dataTransfer.getData('text/x-lcui-kind');
            var from = event.dataTransfer.getData('text/x-lcui-index');
            var target = Number(node.getAttribute('data-index'));

            if (kind) {
                Store.session.insertControl(target, kind);
                Store.selection = target;
            } else if (from !== '' && from !== null) {
                Store.session.moveControl(Number(from), target);
                Store.selection = Number(from) < target ? target - 1 : target;
            }
            renderAll();
        });

        return node;
    }

    function renderSelectionBar(mock) {
        var controls = Store.session.document.controls;
        var canvas = byId('canvas');
        var existing = canvas.querySelector('.selection-bar');
        if (existing) existing.remove();

        if (Store.selection === null || !controls.length) return;

        var index = Store.selection;
        var bar = el('div', 'selection-bar');
        var up = el('button', 'tool wide', '上移');
        var down = el('button', 'tool wide', '下移');
        var copy = el('button', 'tool wide', '复制');
        var remove = el('button', 'tool wide danger', '删除');
        var descriptor = Schema.findControl(controls[index].kind);

        up.disabled = index === 0;
        down.disabled = index === controls.length - 1;

        up.addEventListener('click', function () {
            Store.session.moveControl(index, index - 1);
            Store.selection = index - 1;
            renderAll();
        });
        down.addEventListener('click', function () {
            Store.session.moveControl(index, index + 1);
            Store.selection = index + 1;
            renderAll();
        });
        copy.addEventListener('click', function () {
            if (Store.session.duplicateControl(index)) {
                Store.selection = index + 1;
                Store.justAdded = index + 1;
            }
            renderAll();
        });
        remove.addEventListener('click', function () {
            Store.session.eraseControl(index);
            clampSelection();
            renderAll();
        });

        bar.appendChild(el('span', 'selection-title', (index + 1) + '/' + controls.length + ' · ' + descriptor.label));
        bar.appendChild(up);
        bar.appendChild(down);
        bar.appendChild(copy);
        bar.appendChild(remove);
        canvas.appendChild(bar);
    }

    function renderInspector() {
        var host = byId('inspector');
        host.textContent = '';

        var document = Store.session.document;
        var form = Schema.findForm(document.kind);

        host.appendChild(el('div', 'inspector-section-title', '表单设置 · ' + form.name));
        form.properties.forEach(function (descriptor) {
            host.appendChild(renderField('form', descriptor, document.properties[descriptor.key]));
        });
        var showBtn = el('button', 'tool wide accent', '编辑 show 回调 ▸');
        showBtn.addEventListener('click', function () { openLogic({ scope: 'document', kind: 'shown' }); });
        host.appendChild(showBtn);

        if (Store.selection === null) {
            host.appendChild(el('div', 'inspector-hint', '在画布中选择一个组件以编辑其属性'));
            return;
        }

        var control = document.controls[Store.selection];
        if (!control) return;
        var descriptor = Schema.findControl(control.kind);

        host.appendChild(el('div', 'inspector-section-title control-title', '组件属性 · ' + descriptor.label));
        if (descriptor.hasCallback) {
            var onBtn = el('button', 'tool wide accent', '编辑 on 回调 ▸');
            onBtn.addEventListener('click', function () { openLogic({ scope: 'control', index: Store.selection, kind: 'on' }); });
            host.appendChild(onBtn);
            host.appendChild(el('div', 'inspector-hint', '在拼图编辑器中为回调编写逻辑。'));
        }
        if (!descriptor.properties.length && !descriptor.hasCallback)
            host.appendChild(el('div', 'inspector-hint', '该组件没有可编辑属性'));

        descriptor.properties.forEach(function (property) {
            host.appendChild(renderField('control', property, control.properties[property.key]));
        });
    }

    function getLogicWorkspace(scope, index, kind, ensure) {
        var document = Store.session.document;
        if (scope === 'document') {
            if (ensure) document.callbacks = document.callbacks || {};
            return document.callbacks ? (document.callbacks[kind] || (ensure ? (document.callbacks[kind] = LCUI.Blocks.workspace()) : null)) : null;
        }
        var control = document.controls[index];
        if (!control) return null;
        if (ensure) control.callbacks = control.callbacks || {};
        return control.callbacks ? (control.callbacks[kind] || (ensure ? (control.callbacks[kind] = LCUI.Blocks.workspace()) : null)) : null;
    }

    function logicContext() {
        var document = Store.session.document;
        var observables = LCUI.Blocks.observableNames(document);
        var variables = [];
        if (Store.logicTarget) {
            var w = getLogicWorkspace(Store.logicTarget.scope, Store.logicTarget.index, Store.logicTarget.kind, false);
            if (w) variables = LCUI.Blocks.declaredVars(w);
        }
        var formVariable = Model.isIdentifier(document.properties.variable) ? document.properties.variable : 'form';
        return {
            observables: observables,
            variables: variables,
            formVariable: formVariable,
            hint: '回调逻辑将生成 on: func () -> void { … }（按钮）或 show(func (result) -> void { … })（表单显示）。'
        };
    }

    function currentLogicTarget() {
        var document = Store.session.document;
        if (Store.selection !== null && document.controls[Store.selection]) {
            var control = document.controls[Store.selection];
            var descriptor = Schema.findControl(control.kind);
            if (descriptor && descriptor.hasCallback) return { scope: 'control', index: Store.selection, kind: 'on' };
        }
        return { scope: 'document', kind: 'shown' };
    }

    function openLogic(target) {
        if (!target) target = currentLogicTarget();
        if (!target) return;
        Store.logicTarget = target;
        Store.logicOpen = true;
        byId('logicPanel').hidden = false;

        var titleText, subText;
        if (target.scope === 'document') {
            titleText = '表单 show 回调';
            subText = '在表单显示后执行';
        } else {
            var control = Store.session.document.controls[target.index];
            var descriptor = Schema.findControl(control.kind);
            titleText = descriptor.label + ' on 回调';
            subText = '在点击「' + (control.properties.text || control.properties.label || descriptor.label) + '」时执行';
        }
        byId('logicTitle').textContent = titleText;
        byId('logicSubtitle').textContent = subText;

        var commit = function () {
            Store.session.recordCallback(target.scope === 'document' ? null : target.index, target.kind, getLogicWorkspace(target.scope, target.index, target.kind, false));
            renderCode();
            renderIssues();
        };
        var getWs = function () { return getLogicWorkspace(target.scope, target.index, target.kind, false); };

        BlockView.show({
            panel: byId('logicPanel'),
            palette: byId('blockPalette'),
            workspace: byId('blockWorkspace'),
            titleEl: byId('logicTitle'),
            subEl: byId('logicSubtitle'),
            bar: byId('logicBar'),
            getWorkspace: getWs,
            getContext: logicContext,
            onCommit: commit
        });
    }

    function closeLogic() {
        BlockView.hide();
        byId('logicPanel').hidden = true;
        Store.logicOpen = false;
        Store.logicTarget = null;
        switchTab('canvasPanel');
    }

    function renderLogic() {
        if (!Store.logicOpen || !Store.logicTarget) return;
        var target = Store.logicTarget;
        var workspace = getLogicWorkspace(target.scope, target.index, target.kind, false);
        if (!workspace) { closeLogic(); return; }
        var commit = function () {
            Store.session.recordCallback(target.scope === 'document' ? null : target.index, target.kind, getLogicWorkspace(target.scope, target.index, target.kind, false));
            renderCode();
            renderIssues();
        };
        var getWs = function () { return getLogicWorkspace(target.scope, target.index, target.kind, false); };
        BlockView.show({
            panel: byId('logicPanel'),
            palette: byId('blockPalette'),
            workspace: byId('blockWorkspace'),
            titleEl: byId('logicTitle'),
            subEl: byId('logicSubtitle'),
            bar: byId('logicBar'),
            getWorkspace: getWs,
            getContext: logicContext,
            onCommit: commit
        });
    }

    function renderField(scope, descriptor, value) {
        var row = el('label', 'field');
        var name = el('span', 'field-name');
        name.textContent = descriptor.label + (descriptor.required ? ' *' : '');
        row.appendChild(name);

        var input = null;
        if (descriptor.type === 'boolean') {
            input = el('input', 'field-checkbox');
            input.type = 'checkbox';
            input.checked = !!value;
        } else if (descriptor.type === 'number') {
            input = el('input', 'field-input');
            input.type = 'number';
            input.value = value === undefined || value === null ? '' : value;
            input.setAttribute('step', String(descriptor.step || 1));
            if (descriptor.min !== undefined) input.setAttribute('min', String(descriptor.min));
            if (descriptor.max !== undefined) input.setAttribute('max', String(descriptor.max));
        } else if (descriptor.type === 'items') {
            input = el('textarea', 'field-input area');
            input.rows = 4;
            input.spellcheck = false;
            input.value = value || '';
            input.placeholder = '每行一个条目';
        } else {
            input = el('input', 'field-input');
            input.type = 'text';
            input.spellcheck = false;
            input.value = value === undefined || value === null ? '' : value;
        }

        input.setAttribute('data-scope', scope);
        input.setAttribute('data-key', descriptor.key);
        row.appendChild(input);
        return row;
    }

    function escapeHtml(text) {
        return String(text).replace(/&/g, '&amp;').replace(/</g, '&lt;').replace(/>/g, '&gt;');
    }

    function highlight(source) {
        return escapeHtml(source)
            .replace(/&quot;/g, '"')
            .replace(/"((?:[^"\\]|\\.)*)"/g, '<span class="token-string">"$1"</span>')
            .replace(/\b(let|new|func|void|true|false|on)\b/g, '<span class="token-keyword">$1</span>');
    }

    function renderCode() {
        var view = byId('codeView');
        var result = Codegen.emit(Store.session.document);
        if (result.ok) {
            Store.lastSource = result.source;
            view.innerHTML = '<code>' + highlight(result.source) + '</code>';
        } else {
            view.innerHTML = '<code class="code-pending">修正校验问题后将在此生成源码…</code>';
        }
    }

    function renderIssues() {
        var bar = byId('issuesBar');
        var issues = Model.validate(Store.session.document);
        if (!issues.length) {
            bar.hidden = true;
            return;
        }
        bar.textContent = '';
        bar.hidden = false;
        bar.appendChild(el('span', 'issues-count', '校验问题 ×' + issues.length));
        issues.slice(0, 3).forEach(function (issue) {
            bar.appendChild(el('span', 'issue-entry', issue.message));
        });
    }

    function renderAll() {
        clampSelection();
        renderStatus();
        renderPalette();
        renderOutline();
        renderCanvas();
        renderInspector();
        renderCode();
        renderIssues();
        renderLogic();
        saveStore();
    }

    function renderLight() {
        clampSelection();
        renderStatus();
        renderOutline();
        renderCanvas();
        renderCode();
        renderIssues();
        saveStore();
    }

    function switchTab(panelId) {
        Store.activeTab = panelId;
        document.querySelectorAll('#tabbar .tab').forEach(function (tab) {
            tab.classList.toggle('active', tab.getAttribute('data-tab') === panelId);
        });
        document.querySelectorAll('#workspace > .panel').forEach(function (panel) {
            panel.classList.toggle('active', panel.getAttribute('id') === panelId);
        });
    }

    function download() {
        var result = Codegen.emit(Store.session.document);
        if (!result.ok) {
            var first = result.issues[0];
            toast('无法导出：' + (first ? first.message : '未知问题'));
            byId('issuesBar').hidden = false;
            switchTab('canvasPanel');
            return;
        }
        var name = (Store.session.document.properties.id || 'form').replace(/[^A-Za-z0-9._-]/g, '_') + '.lcui';
        var blob = new Blob([result.source], { type: 'text/plain;charset=utf-8' });
        var link = document.createElement('a');
        link.href = URL.createObjectURL(blob);
        link.download = name;
        link.click();
        setTimeout(function () { URL.revokeObjectURL(link.href); }, 4000);
        toast('已导出 ' + name);
    }

    function readFieldValue(target) {
        if (target.type === 'checkbox') return target.checked;
        if (target.type === 'number') {
            var value = parseFloat(target.value);
            return isNaN(value) ? undefined : value;
        }
        var text = String(target.value || '');
        if (text.length > Model.Limits.maxTextLength) text = text.slice(0, Model.Limits.maxTextLength);
        return text;
    }

    function wireEvents() {
        byId('btnUndo').addEventListener('click', function () { Store.session.undo(); renderAll(); });
        byId('btnRedo').addEventListener('click', function () { Store.session.redo(); renderAll(); });
        byId('btnNew').addEventListener('click', function () { openNewDialog(); });
        byId('btnSave').addEventListener('click', download);
        byId('btnDownload').addEventListener('click', download);
        byId('btnCopy').addEventListener('click', function () {
            if (!Store.lastSource) return toast('当前无可复制的源码');
            if (navigator.clipboard && navigator.clipboard.writeText) {
                navigator.clipboard.writeText(Store.lastSource).then(function () { toast('源码已复制'); }, function () { toast('复制失败，请手动选择文本'); });
            } else {
                toast('浏览器不支持自动复制');
            }
        });
        byId('btnToggleCode').addEventListener('click', function () {
            byId('codePanel').classList.toggle('collapsed');
        });

        document.querySelectorAll('#tabbar .tab').forEach(function (tab) {
            tab.addEventListener('click', function () {
                var id = tab.getAttribute('data-tab');
                if (id === 'logicTab') {
                    openLogic(currentLogicTarget());
                    document.querySelectorAll('#tabbar .tab').forEach(function (t) { t.classList.toggle('active', t === tab); });
                    return;
                }
                switchTab(id);
            });
        });

        byId('logicClose').addEventListener('click', function () { closeLogic(); });

        byId('inspector').addEventListener('input', function (event) {
            var target = event.target;
            var scope = target.getAttribute('data-scope');
            if (!scope) return;
            var key = target.getAttribute('data-key');
            var value = readFieldValue(target);
            if (value === undefined) return;

            if (scope === 'form') Store.session.setFormProperty(key, value);
            else if (Store.selection !== null) Store.session.setControlProperty(Store.selection, key, value);
            renderLight();
        });

        byId('inspector').addEventListener('change', function (event) {
            if (event.target.getAttribute('data-scope')) renderAll();
        });

        document.addEventListener('keydown', function (event) {
            if (event.ctrlKey || event.metaKey) {
                var key = event.key.toLowerCase();
                if (key === 'z' && !event.shiftKey) { event.preventDefault(); Store.session.undo(); renderAll(); }
                else if (key === 'y' || (key === 'z' && event.shiftKey)) { event.preventDefault(); Store.session.redo(); renderAll(); }
                else if (key === 's') { event.preventDefault(); download(); }
                return;
            }
            if ((event.key === 'Delete' || event.key === 'Backspace') && Store.selection !== null) {
                if (Store.logicOpen) return;
                var active = document.activeElement;
                if (active && /INPUT|TEXTAREA/.test(active.tagName)) return;
                event.preventDefault();
                Store.session.eraseControl(Store.selection);
                clampSelection();
                renderAll();
            }
        });

        window.addEventListener('beforeunload', function (event) {
            if (Store.session.isModified()) {
                event.preventDefault();
                event.returnValue = '';
            }
        });

        byId('importFile').addEventListener('change', function (event) {
            var file = event.target.files && event.target.files[0];
            if (!file) return;
            if (file.size > Model.Limits.maxSourceLength) {
                toast('文件过大（超过 1 MiB）');
                event.target.value = '';
                return;
            }
            var reader = new FileReader();
            reader.onload = function () {
                byId('importText').value = String(reader.result || '');
                byId('importErrors').hidden = true;
            };
            reader.readAsText(file, 'utf-8');
        });

        byId('importConfirm').addEventListener('click', function (event) {
            event.preventDefault();
            var text = byId('importText').value;
            var box = byId('importErrors');
            box.hidden = true;
            box.textContent = '';

            if (text.length > Model.Limits.maxSourceLength) {
                showImportErrors([{ line: 0, message: '脚本超过 1 MiB 上限' }]);
                return;
            }

            var result = Codegen.importDocument(text);
            if (!result.ok) {
                showImportErrors(result.errors);
                return;
            }

            var issues = Model.validate(result.document);
            if (issues.length) {
                showImportErrors(issues.map(function (issue) { return { line: 0, message: issue.message }; }));
                return;
            }

            Store.session.reset(result.document);
            Store.selection = null;
            byId('importDialog').close();
            renderAll();
            toast('导入成功');
        });

        function showImportErrors(errors) {
            var box = byId('importErrors');
            box.textContent = '';
            errors.slice(0, 8).forEach(function (error) {
                box.appendChild(el('div', 'import-error-entry', (error.line ? '第 ' + error.line + ' 行：' : '') + error.message));
            });
            box.hidden = false;
        }

        byId('btnImport').addEventListener('click', function () {
            byId('importText').value = '';
            byId('importFile').value = '';
            byId('importErrors').hidden = true;
            byId('importDialog').showModal();
        });

        document.querySelectorAll('#newDialog [value="cancel"], #importDialog [value="cancel"]').forEach(function (button) {
            button.addEventListener('click', function () { button.form.close(); });
        });
    }

    function openNewDialog() {
        var list = byId('newKindList');
        list.textContent = '';
        var selected = 'Custom';
        Schema.forms.forEach(function (form) {
            var card = el('div', 'kind-card' + (form.kind === selected ? ' selected' : ''));
            card.appendChild(el('strong', null, form.label));
            card.appendChild(el('small', null, form.hint));
            card.addEventListener('click', function () {
                selected = form.kind;
                list.querySelectorAll('.kind-card').forEach(function (entry) { entry.classList.remove('selected'); });
                card.classList.add('selected');
            });
            list.appendChild(card);
        });
        byId('newFormId').value = '';
        byId('newFormTitle').value = '';
        byId('newDialog').showModal();
        setTimeout(function () { byId('newFormId').focus(); }, 60);
        byId('newConfirm').onclick = function (event) {
            event.preventDefault();
            var form = Schema.forms.filter(function (entry) { return entry.kind === selected; })[0];
            var document = byId('newFormSample').checked ? sampleDocument(selected) : defaultDocument(selected);
            var id = byId('newFormId').value.trim();
            var title = byId('newFormTitle').value.trim();
            if (id) document.properties.id = id;
            if (title) document.properties.title = title;

            var issues = Model.validate(document);
            if (issues.length) {
                toast('创建失败：' + issues[0].message);
                return;
            }

            Store.session.reset(document);
            Store.selection = null;
            byId('newDialog').close();
            renderAll();
            toast('已创建 ' + form.name);
        };
    }

    function init() {
        var stored = loadStored();
        Store.session = new Model.Session(stored || sampleDocument());
        if (!stored) Store.selection = null;
        Store.session.onChange(function () { saveStore(); });
        wireEvents();
        switchTab('canvasPanel');
        renderAll();
    }

    if (document.readyState === 'loading') document.addEventListener('DOMContentLoaded', init);
    else init();
})();
