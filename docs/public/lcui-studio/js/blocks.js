var LCUI = globalThis.LCUI = globalThis.LCUI || {};

LCUI.Blocks = (function () {
    'use strict';

    var Schema = LCUI.Schema;
    var Model = LCUI.Model;

    function quote(value) {
        if (LCUI.Codegen && LCUI.Codegen.quote) return LCUI.Codegen.quote(value);
        var out = '"';
        var text = String(value == null ? '' : value);
        for (var i = 0; i < text.length; ++i) {
            var c = text.charAt(i);
            if (c === '\\') out += '\\\\';
            else if (c === '"') out += '\\"';
            else if (c === '\n') out += '\\n';
            else out += c;
        }
        return out + '"';
    }

    function numberLiteral(value) {
        if (LCUI.Codegen && LCUI.Codegen.numberLiteral) return LCUI.Codegen.numberLiteral(value);
        var n = Number(value);
        if (!isFinite(n)) return '0';
        if (Math.abs(n) < 1e15 && Math.floor(n) === n) return String(Math.round(n));
        return String(n);
    }

    var CATS = [
        { id: 'nav', label: '表单导航', color: '#27a17a' },
        { id: 'data', label: '数据交互', color: '#3b82f6' },
        { id: 'flow', label: '控制流', color: '#e8820c' },
        { id: 'ops', label: '运算', color: '#a855f7' },
        { id: 'var', label: '变量', color: '#d97706' }
    ];

    var FORM_TYPES = ['FORM_CUSTOM', 'FORM_PAGINATED', 'FORM_MESSAGE'];
    var COMPARE_OPS = ['==', '!=', '>', '<', '>=', '<='];

    var LIST = [
        { id: 'closeForm', cat: 'nav', kind: 'statement', shape: 'stack', label: ['关闭当前表单'],
            emit: function (b, api) { return api.indent(api.level) + 'this.close();'; } },

        { id: 'switchTo', cat: 'nav', kind: 'statement', shape: 'stack',
            fields: [{ key: 'formId', type: 'text', default: '' }, { key: 'formType', type: 'select', default: 'FORM_CUSTOM', options: FORM_TYPES }],
            label: ['切换到表单', { f: 'formId' }, { f: 'formType' }],
            emit: function (b, api) { return api.indent(api.level) + 'GUIManager::switchTo(' + quote(api.field(b, 'formId')) + ', ' + api.field(b, 'formType') + ');'; } },

        { id: 'openForm', cat: 'nav', kind: 'statement', shape: 'stack',
            fields: [{ key: 'owner', type: 'text', default: '' }, { key: 'formId', type: 'text', default: '' }, { key: 'formType', type: 'select', default: 'FORM_CUSTOM', options: FORM_TYPES }],
            inputs: [{ key: 'params', kind: 'expr', defaultExpr: '[]' }],
            label: ['打开表单', { f: 'owner' }, { f: 'formId' }, { f: 'formType' }, '参数', { i: 'params' }],
            emit: function (b, api) { return api.indent(api.level) + 'GUIManager::open(' + quote(api.field(b, 'owner')) + ', ' + quote(api.field(b, 'formId')) + ', ' + api.field(b, 'formType') + ', ' + api.input(b, 'params') + ');'; } },

        { id: 'callback', cat: 'data', kind: 'statement', shape: 'stack',
            fields: [{ key: 'action', type: 'text', default: 'module.action' }],
            inputs: [{ key: 'args', kind: 'expr', defaultExpr: '[]' }],
            label: ['回传数据', { f: 'action' }, '参数', { i: 'args' }],
            emit: function (b, api) { return api.indent(api.level) + 'GUIManager::callback(' + quote(api.field(b, 'action')) + ', ' + api.input(b, 'args') + ');'; } },

        { id: 'setVar', cat: 'var', kind: 'statement', shape: 'stack',
            fields: [{ key: 'name', type: 'varname', default: 'x' }],
            inputs: [{ key: 'value', kind: 'expr', defaultExpr: '""' }],
            label: ['新建变量', { f: 'name' }, '=', { i: 'value' }],
            emit: function (b, api) { return api.indent(api.level) + 'let ' + api.field(b, 'name') + ' = ' + api.input(b, 'value') + ';'; } },

        { id: 'assignObs', cat: 'data', kind: 'statement', shape: 'stack',
            fields: [{ key: 'obs', type: 'obs', default: '' }],
            inputs: [{ key: 'value', kind: 'expr', defaultExpr: '""' }],
            label: ['设置', { f: 'obs' }, '.value =', { i: 'value' }],
            emit: function (b, api) { return api.indent(api.level) + api.field(b, 'obs') + '.value = ' + api.input(b, 'value') + ';'; } },

        { id: 'if', cat: 'flow', kind: 'statement', shape: 'cblock',
            inputs: [{ key: 'cond', kind: 'bool', defaultExpr: 'true' }],
            inner: [{ key: 'body', label: '执行' }, { key: 'else', label: '否则执行' }],
            label: ['如果', { i: 'cond' }, '则'],
            emit: cblockEmit(function (b, api) {
                return 'if (' + api.input(b, 'cond') + ') [';
            }) },

        { id: 'while', cat: 'flow', kind: 'statement', shape: 'cblock',
            inputs: [{ key: 'cond', kind: 'bool', defaultExpr: 'true' }],
            inner: [{ key: 'body', label: '重复' }],
            label: ['当', { i: 'cond' }, '成立时重复'],
            emit: cblockEmit(function (b, api) {
                return 'while (' + api.input(b, 'cond') + ') [';
            }) },

        { id: 'forCount', cat: 'flow', kind: 'statement', shape: 'cblock',
            fields: [{ key: 'name', type: 'varname', default: 'i' }],
            inputs: [{ key: 'count', kind: 'expr', defaultExpr: '10' }],
            inner: [{ key: 'body', label: '重复' }],
            label: ['计数循环', { f: 'name' }, '从 0 到', { i: 'count' }],
            emit: cblockEmit(function (b, api) {
                var v = api.field(b, 'name');
                return 'for (let ' + v + ' = 0; ' + v + ' < ' + api.input(b, 'count') + '; ' + v + '++) [';
            }) },

        { id: 'forIn', cat: 'flow', kind: 'statement', shape: 'cblock',
            fields: [{ key: 'name', type: 'varname', default: 'item' }],
            inputs: [{ key: 'list', kind: 'expr', defaultExpr: '[]' }],
            inner: [{ key: 'body', label: '遍历' }],
            label: ['遍历', { f: 'name' }, '于', { i: 'list' }],
            emit: cblockEmit(function (b, api) {
                return 'for (' + api.field(b, 'name') + ' in ' + api.input(b, 'list') + ') [';
            }) },

        { id: 'break', cat: 'flow', kind: 'statement', shape: 'stack', label: ['跳出循环'],
            emit: function (b, api) { return api.indent(api.level) + 'break;'; } },

        { id: 'continue', cat: 'flow', kind: 'statement', shape: 'stack', label: ['继续下一轮'],
            emit: function (b, api) { return api.indent(api.level) + 'continue;'; } },

        { id: 'panic', cat: 'flow', kind: 'statement', shape: 'stack',
            inputs: [{ key: 'msg', kind: 'expr', defaultExpr: '""' }],
            label: ['终止脚本', { i: 'msg' }],
            emit: function (b, api) { return api.indent(api.level) + 'panic(' + api.input(b, 'msg') + ');'; } },

        { id: 'text', cat: 'ops', kind: 'expression', shape: 'reporter',
            fields: [{ key: 'value', type: 'text', default: '' }],
            expr: function (b, api) { return quote(api.field(b, 'value')); } },

        { id: 'number', cat: 'ops', kind: 'expression', shape: 'reporter',
            fields: [{ key: 'value', type: 'number', default: 0 }],
            expr: function (b, api) { return numberLiteral(api.field(b, 'value')); } },

        { id: 'bool', cat: 'ops', kind: 'expression', shape: 'reporter',
            fields: [{ key: 'value', type: 'bool', default: false }],
            expr: function (b, api) { return api.field(b, 'value') ? 'true' : 'false'; } },

        { id: 'compare', cat: 'ops', kind: 'expression', shape: 'reporter',
            fields: [{ key: 'op', type: 'select', default: '==', options: COMPARE_OPS }],
            inputs: [{ key: 'a', kind: 'expr', defaultExpr: '""' }, { key: 'b', kind: 'expr', defaultExpr: '""' }],
            expr: function (b, api) { return '(' + api.input(b, 'a') + ' ' + api.field(b, 'op') + ' ' + api.input(b, 'b') + ')'; } },

        { id: 'and', cat: 'ops', kind: 'expression', shape: 'reporter',
            inputs: [{ key: 'a', kind: 'expr', defaultExpr: 'true' }, { key: 'b', kind: 'expr', defaultExpr: 'true' }],
            expr: function (b, api) { return '(' + api.input(b, 'a') + ' && ' + api.input(b, 'b') + ')'; } },

        { id: 'or', cat: 'ops', kind: 'expression', shape: 'reporter',
            inputs: [{ key: 'a', kind: 'expr', defaultExpr: 'true' }, { key: 'b', kind: 'expr', defaultExpr: 'true' }],
            expr: function (b, api) { return '(' + api.input(b, 'a') + ' || ' + api.input(b, 'b') + ')'; } },

        { id: 'not', cat: 'ops', kind: 'expression', shape: 'reporter',
            inputs: [{ key: 'a', kind: 'expr', defaultExpr: 'true' }],
            expr: function (b, api) { return '(!' + api.input(b, 'a') + ')'; } },

        { id: 'concat', cat: 'ops', kind: 'expression', shape: 'reporter',
            inputs: [{ key: 'a', kind: 'expr', defaultExpr: '""' }, { key: 'b', kind: 'expr', defaultExpr: '""' }],
            expr: function (b, api) { return '(' + api.input(b, 'a') + ' + ' + api.input(b, 'b') + ')'; } },

        { id: 'list2', cat: 'ops', kind: 'expression', shape: 'reporter',
            inputs: [{ key: 'a', kind: 'expr', defaultExpr: '""' }, { key: 'b', kind: 'expr', defaultExpr: '""' }],
            expr: function (b, api) { return '[' + api.input(b, 'a') + ', ' + api.input(b, 'b') + ']'; } },

        { id: 'emptyList', cat: 'ops', kind: 'expression', shape: 'reporter',
            expr: function () { return '[]'; } },

        { id: 'request', cat: 'data', kind: 'expression', shape: 'reporter',
            fields: [{ key: 'action', type: 'text', default: 'module.query' }],
            inputs: [{ key: 'args', kind: 'expr', defaultExpr: '[]' }],
            expr: function (b, api) { return 'GUIManager::request(' + quote(api.field(b, 'action')) + ', ' + api.input(b, 'args') + ')'; } },

        { id: 'valueOf', cat: 'data', kind: 'expression', shape: 'reporter',
            fields: [{ key: 'key', type: 'text', default: 'module.key' }],
            expr: function (b, api) { return 'GUIManager::value(' + quote(api.field(b, 'key')) + ')'; } },

        { id: 'obsValue', cat: 'data', kind: 'expression', shape: 'reporter',
            fields: [{ key: 'obs', type: 'obs', default: '' }],
            expr: function (b, api) { return api.field(b, 'obs') + '.value'; } },

        { id: 'obsData', cat: 'data', kind: 'expression', shape: 'reporter',
            fields: [{ key: 'obs', type: 'obs', default: '' }],
            expr: function (b, api) { return api.field(b, 'obs') + '.getData()'; } },

        { id: 'varRef', cat: 'var', kind: 'expression', shape: 'reporter',
            fields: [{ key: 'name', type: 'varname', default: 'x' }],
            expr: function (b, api) { return api.field(b, 'name'); } },

        { id: 'resultSelection', cat: 'data', kind: 'expression', shape: 'reporter',
            expr: function () { return 'result.selection'; } },

        { id: 'resultIndex', cat: 'data', kind: 'expression', shape: 'reporter',
            expr: function () { return 'result.selectionIndex'; } }
    ];

    var DEFS = {};
    LIST.forEach(function (def) { DEFS[def.id] = def; });

    var NAMES = {
        closeForm: '关闭表单', switchTo: '切换表单', openForm: '打开表单', callback: '回传数据',
        setVar: '新建变量', assignObs: '设置可观察', if: '如果', while: '当…重复',
        forCount: '计数循环', forIn: '遍历循环', 'break': '跳出循环', 'continue': '继续下一轮',
        panic: '终止脚本', text: '文本', number: '数字', 'bool': '布尔', compare: '比较',
        and: '与', or: '或', not: '非', concat: '拼接', list2: '列表(2)', emptyList: '空列表',
        request: '请求', valueOf: '取值', obsValue: '可观察值', obsData: '可观察数据',
        varRef: '变量', resultSelection: '选择项', resultIndex: '选择序号'
    };
    LIST.forEach(function (def) { if (!def.name) def.name = NAMES[def.id] || def.id; });

    function defOf(id) { return DEFS[id] || null; }

    function findField(def, key) {
        return (def.fields || []).filter(function (f) { return f.key === key; })[0] || null;
    }

    function findInput(def, key) {
        return (def.inputs || []).filter(function (i) { return i.key === key; })[0] || null;
    }

    function defaultField(defId, key) {
        var f = findField(defOf(defId), key);
        return f ? f.default : '';
    }

    var uidCounter = 0;
    function uid() { return 'k' + (++uidCounter).toString(36) + Date.now().toString(36).slice(-4); }

    function make(defId) {
        var def = defOf(defId);
        if (!def) return null;
        var block = { uid: uid(), def: defId, fields: {}, inputs: {}, inner: {} };
        (def.fields || []).forEach(function (f) { block.fields[f.key] = f.default; });
        (def.inputs || []).forEach(function (i) { block.inputs[i.key] = null; });
        (def.inner || []).forEach(function (s) { block.inner[s.key] = []; });
        return block;
    }

    function clone(tree) { return JSON.parse(JSON.stringify(tree)); }

    function workspace() { return { blocks: [] }; }

    function eachBlock(block, fn) {
        if (!block) return;
        fn(block);
        var def = defOf(block.def);
        (def ? (def.inputs || []) : []).forEach(function (i) { eachBlock(block.inputs[i.key], fn); });
        (def ? (def.inner || []) : []).forEach(function (s) {
            (block.inner[s.key] || []).forEach(function (c) { eachBlock(c, fn); });
        });
    }

    function find(ws, id) {
        var found = null;
        (ws && ws.blocks ? ws.blocks : []).forEach(function (b) { eachBlock(b, function (x) { if (x.uid === id) found = x; }); });
        return found;
    }

    function locate(ws, id) {
        var root = ws && ws.blocks ? ws.blocks : [];
        var hit = null;
        function scan(list, owner, socket) {
            for (var i = 0; i < list.length; ++i) {
                if (list[i].uid === id) { hit = { array: list, index: i, owner: owner, socket: socket }; return true; }
                var def = defOf(list[i].def);
                if (def) (def.inner || []).forEach(function (s) {
                    if (scan(list[i].inner[s.key] || [], list[i], s.key)) return true;
                });
                if (hit) return true;
            }
            return !!hit;
        }
        scan(root, null, null);
        return hit;
    }

    function detach(ws, id) {
        var loc = locate(ws, id);
        if (!loc) return null;
        return loc.array.splice(loc.index, 1)[0];
    }

    // 从任意位置（语句数组或表达式槽，支持任意嵌套）移除并返回该块。
    function remove(ws, id) {
        var result = null;

        function clearSlot(block, key) {
            var slot = block.inputs[key];
            if (slot && slot.uid === id) { result = slot; block.inputs[key] = null; return true; }
            if (slot && clearFromInputs(slot)) return true;
            return false;
        }

        function clearFromInputs(block) {
            var def = defOf(block.def);
            if (!def) return false;
            var inputs = def.inputs || [];
            for (var k = 0; k < inputs.length; k++) {
                if (clearSlot(block, inputs[k].key)) return true;
            }
            return false;
        }

        function descend(list) {
            for (var i = 0; i < list.length; i++) {
                if (list[i].uid === id) { result = list.splice(i, 1)[0]; return true; }
                if (clearFromInputs(list[i])) return true;
                var def = defOf(list[i].def);
                if (def) {
                    var inner = def.inner || [];
                    for (var s = 0; s < inner.length; s++) {
                        if (descend(list[i].inner[inner[s].key] || [])) return true;
                    }
                }
            }
            return false;
        }

        descend(ws && ws.blocks ? ws.blocks : []);
        return result;
    }

    function insert(ws, target, position, block) {
        if (position === 'root') { ws.blocks.push(block); return true; }
        if (position.indexOf('after:') === 0) {
            var id = position.slice(6);
            var loc = locate(ws, id);
            if (!loc) return false;
            loc.array.splice(loc.index + 1, 0, block);
            return true;
        }
        if (position.indexOf('inner:') === 0) {
            var rest = position.slice(6).split(':');
            var owner = find(ws, rest[0]);
            if (!owner || !owner.inner[rest[1]]) return false;
            owner.inner[rest[1]].push(block);
            return true;
        }
        return false;
    }

    function isDescendant(ws, ancestorId, maybeChildId) {
        var node = find(ws, ancestorId);
        if (!node) return false;
        var yes = false;
        eachBlock(node, function (x) { if (x.uid === maybeChildId) yes = true; });
        return yes;
    }

    function apiFor(level, ctx) {
        return {
            level: level,
            indent: function (l) { return '    '.repeat(l); },
            stmts: function (list, l) { return stmts(list || [], l); },
            field: function (block, key) {
                var v = block.fields ? block.fields[key] : undefined;
                return v == null ? defaultField(block.def, key) : v;
            },
            input: function (block, key) {
                var slot = block.inputs ? block.inputs[key] : null;
                if (slot) return exprOf(slot);
                var spec = findInput(defOf(block.def), key);
                return spec && spec.defaultExpr != null ? spec.defaultExpr : '""';
            },
            quote: quote
        };
    }

    function exprOf(block) {
        if (!block) return '""';
        var def = defOf(block.def);
        if (!def || !def.expr) return '""';
        return def.expr(block, apiFor(0, null));
    }

    function socketLines(api, list) {
        if (!list || !list.length) return '';
        return api.stmts(list, api.level + 1);
    }

    function cblockEmit(build) {
        return function (b, api) {
            var body = socketLines(api, b.inner ? b.inner.body : null);
            var alt = b.inner && b.inner.else ? socketLines(api, b.inner.else) : '';
            var lines = [api.indent(api.level) + build(b, api)];
            if (body) lines.push(body);
            lines.push(api.indent(api.level) + ']');
            if (alt) {
                lines.push(api.indent(api.level) + ' : [');
                lines.push(alt);
                lines.push(api.indent(api.level) + ']');
            }
            return lines.join('\n');
        };
    }

    function stmt(block, level) {
        var def = defOf(block.def);
        if (!def || !def.emit) return '';
        return def.emit(block, apiFor(level, null));
    }

    function stmts(list, level) {
        return (list || []).map(function (b) { return stmt(b, level); }).join('\n');
    }

    function generate(ws, ctx) {
        ctx = ctx || {};
        var level = ctx.indent == null ? 1 : ctx.indent;
        return stmts(ws && ws.blocks ? ws.blocks : [], level);
    }

    function serialize(ws) { return JSON.parse(JSON.stringify(ws)); }

    function deserialize(raw) {
        if (!raw || !raw.blocks) return workspace();
        return { blocks: raw.blocks };
    }

    function observableNames(document) {
        var prefix = Model.isIdentifier(document.properties.variable) ? document.properties.variable : 'form';
        var ordinals = {};
        var names = [];
        (document.controls || []).forEach(function (control) {
            var d = Schema.findControl(control.kind);
            if (d && d.observable) {
                ordinals[d.kind] = (ordinals[d.kind] || 0) + 1;
                names.push(prefix + '_' + d.name + '_' + ordinals[d.kind] + '_value');
            }
        });
        return names;
    }

    function declaredVars(ws) {
        var names = [];
        (ws && ws.blocks ? ws.blocks : []).forEach(function (b) {
            eachBlock(b, function (x) {
                if (x.def === 'setVar') names.push(String(x.fields.name));
            });
        });
        return names;
    }

    return {
        categories: CATS,
        defs: DEFS,
        list: LIST,
        make: make,
        clone: clone,
        workspace: workspace,
        find: find,
        locate: locate,
        detach: detach,
        remove: remove,
        insert: insert,
        isDescendant: isDescendant,
        generate: generate,
        serialize: serialize,
        deserialize: deserialize,
        observableNames: observableNames,
        declaredVars: declaredVars,
        eachBlock: eachBlock
    };
})();
