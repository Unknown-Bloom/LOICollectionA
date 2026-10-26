(function () {
    'use strict';

    var Schema = LCUI.Schema;
    var Model = LCUI.Model;
    var Codegen = LCUI.Codegen;

    var Store = {
        key: 'lcui-studio-project-v2',
        legacyKey: 'lcui-studio-document-v1',
        project: null,
        sessions: {},
        selections: {},
        selection: null,
        session: null,
        activeTab: 'canvasPanel',
        lastSource: '',
        justAdded: null,
        logicTarget: null,
        logicOpen: false,
        importFiles: [],
        projectDirty: false
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

    function svgEl(tag, attributes) {
        var node = document.createElementNS('http://www.w3.org/2000/svg', tag);
        Object.keys(attributes || {}).forEach(function (key) { node.setAttribute(key, attributes[key]); });
        return node;
    }

    function toast(message) {
        var node = byId('toast');
        node.textContent = message;
        node.hidden = false;
        clearTimeout(toast.timer);
        toast.timer = setTimeout(function () { node.hidden = true; }, 2400);
    }

    function activeDocument() {
        if (!Store.project) return null;
        return Store.project.documents.filter(function (document) {
            return document.id === Store.project.activeId;
        })[0] || null;
    }

    function documentSummary(control) {
        var properties = control.properties;
        if (control.kind === 'ComponentCall') {
            var target = Model.findComponentByName(Store.project, properties.target);
            return target ? target.properties.name : (properties.target || '未选择组件');
        }
        var source = properties.text || properties.label || properties.title || properties.prompt || '';
        var value = String(source || '').trim();
        return value.length ? value : Schema.findControl(control.kind).label;
    }

    function clampSelection() {
        if (Store.selection !== null && Store.session) {
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

        if (kind === 'Menu') {
            document.properties.id = 'menu.main';
            document.properties.title = '主菜单';

            var intro = Model.makeControl('Label');
            intro.properties.text = '请选择一项操作。';
            var reward = Model.makeControl('MenuButton');
            reward.properties.text = '领取奖励';
            reward.properties.actionId = 'claim';
            reward.properties.run = 'say 已领取奖励';
            reward.properties.scores = 'money:100';
            var back = Model.makeControl('Button');
            back.properties.text = '返回';
            document.controls = [intro, reward, back];
            return document;
        }

        if (kind === 'Shop') {
            document.properties.id = 'shop.main';
            document.properties.title = '商品商店';
            document.properties.type = 'buy';
            document.properties.content = '欢迎选购，点击条目即可购买。';
            document.properties.exitCommand = 'say 已退出商店';
            document.properties.scoreCommand = 'say 积分不足';
            document.properties.titleCommand = 'say 购买称号';

            var apple = Model.makeControl('ShopItem');
            apple.properties.title = '苹果';
            apple.properties.id = 'minecraft:apple';
            apple.properties.introduce = '脆甜的苹果。';
            apple.properties.number = '购买数量';
            apple.properties.scores = 'money:100';
            var titleItem = Model.makeControl('ShopItem');
            titleItem.properties.type = 'title';
            titleItem.properties.title = '勇士称号';
            titleItem.properties.introduce = '显示在头顶的称号。';
            titleItem.properties.confirmButton = '确认';
            titleItem.properties.cancelButton = '取消';
            titleItem.properties.scores = 'money:500';
            document.controls = [apple, titleItem];
            return document;
        }

        document.properties.id = 'example.main';
        document.properties.title = '示例表单';
        document.properties.variable = 'form';

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
        } else if (formKind === 'Menu') {
            document.properties.id = 'menu.main';
            document.properties.title = '主菜单';
            var menuAction = Model.makeControl('MenuButton');
            menuAction.properties.text = '开始';
            var menuLabel = Model.makeControl('Label');
            menuLabel.properties.text = '请选择一项操作。';
            document.controls = [menuLabel, menuAction];
        } else if (formKind === 'Shop') {
            document.properties.id = 'shop.main';
            document.properties.title = '商品商店';
            document.properties.type = 'buy';
            var shopItem = Model.makeControl('ShopItem');
            shopItem.properties.title = '苹果';
            shopItem.properties.id = 'minecraft:apple';
            shopItem.properties.introduce = '脆甜的苹果。';
            document.controls = [shopItem];
        } else {
            document.properties.id = 'example.main';
            document.properties.title = '示例表单';
            document.controls = [Model.makeControl('Label'), Model.makeControl('Button')];
            document.controls[0].properties.text = '欢迎使用 LCUI Studio';
            document.controls[1].properties.text = '确定';
        }
        return document;
    }

    function sampleDeclaration(kind) {
        if (kind === 'Enum') {
            var enumeration = Model.makeDeclaration('Enum', 'Option');
            enumeration.properties.name = 'Option';
            enumeration.properties.variants = 'Some(int)\nNothing';
            return enumeration;
        }
        var component = Model.makeDeclaration('Component', 'ConfirmBar');
        component.properties.name = 'ConfirmBar';
        component.properties.params = 'confirmText, onConfirm';
        var button = Model.makeControl('Button');
        button.properties.text = '$confirmText';
        button.callbackParam = 'onConfirm';
        var close = Model.makeControl('CloseButton');
        component.controls = [button, close];
        return component;
    }

    function loadStored() {
        try {
            var raw = window.localStorage.getItem(Store.key);
            if (raw) {
                var data = JSON.parse(raw);
                var project = Model.normalizeProject(data && data.project);
                if (project.documents.length) return { project: project, selections: data.selections || {} };
            }
            var legacy = window.localStorage.getItem(Store.legacyKey);
            if (!legacy) return null;
            var old = JSON.parse(legacy);
            if (!old || !old.document) return null;
            var document = Model.normalizeDocument(old.document);
            if (!document) return null;
            if (!Model.isFileName(document.properties.file)) document.properties.file = 'form.lcui';
            var project = Model.makeProject([document]);
            var selections = {};
            selections[document.id] = old.selection === undefined ? null : old.selection;
            return { project: project, selections: selections };
        } catch (ignored) {
            return null;
        }
    }

    function ensureUniqueFiles(project) {
        var staged = { documents: [] };
        project.documents.forEach(function (document) {
            var desired = Model.isFileName(document.properties.file) ? document.properties.file : 'document.lcui';
            document.properties.file = Model.uniqueFileName(staged, desired, document.id);
            staged.documents.push(document);
        });
    }

    function registerSession(document) {
        var session = new Model.Session(document);
        session.onChange(function () { saveStore(); });
        Store.sessions[document.id] = session;
        return session;
    }

    function ensureSession(document) {
        return Store.sessions[document.id] || registerSession(document);
    }

    function activateDocument(id, quiet) {
        var document = Store.project.documents.filter(function (entry) { return entry.id === id; })[0];
        if (!document) return;
        var current = activeDocument();
        if (current) Store.selections[current.id] = Store.selection;
        Store.project.activeId = document.id;
        Store.session = ensureSession(document);
        Store.selection = Object.prototype.hasOwnProperty.call(Store.selections, document.id) ? Store.selections[document.id] : null;
        Store.logicOpen = false;
        Store.logicTarget = null;
        byId('logicPanel').hidden = true;
        if (BlockView) BlockView.hide();
        clampSelection();
        saveStore();
        if (!quiet) renderAll();
    }

    function currentSessionId() {
        var document = activeDocument();
        return document ? document.id : null;
    }

    var storeTimer = null;

    function saveStore() {
        if (!Store.project) return;
        var sessionId = currentSessionId();
        if (sessionId) Store.selections[sessionId] = Store.selection;
        clearTimeout(storeTimer);
        storeTimer = setTimeout(function () {
            try {
                window.localStorage.setItem(Store.key, JSON.stringify({
                    project: { activeId: Store.project.activeId, documents: Store.project.documents },
                    selections: Store.selections
                }));
            } catch (ignored) {}
        }, 300);
    }

    function documentIssues(document) {
        return Model.validate(document);
    }

    function projectIssueCount(issues, document) {
        return issues.filter(function (issue) {
            return issue.target === 'document:' + document.id || String(issue.target || '').indexOf('document:' + document.id + ':') === 0;
        }).length;
    }

    function renderStatus() {
        var document = Store.session.document;
        var descriptor = Schema.findForm(document.kind);
        var badge = document.docType === 'form'
            ? descriptor.name
            : document.docType + ' · ' + (document.properties.name || '未命名');
        byId('formKindBadge').textContent = badge;
        byId('btnUndo').disabled = !Store.session.canUndo();
        byId('btnRedo').disabled = !Store.session.canRedo();
        byId('canvasTitle').textContent = Model.documentName(document) || '（未命名定义）';
        window.document.title = (Store.session.isModified() ? '● ' : '') + 'LCUI Studio · .lcui 可视化编辑器';
    }

    function renderPalette() {
        var host = byId('palette');
        host.textContent = '';
        var document = Store.session.document;

        if (document.docType !== 'form' && document.docType !== 'component') {
            host.appendChild(el('div', 'palette-hint', '枚举声明没有可添加的界面组件，请在右侧编辑变体。'));
            return;
        }

        var kinds = ['Label', 'Header', 'TextField', 'Dropdown', 'Toggle', 'Slider', 'Button', 'MenuButton', 'ShopItem', 'Divider', 'Spacer', 'Body', 'Button1', 'Button2', 'PreviousButton', 'NextButton', 'ChooseButton', 'CloseButton', 'ComponentCall'];
        var groups = { '基础': [], '输入': [], '操作': [], '分页': [], '消息框': [], '菜单': [], '商店': [], '组件': [] };
        var hasComponent = Store.project.documents.some(function (entry) {
            return entry.docType === 'component' && entry.id !== document.id;
        });

        kinds.forEach(function (kind) {
            var descriptor = Schema.findControl(kind);
            if (kind === 'ComponentCall' && !hasComponent) return;
            if (!Store.session.accepts(kind)) return;
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
        var glyphs = { Label: 'T', Header: 'H', TextField: '⌨', Dropdown: '▾', Toggle: '◑', Slider: '⇿', Button: '▭', MenuButton: '⚑', ShopItem: '▤', Divider: '—', Spacer: '␣', Body: '¶', Button1: '1', Button2: '2', PreviousButton: '◀', NextButton: '▶', ChooseButton: '↷', CloseButton: '✕', ComponentCall: '⧉' };
        return glyphs[kind] || '▪';
    }

    function addControl(kind) {
        var index = Store.selection === null ? Store.session.document.controls.length : Store.selection + 1;
        if (!Store.session.insertControl(index, kind)) {
            toast('无法添加该组件');
            return;
        }
        Store.selection = index;
        Store.justAdded = index;
        renderAll();
        switchTab('canvasPanel');
    }

    function renderOutline() {
        var host = byId('outline');
        host.textContent = '';
        var document = Store.session.document;

        if (document.docType === 'enum') {
            var variants = Model.enumVariants(document);
            if (!variants.length) {
                host.appendChild(el('div', 'outline-empty', '尚未定义变体'));
                return;
            }
            variants.forEach(function (variant, index) {
                var row = el('div', 'outline-row');
                row.appendChild(el('span', 'outline-glyph', '◆'));
                row.appendChild(el('span', 'outline-text', (index + 1) + '. ' + variant.name + (variant.params.length ? '(' + variant.params.join(', ') + ')' : '')));
                host.appendChild(row);
            });
            return;
        }

        var controls = document.controls;
        if (!controls.length) {
            host.appendChild(el('div', 'outline-empty', document.docType === 'component' ? '组件体为空，点击上方组件库添加' : '暂无组件，点击上方组件库添加'));
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

    function renderEnumCanvas(document) {
        var mock = byId('formMock');
        mock.textContent = '';
        var card = el('div', 'declaration-card');
        var head = el('div', 'declaration-head');
        head.appendChild(el('span', 'declaration-kind', 'enum'));
        head.appendChild(el('span', 'declaration-name', document.properties.name || '未命名枚举'));
        card.appendChild(head);

        var variants = Model.enumVariants(document);
        if (!variants.length) {
            card.appendChild(el('div', 'declaration-empty', '尚未定义变体'));
        } else {
            var list = el('div', 'declaration-variants');
            variants.forEach(function (variant) {
                var row = el('div', 'declaration-variant' + (variant.valid ? '' : ' invalid'));
                row.appendChild(el('span', 'variant-name', variant.name || '?'));
                if (variant.params.length)
                    row.appendChild(el('span', 'variant-params', '(' + variant.params.join(', ') + ')'));
                list.appendChild(row);
            });
            card.appendChild(list);
        }
        var file = el('div', 'declaration-file', document.properties.file || '');
        card.appendChild(file);
        mock.appendChild(card);
    }

    function renderCanvas() {
        var mock = byId('formMock');
        mock.textContent = '';

        var document = Store.session.document;
        if (document.docType === 'enum') {
            renderEnumCanvas(document);
            return;
        }

        var properties = document.properties;
        var invalid = new Set();
        documentIssues(document).forEach(function (issue) {
            var match = /^control:(\d+)/.exec(issue.target || '');
            if (match) invalid.add(Number(match[1]));
        });

        var component = document.docType === 'component';
        var head = el('div', 'mock-head');
        if (component) {
            head.appendChild(richEl('span', 'mock-title', 'component ' + (properties.name || '未命名')));
            if (Model.componentParams(document).length)
                head.appendChild(el('span', 'mock-subtitle', '(' + Model.componentParams(document).join(', ') + ')'));
        } else {
            head.appendChild(richEl('span', 'mock-title', properties.title || '未命名'));
            head.appendChild(el('span', 'mock-close', '✕'));
        }
        mock.appendChild(head);

        var body = el('div', 'mock-body');
        if (!component && document.kind === 'MessageBox') {
            document.controls.forEach(function (control, index) { body.appendChild(renderWidget(control, index, invalid)); });
        } else if (!component && document.kind === 'Paginated') {
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
        } else if (!component && document.kind === 'Shop') {
            body.appendChild(el('div', 'mc-shop-page', '1 / ' + Math.max(1, Math.ceil(document.controls.length / 10))));
            if (properties.content) body.appendChild(richEl('div', 'mc-shop-content', properties.content));
            document.controls.forEach(function (control, index) { body.appendChild(renderWidget(control, index, invalid)); });
        } else {
            document.controls.forEach(function (control, index) { body.appendChild(renderWidget(control, index, invalid)); });
        }
        mock.appendChild(body);

        if (!component && document.kind === 'MessageBox') {
            var actions = el('div', 'mock-actions');
            actions.appendChild(richEl('div', 'mc-button half', button1Text(document)));
            actions.appendChild(richEl('div', 'mc-button half', button2Text(document)));
            mock.appendChild(actions);
        } else if (!component && document.kind === 'Paginated') {
            var pager = el('div', 'mock-actions pager');
            pager.appendChild(richEl('div', 'mc-button small', footerText(document, 'PreviousButton', '◀ 上一页')));
            pager.appendChild(richEl('div', 'mc-button small', footerText(document, 'ChooseButton', '跳转')));
            pager.appendChild(richEl('div', 'mc-button small', footerText(document, 'NextButton', '下一页 ▶')));
            mock.appendChild(pager);
        } else if (!component && document.kind === 'Shop') {
            var shopPager = el('div', 'mock-actions pager');
            shopPager.appendChild(richEl('div', 'mc-button small', '◀ 上一页'));
            shopPager.appendChild(richEl('div', 'mc-button small', '下一页 ▶'));
            mock.appendChild(shopPager);
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

        if (kind === 'ComponentCall') {
            var target = Model.findComponentByName(Store.project, properties.target);
            var chip = el('div', 'mc-component-call');
            chip.appendChild(el('span', 'mc-component-glyph', '⧉'));
            chip.appendChild(el('span', 'mc-component-name', target ? target.properties.name : (properties.target || '未选择组件')));
            var args = Model.callArguments(properties.arguments);
            if (args.length) chip.appendChild(el('span', 'mc-component-args', '(' + args.join(', ') + ')'));
            node.appendChild(chip);
        } else if (kind === 'Label') node.appendChild(richEl('div', 'mc-label', properties.text || ''));
        else if (kind === 'Header') node.appendChild(richEl('div', 'mc-header', properties.text || ''));
        else if (kind === 'Body') node.appendChild(richEl('div', 'mc-body', properties.text || ''));
        else if (kind === 'Divider') node.appendChild(el('div', 'mc-divider'));
        else if (kind === 'Spacer') node.appendChild(el('div', 'mc-spacer'));
        else if (kind === 'Button' || kind === 'MenuButton') node.appendChild(richEl('div', 'mc-button', properties.text || '按钮'));
        else if (kind === 'ShopItem') {
            var card = el('div', 'mc-shop-item');
            card.appendChild(richEl('div', 'mc-shop-title', properties.title || '未命名条目'));
            card.appendChild(el('div', 'mc-shop-meta', (properties.type || 'commodity') + (properties.id ? ' · ' + properties.id : '')));
            if (properties.introduce) card.appendChild(richEl('div', 'mc-shop-desc', properties.introduce));
            node.appendChild(card);
        } else if (kind === 'Button1' || kind === 'Button2') node.appendChild(richEl('div', 'mc-button half', properties.text || '按钮'));
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

        var title = document.docType === 'form' ? '表单设置 · ' + form.name : (document.docType === 'enum' ? '枚举设置' : '组件设置');
        host.appendChild(el('div', 'inspector-section-title', title));
        form.properties.forEach(function (descriptor) {
            host.appendChild(renderField('form', descriptor, document.properties[descriptor.key]));
        });

        if (document.docType !== 'enum') {
            var showBtn = el('button', 'tool wide accent', '编辑 show 回调 ▸');
            var showHint = el('div', 'inspector-hint', '在拼图编辑器中为回调编写逻辑。');
            if (document.docType === 'component') {
                showBtn.disabled = true;
                showHint.textContent = '组件声明没有 show 回调，回调逻辑请绑定到按钮 on:。';
            } else {
                showBtn.addEventListener('click', function () { openLogic({ scope: 'document', kind: 'shown' }); });
            }
            host.appendChild(showBtn);
            host.appendChild(showHint);
        }

        if (document.docType === 'enum' || Store.selection === null) {
            if (document.docType === 'enum') {
                host.appendChild(el('div', 'inspector-hint', '每行一个变体：Some(int) 或 Nothing（无载荷变体可省略括号）。'));
            } else {
                host.appendChild(el('div', 'inspector-hint', '在画布中选择一个组件以编辑其属性'));
            }
            return;
        }

        var control = document.controls[Store.selection];
        if (!control) return;
        var descriptor = Schema.findControl(control.kind);

        host.appendChild(el('div', 'inspector-section-title control-title', '组件属性 · ' + descriptor.label));
        if (descriptor.hasCallback) {
            if (document.docType === 'component' && Model.componentParams(document).length) {
                host.appendChild(renderCallbackParam(document, control));
            }
            var onBtn = el('button', 'tool wide accent', '编辑 on 回调 ▸');
            onBtn.disabled = !!control.callbackParam;
            onBtn.addEventListener('click', function () { openLogic({ scope: 'control', index: Store.selection, kind: 'on' }); });
            host.appendChild(onBtn);
            if (control.callbackParam)
                host.appendChild(el('div', 'inspector-hint', '当前回调绑定组件参数 ' + control.callbackParam + '，内联逻辑不会写入源码。'));
            else
                host.appendChild(el('div', 'inspector-hint', '在拼图编辑器中为回调编写逻辑。'));
        }
        if (!descriptor.properties.length && !descriptor.hasCallback)
            host.appendChild(el('div', 'inspector-hint', '该组件没有可编辑属性'));

        descriptor.properties.forEach(function (propertyDescriptor) {
            host.appendChild(renderField('control', propertyDescriptor, control.properties[propertyDescriptor.key]));
        });

        if (document.docType === 'component' && descriptor.hasCallback && !Model.componentParams(document).length)
            host.appendChild(el('div', 'inspector-hint', '为组件添加参数后，可把 on: 回调直接绑定到参数。'));
    }

    function renderCallbackParam(document, control) {
        var row = el('label', 'field');
        row.appendChild(el('span', 'field-name', '回调参数'));
        var select = el('select', 'field-input');
        var empty = el('option', null, '内联实现');
        empty.value = '';
        select.appendChild(empty);
        Model.componentParams(document).forEach(function (name) {
            var option = el('option', null, name);
            option.value = name;
            select.appendChild(option);
        });
        select.value = control.callbackParam || '';
        select.addEventListener('change', function () {
            Store.session.setControlCallbackParam(Store.selection, select.value);
            renderAll();
        });
        row.appendChild(select);
        return row;
    }

    function renderField(scope, descriptor, value) {
        var row = el('label', 'field');
        var name = el('span', 'field-name');
        name.textContent = descriptor.label + (descriptor.required ? ' *' : '');
        row.appendChild(name);

        var input = null;
        if (descriptor.type === 'document') {
            input = el('select', 'field-input');
            var empty = el('option', null, '（选择组件）');
            empty.value = '';
            input.appendChild(empty);
            var self = Store.session.document.id;
            Store.project.documents.forEach(function (document) {
                if (document.docType !== 'component' || document.id === self) return;
                var option = el('option', null, document.properties.name + ' · ' + document.properties.file);
                option.value = document.properties.name;
                input.appendChild(option);
            });
            input.value = value || '';
        } else if (descriptor.type === 'boolean') {
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
        } else if (descriptor.type === 'items' || descriptor.type === 'scores' || descriptor.type === 'arguments' || descriptor.type === 'variants') {
            input = el('textarea', 'field-input area');
            input.rows = descriptor.type === 'variants' ? 5 : 4;
            input.spellcheck = false;
            input.value = value || '';
            input.placeholder = descriptor.type === 'scores'
                ? '每行一个：计分板:分数'
                : descriptor.type === 'arguments'
                    ? '每行一个：$参数、=标识符 或普通文本'
                    : descriptor.type === 'variants'
                        ? '每行一个变体：Some(int)'
                        : '每行一个条目';
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
        var context = {
            observables: observables,
            variables: variables,
            hint: document.docType === 'component'
                ? '回调逻辑将生成 on: func () -> void { … }，在组件展开后执行。'
                : '回调逻辑将生成 on: func () -> void { … }（按钮）或 show(func (result) -> void { … })（表单显示）。'
        };
        if (document.docType !== 'component') context.formVariable = formVariable;
        return context;
    }

    function currentLogicTarget() {
        var document = Store.session.document;
        if (Store.selection !== null && document.controls[Store.selection]) {
            var control = document.controls[Store.selection];
            var descriptor = Schema.findControl(control.kind);
            if (descriptor && descriptor.hasCallback && !control.callbackParam) return { scope: 'control', index: Store.selection, kind: 'on' };
        }
        if (document.docType === 'component') return null;
        return { scope: 'document', kind: 'shown' };
    }

    function openLogic(target) {
        if (!target) target = currentLogicTarget();
        if (!target) {
            toast('组件声明没有可用于该文档的 show 回调');
            switchTab('canvasPanel');
            return;
        }
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
        getLogicWorkspace(target.scope, target.index, target.kind, true);

        var commit = function (previous) {
            Store.session.recordCallback(target.scope === 'document' ? null : target.index, target.kind, getLogicWorkspace(target.scope, target.index, target.kind, false), previous);
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
        var commit = function (previous) {
            Store.session.recordCallback(target.scope === 'document' ? null : target.index, target.kind, getLogicWorkspace(target.scope, target.index, target.kind, false), previous);
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

    function escapeHtml(text) {
        return String(text).replace(/&/g, '&amp;').replace(/</g, '&lt;').replace(/>/g, '&gt;');
    }

    function highlight(source) {
        return escapeHtml(source)
            .replace(/&quot;/g, '"')
            .replace(/"((?:[^"\\]|\\.)*)"/g, '<span class="token-string">"$1"</span>')
            .replace(/\b(let|new|func|void|true|false|on|import|enum|component)\b/g, '<span class="token-keyword">$1</span>');
    }

    function renderCode() {
        var view = byId('codeView');
        var result = Codegen.emit(Store.session.document, Store.project);
        if (result.ok) {
            Store.lastSource = result.source;
            view.innerHTML = '<code>' + highlight(result.source) + '</code>';
        } else {
            view.innerHTML = '<code class="code-pending">修正校验问题后将在此生成源码…</code>';
        }
    }

    function renderIssues() {
        var bar = byId('issuesBar');
        var issues = documentIssues(Store.session.document).map(function (issue) {
            return { level: 'error', message: issue.message };
        });
        Model.projectIssues(Store.project).forEach(function (issue) { issues.push(issue); });
        if (!issues.length) {
            bar.hidden = true;
            return;
        }
        bar.textContent = '';
        bar.hidden = false;
        bar.appendChild(el('span', 'issues-count', '校验问题 ×' + issues.length));
        issues.slice(0, 3).forEach(function (issue) {
            var entry = el('span', 'issue-entry' + (issue.level === 'warning' ? ' warning' : ''), issue.message);
            bar.appendChild(entry);
        });
    }

    function renderProject() {
        var host = byId('projectDocs');
        host.textContent = '';
        var issues = Model.projectIssues(Store.project);
        var projectErrors = issues.filter(function (issue) { return issue.level === 'error'; });
        byId('projectSummary').textContent = Store.project.documents.length + ' 个文档 · ' + (projectErrors.length ? projectErrors.length + ' 个错误' : '无错误');

        Store.project.documents.forEach(function (document) {
            var row = el('div', 'project-doc' + (document.id === Store.project.activeId ? ' active' : ''));
            row.appendChild(el('span', 'project-doc-kind ' + document.docType, document.docType === 'form' ? '表单' : document.docType === 'enum' ? '枚举' : '组件'));
            var text = el('div', 'project-doc-text');
            text.appendChild(el('strong', null, Model.documentName(document) || '未命名'));
            text.appendChild(el('small', null, document.properties.file));
            row.appendChild(text);
            var count = projectIssueCount(issues, document);
            if (count) row.appendChild(el('span', 'project-doc-badge', String(count)));
            row.addEventListener('click', function () { activateDocument(document.id); });
            host.appendChild(row);
        });

        renderGraph(issues);
        renderProjectIssues(issues);
    }

    function reachable(edgesBySource, from, target) {
        var seen = {};
        var queue = [from];
        while (queue.length) {
            var current = queue.shift();
            if (current === target) return true;
            if (seen[current]) continue;
            seen[current] = true;
            (edgesBySource[current] || []).forEach(function (next) { queue.push(next); });
        }
        return false;
    }

    function renderGraph(issues) {
        var host = byId('projectGraph');
        host.textContent = '';
        var documents = Store.project.documents;
        if (!documents.length) return;

        var edges = Model.projectEdges(Store.project);
        var byIdMap = {};
        documents.forEach(function (document) { byIdMap[document.id] = document; });
        var outgoing = {};
        var incoming = {};
        documents.forEach(function (document) { outgoing[document.id] = []; incoming[document.id] = []; });
        edges.forEach(function (edge) {
            if (!outgoing[edge.from]) outgoing[edge.from] = [];
            if (!incoming[edge.to]) incoming[edge.to] = [];
            outgoing[edge.from].push(edge.to);
            incoming[edge.to].push(edge.from);
        });

        var depth = {};
        var visiting = {};
        function level(id) {
            if (depth[id] !== undefined) return depth[id];
            if (visiting[id]) return 0;
            visiting[id] = true;
            var value = 0;
            (outgoing[id] || []).forEach(function (next) { value = Math.max(value, level(next) + 1); });
            visiting[id] = false;
            depth[id] = value;
            return value;
        }
        documents.forEach(function (document) { level(document.id); });

        var layers = {};
        var maxLayer = 0;
        documents.forEach(function (document) {
            var value = depth[document.id] || 0;
            maxLayer = Math.max(maxLayer, value);
            (layers[value] = layers[value] || []).push(document);
        });

        var nodeWidth = 164;
        var nodeHeight = 42;
        var gapX = 26;
        var gapY = 74;
        var width = 0;
        Object.keys(layers).forEach(function (key) {
            width = Math.max(width, layers[key].length * nodeWidth + Math.max(0, layers[key].length - 1) * gapX);
        });
        width = Math.max(520, width + 40);
        var height = (maxLayer + 1) * gapY + 30;

        var svg = svgEl('svg', { viewBox: '0 0 ' + width + ' ' + height, class: 'graph-svg' });
        var defs = svgEl('defs');
        var marker = svgEl('marker', { id: 'graph-arrow', viewBox: '0 0 10 10', refX: '9', refY: '5', markerWidth: '6', markerHeight: '6', orient: 'auto-start-reverse' });
        marker.appendChild(svgEl('path', { d: 'M 0 0 L 10 5 L 0 10 z', fill: '#5f6b7a' }));
        defs.appendChild(marker);
        var cycleMarker = svgEl('marker', { id: 'graph-arrow-cycle', viewBox: '0 0 10 10', refX: '9', refY: '5', markerWidth: '6', markerHeight: '6', orient: 'auto-start-reverse' });
        cycleMarker.appendChild(svgEl('path', { d: 'M 0 0 L 10 5 L 0 10 z', fill: '#ef5350' }));
        defs.appendChild(cycleMarker);
        svg.appendChild(defs);

        var positions = {};
        Object.keys(layers).forEach(function (key) {
            var list = layers[key];
            var total = list.length * nodeWidth + Math.max(0, list.length - 1) * gapX;
            var startX = (width - total) / 2;
            list.forEach(function (document, index) {
                positions[document.id] = {
                    x: startX + index * (nodeWidth + gapX),
                    y: height - 30 - (Number(key) + 1) * gapY + 4
                };
            });
        });

        edges.forEach(function (edge) {
            var from = positions[edge.from];
            var to = positions[edge.to];
            if (!from || !to) return;
            var cyclic = reachable(outgoing, edge.to, edge.from);
            var line = svgEl('line', {
                x1: from.x + nodeWidth / 2,
                y1: from.y + nodeHeight,
                x2: to.x + nodeWidth / 2,
                y2: to.y,
                class: 'graph-edge' + (cyclic ? ' cycle' : ''),
                'marker-end': cyclic ? 'url(#graph-arrow-cycle)' : 'url(#graph-arrow)'
            });
            var title = svgEl('title');
            title.textContent = (byIdMap[edge.from] ? Model.documentName(byIdMap[edge.from]) : '') + ' → ' + (byIdMap[edge.to] ? Model.documentName(byIdMap[edge.to]) : '');
            line.appendChild(title);
            svg.appendChild(line);
        });

        documents.forEach(function (document) {
            var position = positions[document.id];
            if (!position) return;
            var group = svgEl('g', { class: 'graph-node' + (document.id === Store.project.activeId ? ' active' : '') });
            group.appendChild(svgEl('rect', { x: position.x, y: position.y, width: nodeWidth, height: nodeHeight, rx: 8 }));
            var title = svgEl('text', { x: position.x + 12, y: position.y + 17, class: 'graph-node-title' });
            var label = Model.documentName(document) || '未命名';
            title.textContent = label.length > 20 ? label.slice(0, 19) + '…' : label;
            group.appendChild(title);
            var sub = svgEl('text', { x: position.x + 12, y: position.y + 32, class: 'graph-node-file' });
            var file = document.properties.file || '';
            sub.textContent = document.docType + ' · ' + (file.length > 22 ? file.slice(0, 21) + '…' : file);
            group.appendChild(sub);
            var count = projectIssueCount(issues, document);
            if (count) {
                group.appendChild(svgEl('circle', { cx: position.x + nodeWidth - 12, cy: position.y + 12, r: 7, class: 'graph-node-badge' }));
                var badge = svgEl('text', { x: position.x + nodeWidth - 12, y: position.y + 16, class: 'graph-node-badge-text' });
                badge.textContent = String(count);
                group.appendChild(badge);
            }
            group.addEventListener('click', function () { activateDocument(document.id); });
            svg.appendChild(group);
        });

        host.appendChild(svg);
    }

    function renderProjectIssues(issues) {
        var host = byId('projectIssues');
        host.textContent = '';
        if (!issues.length) {
            host.appendChild(el('div', 'project-ok', '项目结构完整，没有检测到冲突或循环依赖。'));
            return;
        }
        issues.forEach(function (issue) {
            var row = el('div', 'project-issue ' + (issue.level === 'warning' ? 'warning' : 'error'));
            row.appendChild(el('span', 'project-issue-level', issue.level === 'warning' ? '警告' : '错误'));
            row.appendChild(el('span', null, issue.message));
            host.appendChild(row);
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
        renderProject();
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
        renderProject();
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

    function activeFileName(document) {
        var file = document.properties.file;
        return Model.isFileName(file) ? file : (document.properties.id || 'form').replace(/[^A-Za-z0-9._-]/g, '_') + '.lcui';
    }

    function download() {
        var result = Codegen.emit(Store.session.document, Store.project);
        if (!result.ok) {
            var first = result.issues[0];
            toast('无法导出：' + (first ? first.message : '未知问题'));
            byId('issuesBar').hidden = false;
            switchTab('canvasPanel');
            return;
        }
        var name = activeFileName(Store.session.document);
        var blob = new Blob([result.source], { type: 'text/plain;charset=utf-8' });
        var link = document.createElement('a');
        link.href = URL.createObjectURL(blob);
        link.download = name;
        link.click();
        setTimeout(function () { URL.revokeObjectURL(link.href); }, 4000);
        Store.session.markSaved();
        toast('已导出 ' + name);
    }

    function exportProject() {
        var documents = Store.project.documents;
        var sources = [];
        var failure = null;
        documents.forEach(function (document) {
            var result = Codegen.emit(document, Store.project);
            if (!result.ok && !failure) failure = result.issues[0];
            if (result.ok) sources.push({ name: document.properties.file, content: result.source });
        });
        if (failure) {
            toast('无法导出项目：' + (failure ? failure.message : '未知问题'));
            switchTab('projectPanel');
            return;
        }
        LCUI.Zip.download(sources, 'lcui-project.zip');
        documents.forEach(function (document) {
            if (Store.sessions[document.id]) Store.sessions[document.id].markSaved();
        });
        Store.projectDirty = false;
        toast('已导出 ' + sources.length + ' 个文件');
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

    function applyField(target) {
        var scope = target.getAttribute('data-scope');
        if (!scope) return;
        var key = target.getAttribute('data-key');
        var value = readFieldValue(target);
        if (value === undefined) return;

        if (scope === 'form') Store.session.setFormProperty(key, value);
        else if (Store.selection !== null) Store.session.setControlProperty(Store.selection, key, value);
        renderLight();
    }

    function wireEvents() {
        byId('btnUndo').addEventListener('click', function () { Store.session.undo(); renderAll(); });
        byId('btnRedo').addEventListener('click', function () { Store.session.redo(); renderAll(); });
        byId('btnNew').addEventListener('click', function () { openNewDialog(); });
        byId('btnAddDoc').addEventListener('click', function () { openNewDialog(); });
        byId('btnProject').addEventListener('click', function () { switchTab('projectPanel'); });
        byId('btnProjectClose').addEventListener('click', function () { switchTab('canvasPanel'); });
        byId('btnSave').addEventListener('click', download);
        byId('btnDownload').addEventListener('click', download);
        byId('btnExportProject').addEventListener('click', exportProject);
        byId('btnDeleteDoc').addEventListener('click', deleteActiveDocument);
        byId('btnNewProject').addEventListener('click', resetProject);
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
            if (event.target.tagName === 'SELECT') return;
            applyField(event.target);
        });

        byId('inspector').addEventListener('change', function (event) {
            if (event.target.tagName === 'SELECT') applyField(event.target);
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
                if (active && /INPUT|TEXTAREA|SELECT/.test(active.tagName)) return;
                event.preventDefault();
                Store.session.eraseControl(Store.selection);
                clampSelection();
                renderAll();
            }
        });

        window.addEventListener('beforeunload', function (event) {
            var modified = Store.projectDirty || Store.project.documents.some(function (document) {
                return Store.sessions[document.id] && Store.sessions[document.id].isModified();
            });
            if (modified) {
                event.preventDefault();
                event.returnValue = '';
            }
        });

        byId('importFile').addEventListener('change', function (event) {
            var files = Array.prototype.slice.call(event.target.files || []);
            if (!files.length) return;
            Store.importFiles = [];
            var list = byId('importFileList');
            list.textContent = '';
            var pending = files.length;
            files.forEach(function (file) {
                if (file.size > Model.Limits.maxSourceLength) {
                    toast(file.name + ' 超过 1 MiB');
                    pending--;
                    return;
                }
                var reader = new FileReader();
                reader.onload = function () {
                    Store.importFiles.push({ name: file.name, text: String(reader.result || '') });
                    list.appendChild(el('div', 'import-file-entry', file.name));
                    pending--;
                    if (!pending) {
                        byId('importText').value = Store.importFiles[0] ? Store.importFiles[0].text : '';
                        byId('importErrors').hidden = true;
                    }
                };
                reader.readAsText(file, 'utf-8');
            });
        });

        byId('importText').addEventListener('input', function () {
            Store.importFiles = [];
            byId('importFileList').textContent = '';
        });

        byId('importConfirm').addEventListener('click', function (event) {
            event.preventDefault();
            var box = byId('importErrors');
            box.hidden = true;
            box.textContent = '';

            var files = Store.importFiles.length
                ? Store.importFiles.slice()
                : [{ name: 'imported.lcui', text: byId('importText').value }];

            if (files.some(function (file) { return file.text.length > Model.Limits.maxSourceLength; })) {
                showImportErrors([{ name: '', line: 0, message: '脚本超过 1 MiB 上限' }]);
                return;
            }

            var errors = [];
            var documents = [];
            files.forEach(function (file) {
                var result = Codegen.importDocument(file.text, file.name);
                if (!result.ok) {
                    (result.errors || []).forEach(function (error) {
                        errors.push({ name: file.name, line: error.line, message: error.message });
                    });
                    return;
                }
                documents.push(result.document);
            });

            if (errors.length) {
                showImportErrors(errors);
                return;
            }

            if (!documents.length) {
                showImportErrors([{ name: '', line: 0, message: '没有可导入的脚本' }]);
                return;
            }

            var staged = { documents: [] };
            documents.forEach(function (document) {
                var desired = Model.isFileName(document.properties.file) ? document.properties.file : 'document.lcui';
                document.properties.file = Model.uniqueFileName(staged, desired, document.id);
                staged.documents.push(document);
            });

            var project = Model.makeProject(documents);
            var projectErrors = Model.projectIssues(project).filter(function (issue) { return issue.level === 'error'; });
            if (projectErrors.length) {
                showImportErrors(projectErrors.map(function (issue) { return { name: '', line: 0, message: issue.message }; }));
                return;
            }

            if (Store.project && Store.project.documents.length) {
                var proceed = window.confirm('导入将替换当前项目（' + Store.project.documents.length + ' 个文档），继续？');
                if (!proceed) return;
            }

            Store.project = project;
            Store.sessions = {};
            Store.selections = {};
            Store.projectDirty = true;
            documents.forEach(function (document) {
                if (document.docType === 'form') {
                    Store.selections[document.id] = null;
                }
            });
            var preferred = documents.filter(function (document) { return document.docType === 'form'; })[0] || documents[0];
            Store.session = registerSession(preferred);
            Store.project.activeId = preferred.id;
            Store.selection = null;
            byId('importDialog').close();
            renderAll();
            toast('已导入 ' + documents.length + ' 个文档');
        });

        function showImportErrors(errors) {
            var box = byId('importErrors');
            box.textContent = '';
            errors.slice(0, 8).forEach(function (error) {
                box.appendChild(el('div', 'import-error-entry', (error.name ? error.name + '：' : '') + (error.line ? '第 ' + error.line + ' 行：' : '') + error.message));
            });
            box.hidden = false;
        }

        byId('btnImport').addEventListener('click', function () {
            Store.importFiles = [];
            byId('importText').value = '';
            byId('importFile').value = '';
            byId('importFileList').textContent = '';
            byId('importErrors').hidden = true;
            byId('importDialog').showModal();
        });

        byId('btnImportProject').addEventListener('click', function () {
            byId('btnImport').click();
        });

        document.querySelectorAll('#newDialog [value="cancel"], #importDialog [value="cancel"]').forEach(function (button) {
            button.addEventListener('click', function () { button.form.close(); });
        });
    }

    function deleteActiveDocument() {
        if (Store.project.documents.length <= 1) {
            toast('项目至少需要保留一个文档');
            return;
        }
        var document = activeDocument();
        if (!document) return;
        if (!window.confirm('删除文档 ' + (Model.documentName(document) || document.properties.file) + '？引用它的组件调用会保留并显示为错误。')) return;

        var index = Store.project.documents.indexOf(document);
        Store.project.documents.splice(index, 1);
        delete Store.sessions[document.id];
        delete Store.selections[document.id];
        Store.projectDirty = true;
        var next = Store.project.documents[Math.min(index, Store.project.documents.length - 1)];
        Store.project.activeId = next.id;
        Store.session = ensureSession(next);
        Store.selection = Object.prototype.hasOwnProperty.call(Store.selections, next.id) ? Store.selections[next.id] : null;
        renderAll();
        toast('已删除 ' + document.properties.file);
    }

    function resetProject() {
        if (!window.confirm('新建项目将清空当前项目的全部文档，继续？')) return;
        var document = sampleDocument();
        document.properties.file = Model.uniqueFileName({ documents: [] }, 'form.lcui');
        var project = Model.makeProject([document]);
        Store.project = project;
        Store.sessions = {};
        Store.selections = {};
        Store.projectDirty = true;
        Store.session = registerSession(document);
        Store.selection = null;
        renderAll();
        switchTab('canvasPanel');
        toast('已新建项目');
    }

    function openNewDialog() {
        var list = byId('newKindList');
        list.textContent = '';
        var selected = 'Custom';

        function card(kind, label, hint) {
            var entry = el('div', 'kind-card' + (kind === selected ? ' selected' : ''));
            entry.appendChild(el('strong', null, label));
            entry.appendChild(el('small', null, hint));
            entry.addEventListener('click', function () {
                selected = kind;
                list.querySelectorAll('.kind-card').forEach(function (node) { node.classList.remove('selected'); });
                entry.classList.add('selected');
                updateDefaults();
            });
            list.appendChild(entry);
        }

        function updateDefaults() {
            var isForm = selected !== 'Enum' && selected !== 'Component';
            byId('newIdLabel').textContent = isForm ? '表单标识' : (selected === 'Enum' ? '枚举名' : '组件名');
            byId('newTitleRow').hidden = !isForm;
            byId('newFormId').placeholder = isForm ? 'example.main' : (selected === 'Enum' ? 'Option' : 'ConfirmBar');
            var descriptor = isForm ? Schema.findForm(selected) : null;
            var desired = descriptor && descriptor.file ? descriptor.file : (selected === 'Enum' ? 'option.lcui' : selected === 'Component' ? 'component.lcui' : 'form.lcui');
            byId('newDocFile').value = Model.uniqueFileName(Store.project, desired);
        }

        Schema.documents.forEach(function (document) { card(document.kind === 'form' ? 'Custom' : document.kind === 'enum' ? 'Enum' : 'Component', document.label, document.hint); });
        Schema.forms.forEach(function (form) {
            if (form.kind === 'Custom') return;
            card(form.kind, form.label, form.hint);
        });
        updateDefaults();

        byId('newFormId').value = '';
        byId('newFormTitle').value = '';
        byId('newDialog').showModal();
        setTimeout(function () { byId('newFormId').focus(); }, 60);
        byId('newConfirm').onclick = function (event) {
            event.preventDefault();
            var name = byId('newFormId').value.trim();
            var title = byId('newFormTitle').value.trim();
            var file = Model.uniqueFileName(Store.project, byId('newDocFile').value.trim());
            var document;
            var label;

            if (selected === 'Enum' || selected === 'Component') {
                document = byId('newFormSample').checked ? sampleDeclaration(selected) : Model.makeDeclaration(selected, name);
                if (name) document.properties.name = name;
                document.properties.file = file;
                label = selected === 'Enum' ? '枚举' : '组件';
                if (selected === 'Enum' && !Model.isIdentifier(document.properties.name)) {
                    toast('枚举名必须是以字母或下划线开头的标识符');
                    return;
                }
                if (selected === 'Component' && !Model.isIdentifier(document.properties.name)) {
                    toast('组件名必须是以字母或下划线开头的标识符');
                    return;
                }
            } else {
                document = byId('newFormSample').checked ? sampleDocument(selected) : defaultDocument(selected);
                if (name) document.properties.id = name;
                if (title) document.properties.title = title;
                document.properties.file = file;
                label = Schema.findForm(selected).name;
            }

            var issues = Model.validate(document);
            if (issues.length) {
                toast('创建失败：' + issues[0].message);
                return;
            }

            Store.project.documents.push(document);
            Store.sessions[document.id] = new Model.Session(document);
            Store.sessions[document.id].onChange(function () { saveStore(); });
            Store.selections[document.id] = null;
            Store.projectDirty = true;
            activateDocument(document.id, true);
            byId('newDialog').close();
            renderAll();
            toast('已创建 ' + label + ' · ' + file);
        };
    }

    function init() {
        var stored = loadStored();
        if (stored && stored.project.documents.length) {
            Store.project = stored.project;
            Store.selections = stored.selections || {};
        } else {
            var document = sampleDocument();
            document.properties.file = 'form.lcui';
            Store.project = Model.makeProject([document]);
            Store.selections = {};
        }
        ensureUniqueFiles(Store.project);
        var active = Store.project.documents.filter(function (document) { return document.id === Store.project.activeId; })[0] || Store.project.documents[0];
        Store.project.activeId = active.id;
        Store.session = ensureSession(active);
        Store.selection = Object.prototype.hasOwnProperty.call(Store.selections, active.id) ? Store.selections[active.id] : null;
        wireEvents();
        switchTab('canvasPanel');
        renderAll();
    }

    if (document.readyState === 'loading') document.addEventListener('DOMContentLoaded', init);
    else init();
})();
