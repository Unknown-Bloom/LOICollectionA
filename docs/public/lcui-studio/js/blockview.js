var LCUI = globalThis.LCUI = globalThis.LCUI || {};

LCUI.BlockView = (function () {
    'use strict';

    var Blocks = LCUI.Blocks;

    function el(tag, cls, text) {
        var n = document.createElement(tag);
        if (cls) n.className = cls;
        if (text !== undefined && text !== null) n.textContent = text;
        return n;
    }

    function option(value, label, selected) {
        var o = el('option', null, label);
        o.value = value;
        if (selected) o.selected = true;
        return o;
    }

    var SOCKET_LABELS = {
        cond: '条件', params: '参数', args: '参数', value: '值', a: '值', b: '值',
        msg: '消息', list: '列表', count: '次数'
    };

    var s = {
        panel: null, palette: null, workspace: null, titleEl: null, subEl: null, bar: null,
        getWorkspace: null, getContext: null, onCommit: null,
        selected: null, justAdded: null, drag: null, keyHandler: null
    };

    function ws() { return s.getWorkspace ? s.getWorkspace() : null; }
    function ctx() {
        return s.getContext ? s.getContext() : { observables: [], variables: [], formVariable: 'form' };
    }

    function show(opts) {
        s.panel = opts.panel;
        s.palette = opts.palette;
        s.workspace = opts.workspace;
        s.titleEl = opts.titleEl;
        s.subEl = opts.subEl;
        s.bar = opts.bar;
        s.getWorkspace = opts.getWorkspace;
        s.getContext = opts.getContext;
        s.onCommit = opts.onCommit;
        s.selected = null;
        s.justAdded = null;
        s.drag = null;
        renderAll();
        if (!s.keyHandler) {
            s.keyHandler = function (e) {
                if (!s.panel || s.panel.hidden) return;
                if (e.key !== 'Delete' && e.key !== 'Backspace') return;
                var active = document.activeElement;
                if (active && /INPUT|TEXTAREA|SELECT/.test(active.tagName)) return;
                if (!s.selected) return;
                e.preventDefault();
                deleteSelected();
            };
            document.addEventListener('keydown', s.keyHandler);
        }
    }

    function hide() {
        if (s.keyHandler) { document.removeEventListener('keydown', s.keyHandler); s.keyHandler = null; }
        s.drag = null;
        s.selected = null;
    }

    function selectedUid() { return s.selected; }

    function commit(justUid) {
        s.justAdded = justUid || null;
        if (s.onCommit) s.onCommit();
        renderWorkspace();
        renderBar();
    }

    function contextHint() {
        var c = ctx();
        var parts = [];
        if (c.formVariable) parts.push('脚本变量 ' + c.formVariable);
        if (c.observables && c.observables.length) parts.push('可观察：' + c.observables.join('、'));
        if (c.variables && c.variables.length) parts.push('局部变量：' + c.variables.join('、'));
        return parts.length ? '可用：' + parts.join('  ·  ') : '暂无可用上下文，可先用「新建变量」声明。';
    }

    function renderAll() {
        renderTitle();
        renderPalette();
        renderWorkspace();
        renderBar();
    }

    function renderTitle() {
        if (!s.titleEl) return;
        var c = ctx();
        s.titleEl.textContent = '回调逻辑';
        if (s.subEl) s.subEl.textContent = c.hint || '';
    }

    function renderPalette() {
        var host = s.palette;
        if (!host) return;
        host.textContent = '';
        Blocks.categories.forEach(function (cat) {
            var defs = Blocks.list.filter(function (d) { return d.cat === cat.id; });
            if (!defs.length) return;
            host.appendChild(el('div', 'bgroup-title', cat.label));
            var grid = el('div', 'bgroup');
            defs.forEach(function (def) {
                var chip = el('div', 'bchip bchip-cat-' + cat.id, def.name);
                chip.setAttribute('draggable', 'true');
                chip.title = def.name;
                chip.addEventListener('dragstart', function (e) { startDrag(e, def.id, null, def.kind); });
                chip.addEventListener('dragend', endDrag);
                chip.addEventListener('click', function () { tapAdd(def.id); });
                grid.appendChild(chip);
            });
            host.appendChild(grid);
        });
    }

    function renderWorkspace() {
        var host = s.workspace;
        if (!host) return;
        host.textContent = '';
        var w = ws();
        if (!w || !w.blocks || !w.blocks.length) {
            var dz = el('div', 'block-dropzone', '把积木拖到这里，或点击左侧积木添加');
            dz.addEventListener('dragover', function (e) {
                if (!s.drag || s.drag.kind !== 'statement') return;
                e.preventDefault();
                dz.classList.add('hot');
            });
            dz.addEventListener('dragleave', function () { dz.classList.remove('hot'); });
            dz.addEventListener('drop', function (e) { dz.classList.remove('hot'); dropStatement(e, 'root'); });
            host.appendChild(dz);
            host.appendChild(el('div', 'block-ctx', contextHint()));
            return;
        }
        renderList(host, w.blocks, 'root');
    }

    function renderList(host, list, position) {
        addGap(host, position);
        list.forEach(function (block) {
            host.appendChild(renderBlock(block));
            addGap(host, 'after:' + block.uid);
        });
        addGap(host, position);
    }

    function addGap(host, pos) {
        var g = el('div', 'block-gap');
        g.dataset.pos = pos;
        g.addEventListener('dragover', function (e) {
            if (!s.drag || s.drag.kind !== 'statement') return;
            e.preventDefault();
            g.classList.add('hot');
        });
        g.addEventListener('dragleave', function () { g.classList.remove('hot'); });
        g.addEventListener('drop', function (e) { g.classList.remove('hot'); dropStatement(e, pos); });
        host.appendChild(g);
    }

    function renderBlock(block) {
        var def = Blocks.defs[block.def];
        if (!def) return el('div', 'block');
        var node = el('div', 'block block-cat-' + def.cat + (block.uid === s.selected ? ' selected' : ''));
        node.dataset.uid = block.uid;
        node.draggable = true;

        var head = el('div', 'block-head');
        (def.label || []).forEach(function (part) {
            if (typeof part === 'string') { head.appendChild(el('span', 'block-text', part)); return; }
            if (part.f) { head.appendChild(renderField(block, def, part.f)); return; }
            if (part.i) { head.appendChild(renderSocket(block, def, part.i)); return; }
        });
        node.appendChild(head);

        if (def.shape === 'cblock') {
            var body = el('div', 'block-body');
            (def.inner || []).forEach(function (slot) {
                var sub = el('div', 'block-slot');
                sub.appendChild(el('span', 'block-slot-label', slot.label || slot.key));
                renderList(sub, block.inner[slot.key] || [], 'inner:' + block.uid + ':' + slot.key);
                body.appendChild(sub);
            });
            node.appendChild(body);
        }

        node.addEventListener('click', function (e) {
            if (e.target.closest('.block-input, .block-socket, .block-btn, select, input, button')) return;
            selectBlock(block.uid);
        });
        node.addEventListener('dragstart', function (e) {
            if (e.target.closest('.block-input, .block-socket, select, input, button')) { e.preventDefault(); return; }
            startDrag(e, block.def, block.uid, def.kind);
        });
        node.addEventListener('dragend', endDrag);
        if (block.uid === s.justAdded) node.classList.add('block-enter');
        return node;
    }

    function renderField(block, def, key) {
        var fd = (def.fields || []).filter(function (f) { return f.key === key; })[0] || { type: 'text' };
        var type = fd.type;
        var wrap = el('span', 'block-input block-input-' + type);
        var value = block.fields[key];

        if (type === 'select') {
            var sel = el('select', 'block-select');
            (fd.options || []).forEach(function (o) { sel.appendChild(option(o, o, String(o) === String(value))); });
            sel.addEventListener('click', stop);
            sel.addEventListener('change', function () { block.fields[key] = sel.value; commit(); });
            wrap.appendChild(sel);
        } else if (type === 'obs') {
            var opts = ctx().observables || [];
            if (!opts.length) {
                var ti = textInput(value, '可观察对象', type === 'number');
                ti.addEventListener('change', function () { block.fields[key] = ti.value; commit(); });
                wrap.appendChild(ti);
            } else {
                var so = el('select', 'block-select');
                so.appendChild(option('', '（选择可观察对象）', !value));
                opts.forEach(function (o) { so.appendChild(option(o, o, o === value)); });
                so.addEventListener('click', stop);
                so.addEventListener('change', function () { block.fields[key] = so.value; commit(); });
                wrap.appendChild(so);
            }
        } else if (type === 'bool') {
            var sb = el('select', 'block-select');
            sb.appendChild(option('true', '真', String(value) === 'true'));
            sb.appendChild(option('false', '假', String(value) === 'false'));
            sb.addEventListener('click', stop);
            sb.addEventListener('change', function () { block.fields[key] = sb.value === 'true'; commit(); });
            wrap.appendChild(sb);
        } else {
            var t = textInput(value, type === 'number' ? '' : '', type === 'number');
            if (fd && fd.step) t.setAttribute('step', String(fd.step));
            t.addEventListener('change', function () {
                block.fields[key] = type === 'number' ? (t.value === '' ? '' : Number(t.value)) : t.value;
                commit();
            });
            wrap.appendChild(t);
        }
        return wrap;
    }

    function textInput(value, placeholder, isNumber) {
        var t = el('input', 'block-field');
        t.type = isNumber ? 'number' : 'text';
        t.value = (value === undefined || value === null) ? '' : value;
        t.spellcheck = false;
        if (placeholder) t.placeholder = placeholder;
        t.addEventListener('click', stop);
        t.addEventListener('mousedown', stop);
        return t;
    }

    function renderSocket(block, def, key) {
        var socket = el('span', 'block-socket');
        socket.addEventListener('dragover', function (e) {
            if (!s.drag || s.drag.kind !== 'expression') return;
            e.preventDefault();
            socket.classList.add('hot');
        });
        socket.addEventListener('dragleave', function () { socket.classList.remove('hot'); });
        socket.addEventListener('drop', function (e) { socket.classList.remove('hot'); dropExpression(e, block.uid, key); });

        var current = block.inputs ? block.inputs[key] : null;
        if (current) {
            socket.appendChild(renderBlock(current));
            var clr = el('button', 'block-btn block-clear', '✕');
            clr.title = '清空';
            clr.addEventListener('click', function (e) { e.stopPropagation(); block.inputs[key] = null; commit(); });
            socket.appendChild(clr);
        } else {
            socket.appendChild(el('span', 'socket-ph', SOCKET_LABELS[key] || '值'));
        }
        return socket;
    }

    function stop(e) { e.stopPropagation(); }

    function startDrag(e, defId, uid, kind) {
        s.drag = { defId: defId, uid: uid, kind: kind };
        try { e.dataTransfer.setData('text/plain', uid || defId); } catch (_) {}
        e.dataTransfer.effectAllowed = uid ? 'move' : 'copy';
    }

    function endDrag() { s.drag = null; }

    function consumeDrag() { var d = s.drag; s.drag = null; return d; }

    function tapAdd(defId) {
        var def = Blocks.defs[defId];
        if (!def) return;
        var w = ws();
        if (!w) return;
        var block = Blocks.make(defId);
        if (s.selected) {
            var sel = Blocks.find(w, s.selected);
            if (sel) {
                var sdef = Blocks.defs[sel.def];
                if (sdef && sdef.shape === 'cblock' && sdef.inner && sdef.inner[0]) {
                    var slot = sdef.inner[0].key;
                    (sel.inner[slot] = sel.inner[slot] || []).push(block);
                    commit(block.uid);
                    return;
                }
            }
        }
        w.blocks.push(block);
        commit(block.uid);
    }

    function dropStatement(e, pos) {
        e.preventDefault();
        var d = consumeDrag();
        if (!d || d.kind !== 'statement') return;
        var w = ws();
        var block = d.uid ? Blocks.remove(w, d.uid) : Blocks.make(d.defId);
        if (!block) return;
        if (!Blocks.insert(w, pos, block)) {
            if (d.uid) Blocks.insert(w, 'root', block);
            return;
        }
        commit(block.uid);
    }

    function dropExpression(e, ownerUid, key) {
        e.preventDefault();
        var d = consumeDrag();
        if (!d || d.kind !== 'expression') return;
        var w = ws();
        var owner = Blocks.find(w, ownerUid);
        if (!owner) return;
        if (d.uid && ownerUid === d.uid) return;
        var block = d.uid ? Blocks.remove(w, d.uid) : Blocks.make(d.defId);
        if (!block) return;
        if (d.uid && Blocks.isDescendant(w, d.uid, ownerUid)) {
            Blocks.insert(w, 'root', block);
            return;
        }
        owner.inputs[key] = block;
        commit(block.uid);
    }

    function selectBlock(uid) {
        s.selected = uid;
        renderWorkspace();
        renderBar();
    }

    function deleteSelected() {
        if (!s.selected) return;
        var w = ws();
        Blocks.remove(w, s.selected);
        s.selected = null;
        commit();
    }

    function moveBlock(uid, dir) {
        var loc = Blocks.locate(ws(), uid);
        if (!loc) return;
        var arr = loc.array, i = loc.index, block = arr[i];
        if (dir < 0 && i > 0) { arr.splice(i, 1); arr.splice(i - 1, 0, block); commit(); }
        else if (dir > 0 && i < arr.length - 1) { arr.splice(i, 1); arr.splice(i + 1, 0, block); commit(); }
    }

    function indentBlock(uid) {
        var loc = Blocks.locate(ws(), uid);
        if (!loc || loc.index <= 0) return;
        var prev = loc.array[loc.index - 1];
        var pdef = Blocks.defs[prev.def];
        if (!pdef || pdef.shape !== 'cblock' || !pdef.inner || !pdef.inner[0]) return;
        var block = loc.array.splice(loc.index, 1)[0];
        var slot = pdef.inner[0].key;
        (prev.inner[slot] = prev.inner[slot] || []).push(block);
        commit();
    }

    function outdentBlock(uid) {
        var loc = Blocks.locate(ws(), uid);
        if (!loc || !loc.owner || !loc.socket) return;
        var block = loc.array.splice(loc.index, 1)[0];
        Blocks.insert(ws(), 'after:' + loc.owner.uid, block);
        commit();
    }

    function toolBtn(label, fn, cls) {
        var b = el('button', 'tool wide' + (cls ? ' ' + cls : ''), label);
        b.addEventListener('click', function (e) { e.stopPropagation(); fn(); });
        return b;
    }

    function renderBar() {
        var bar = s.bar;
        if (!bar) return;
        bar.textContent = '';
        if (!s.selected) { bar.hidden = true; return; }
        var block = Blocks.find(ws(), s.selected);
        if (!block) { s.selected = null; bar.hidden = true; return; }
        var def = Blocks.defs[block.def];
        bar.hidden = false;
        bar.appendChild(el('span', 'block-bar-title', def.name));
        if (def.kind === 'statement') {
            bar.appendChild(toolBtn('复制', function () {
                var c = Blocks.clone(block);
                if (Blocks.insert(ws(), 'after:' + block.uid, c)) { s.selected = c.uid; commit(c.uid); }
            }));
            bar.appendChild(toolBtn('上移', function () { moveBlock(block.uid, -1); }));
            bar.appendChild(toolBtn('下移', function () { moveBlock(block.uid, 1); }));
            bar.appendChild(toolBtn('缩进', function () { indentBlock(block.uid); }));
            bar.appendChild(toolBtn('出缩', function () { outdentBlock(block.uid); }));
        }
        bar.appendChild(toolBtn('删除', function () { deleteSelected(); }, 'danger'));
    }

    return {
        show: show,
        hide: hide,
        selectedUid: selectedUid,
        deleteSelected: deleteSelected
    };
})();
