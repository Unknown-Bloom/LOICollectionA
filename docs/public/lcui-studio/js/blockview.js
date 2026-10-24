var LCUI = globalThis.LCUI = globalThis.LCUI || {};

LCUI.BlockView = (function () {
    'use strict';

    var Blocks = LCUI.Blocks;

    var HOLD = 240;
    var SLOP = 5;
    var SOCKET_REACH = 28;
    var GAP_REACH = 34;
    var SCROLL_EDGE = 48;
    var SCROLL_STEP = 14;

    var SOCKET_LABELS = {
        cond: '条件', params: '参数', args: '参数', value: '值', a: '值', b: '值',
        msg: '消息', list: '列表', count: '次数'
    };

    var s = {
        panel: null, palette: null, workspace: null, titleEl: null, subEl: null, bar: null,
        getWorkspace: null, getContext: null, onCommit: null,
        selected: null, justAdded: null, press: null, drag: null,
        keyHandler: null, scrollFrame: null, contextHandler: null, snapshot: null
    };

    function ws() { return s.getWorkspace ? s.getWorkspace() : null; }

    function ctx() {
        return s.getContext ? s.getContext() : { observables: [], variables: [], formVariable: 'form' };
    }

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

    function stop(e) { e.stopPropagation(); }

    function blockTouchMove(e) {
        if (s.drag) e.preventDefault();
    }

    function show(opts) {
        var keep = s.panel === opts.panel && s.workspace === opts.workspace ? s.selected : null;
        cancelPress();
        cleanupDrag();
        detachPointer();
        document.body.classList.add('logic-open');
        s.panel = opts.panel;
        s.palette = opts.palette;
        s.workspace = opts.workspace;
        s.titleEl = opts.titleEl;
        s.subEl = opts.subEl;
        s.bar = opts.bar;
        s.getWorkspace = opts.getWorkspace;
        s.getContext = opts.getContext;
        s.onCommit = opts.onCommit;
        var w = ws();
        s.selected = keep && w && Blocks.find(w, keep) ? keep : null;
        s.snapshot = w ? Blocks.serialize(w) : null;
        if (!s.selected) s.justAdded = null;
        renderAll();
        bindKeys();
    }

    function hide() {
        cancelPress();
        cleanupDrag();
        detachPointer();
        document.body.classList.remove('logic-open');
        if (s.keyHandler) { document.removeEventListener('keydown', s.keyHandler); s.keyHandler = null; }
        if (s.contextHandler) { document.removeEventListener('contextmenu', s.contextHandler); s.contextHandler = null; }
        s.panel = null;
        s.workspace = null;
        s.palette = null;
        s.bar = null;
        s.selected = null;
        s.justAdded = null;
    }

    function bindKeys() {
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
        if (!s.contextHandler) {
            s.contextHandler = function (e) { if (s.drag) e.preventDefault(); };
            document.addEventListener('contextmenu', s.contextHandler);
        }
    }

    function commit(justUid) {
        var baseline = s.snapshot;
        s.justAdded = justUid || null;
        if (s.onCommit) s.onCommit(baseline);
        var w = ws();
        s.snapshot = w ? Blocks.serialize(w) : null;
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
        if (s.titleEl && !s.titleEl.textContent) s.titleEl.textContent = '回调逻辑';
        if (s.subEl && !s.subEl.textContent) s.subEl.textContent = ctx().hint || '';
    }

    function renderPalette() {
        var host = s.palette;
        if (!host) return;
        var top = host.scrollTop;
        host.textContent = '';
        Blocks.categories.forEach(function (cat) {
            var defs = Blocks.list.filter(function (d) { return d.cat === cat.id; });
            if (!defs.length) return;
            host.appendChild(el('div', 'bgroup-title', cat.label));
            var grid = el('div', 'bgroup');
            defs.forEach(function (def) {
                var chip = el('div', 'bchip bchip-cat-' + cat.id, def.name);
                chip.tabIndex = 0;
                chip.title = def.name;
                chip.addEventListener('pointerdown', function (e) { beginPress(e, { defId: def.id, kind: def.kind }, chip); });
                chip.addEventListener('keydown', function (e) {
                    if (e.key !== 'Enter' && e.key !== ' ') return;
                    e.preventDefault();
                    tapAdd(def.id);
                });
                grid.appendChild(chip);
            });
            host.appendChild(grid);
        });
        host.scrollTop = top;
    }

    function renderWorkspace() {
        var host = s.workspace;
        if (!host) return;
        var top = host.scrollTop;
        var w = ws();
        host.textContent = '';
        if (!w) return;
        if (!w.blocks.length) {
            var dz = el('div', 'block-dropzone', '把积木拖到这里，或点击左侧积木添加');
            dz.dataset.pos = 'root:0';
            host.appendChild(dz);
        } else {
            var stack = el('div', 'block-stack');
            renderList(stack, w.blocks, function (i) { return 'root:' + i; });
            host.appendChild(stack);
        }
        host.appendChild(el('div', 'block-ctx', contextHint()));
        host.scrollTop = top;
    }

    function renderList(host, list, positionAt) {
        var empty = !list.length;
        addGap(host, positionAt(0), empty);
        list.forEach(function (block, index) {
            host.appendChild(renderBlock(block));
            addGap(host, positionAt(index + 1), false);
        });
    }

    function addGap(host, position, empty) {
        var gap = el('div', 'block-gap' + (empty ? ' block-gap-empty' : ''));
        gap.dataset.pos = position;
        host.appendChild(gap);
    }

    function renderBlock(block) {
        var def = Blocks.defs[block.def];
        if (!def) return el('div', 'block');
        var reporter = def.kind === 'expression';
        var node = el('div', 'block ' + (reporter ? 'block-reporter' : 'block-statement') +
            ' block-cat-' + def.cat + (block.uid === s.selected ? ' selected' : ''));
        node.dataset.uid = block.uid;
        node.dataset.kind = def.kind;

        var head = el('div', 'block-head');
        (def.label || []).forEach(function (part) {
            if (typeof part === 'string') { head.appendChild(el('span', 'block-text', part)); return; }
            if (part.f) { head.appendChild(renderField(block, def, part.f)); return; }
            if (part.i) { head.appendChild(renderSocket(block, def, part.i)); }
        });
        node.appendChild(head);

        if (def.shape === 'cblock') {
            var body = el('div', 'block-body');
            (def.inner || []).forEach(function (slot) {
                var sub = el('div', 'block-slot');
                sub.appendChild(el('span', 'block-slot-label', slot.label || slot.key));
                renderList(sub, block.inner[slot.key] || [], function (i) {
                    return 'inner:' + block.uid + ':' + slot.key + ':' + i;
                });
                body.appendChild(sub);
            });
            node.appendChild(body);
        }

        node.addEventListener('pointerdown', function (e) {
            if (e.target.closest('.block-input, .block-btn, select, input, button, textarea')) return;
            e.stopPropagation();
            beginPress(e, { uid: block.uid, defId: block.def, kind: def.kind }, node);
        });
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
            sel.addEventListener('pointerdown', stop);
            sel.addEventListener('click', stop);
            sel.addEventListener('change', function () { block.fields[key] = sel.value; commit(); });
            wrap.appendChild(sel);
        } else if (type === 'obs') {
            var opts = ctx().observables || [];
            if (!opts.length) {
                var ti = textInput(value, '可观察对象', false);
                ti.addEventListener('change', function () { block.fields[key] = ti.value; commit(); });
                wrap.appendChild(ti);
            } else {
                var so = el('select', 'block-select');
                so.appendChild(option('', '（选择可观察对象）', !value));
                opts.forEach(function (o) { so.appendChild(option(o, o, o === value)); });
                so.addEventListener('pointerdown', stop);
                so.addEventListener('click', stop);
                so.addEventListener('change', function () { block.fields[key] = so.value; commit(); });
                wrap.appendChild(so);
            }
        } else if (type === 'bool') {
            var sb = el('select', 'block-select');
            sb.appendChild(option('true', '真', String(value) === 'true'));
            sb.appendChild(option('false', '假', String(value) === 'false'));
            sb.addEventListener('pointerdown', stop);
            sb.addEventListener('click', stop);
            sb.addEventListener('change', function () { block.fields[key] = sb.value === 'true'; commit(); });
            wrap.appendChild(sb);
        } else {
            var t = textInput(value, '', type === 'number');
            if (fd.step) t.setAttribute('step', String(fd.step));
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
        t.addEventListener('pointerdown', stop);
        t.addEventListener('mousedown', stop);
        t.addEventListener('click', stop);
        return t;
    }

    function renderSocket(block, def, key) {
        var socket = el('span', 'block-socket' + (block.inputs[key] ? '' : ' block-socket-empty'));
        socket.dataset.owner = block.uid;
        socket.dataset.key = key;
        var current = block.inputs ? block.inputs[key] : null;
        if (current) {
            socket.appendChild(renderBlock(current));
            var clear = el('button', 'block-btn block-clear', '✕');
            clear.title = '清空';
            clear.addEventListener('pointerdown', stop);
            clear.addEventListener('click', function (e) {
                e.stopPropagation();
                block.inputs[key] = null;
                commit();
            });
            socket.appendChild(clear);
        } else {
            socket.appendChild(el('span', 'socket-ph', SOCKET_LABELS[key] || '值'));
        }
        return socket;
    }

    function beginPress(e, payload, node) {
        if (e.pointerType === 'mouse' && e.button !== 0) return;
        if (s.press || s.drag) return;
        var press = {
            payload: payload, node: node, pointerId: e.pointerId,
            startX: e.clientX, startY: e.clientY, x: e.clientX, y: e.clientY,
            touch: e.pointerType !== 'mouse', moved: false, timer: null
        };
        s.press = press;
        if (press.touch) {
            press.timer = setTimeout(function () {
                if (s.press === press && !press.moved) startDrag(press, press.x, press.y);
            }, HOLD);
        }
        document.addEventListener('pointermove', onPointerMove);
        document.addEventListener('pointerup', onPointerUp);
        document.addEventListener('pointercancel', onPointerCancel);
        if (node.setPointerCapture) {
            try { node.setPointerCapture(e.pointerId); } catch (_) {}
        }
    }

    function onPointerMove(e) {
        if (s.drag) {
            if (e.pointerId !== s.drag.pointerId) return;
            e.preventDefault();
            updateDragPosition(e.clientX, e.clientY);
            return;
        }
        var press = s.press;
        if (!press || e.pointerId !== press.pointerId) return;
        press.x = e.clientX;
        press.y = e.clientY;
        if (press.moved || Math.abs(e.clientX - press.startX) + Math.abs(e.clientY - press.startY) <= SLOP) return;
        press.moved = true;
        if (press.touch) { cancelPress(); return; }
        startDrag(press, e.clientX, e.clientY);
    }

    function onPointerUp(e) {
        if (s.drag) {
            if (e.pointerId !== s.drag.pointerId) return;
            cancelPress();
            finishDrop();
            return;
        }
        var press = s.press;
        if (!press || e.pointerId !== press.pointerId) { detachPointer(); return; }
        var tapped = !press.moved;
        if (press.timer) clearTimeout(press.timer);
        cancelPress();
        detachPointer();
        if (!tapped) return;
        if (press.payload.uid) selectBlock(press.payload.uid);
        else tapAdd(press.payload.defId);
    }

    function onPointerCancel() {
        cancelPress();
        cleanupDrag();
        detachPointer();
    }

    function detachPointer() {
        document.removeEventListener('pointermove', onPointerMove);
        document.removeEventListener('pointerup', onPointerUp);
        document.removeEventListener('pointercancel', onPointerCancel);
    }

    function cancelPress() {
        var press = s.press;
        if (!press) return;
        if (press.timer) clearTimeout(press.timer);
        releaseCapture(press);
        s.press = null;
    }

    function releaseCapture(target) {
        if (!target.node || !target.node.releasePointerCapture) return;
        try { target.node.releasePointerCapture(target.pointerId); } catch (_) {}
    }

    function startDrag(press, x, y) {
        if (s.drag) return;
        if (press.timer) { clearTimeout(press.timer); press.timer = null; }
        var rect = press.node.getBoundingClientRect();
        var ghost = buildGhost(press.payload, press.node, rect);
        var offsetX = Math.max(10, Math.min(x - rect.left, Math.max(10, rect.width - 10)));
        var offsetY = Math.max(10, Math.min(y - rect.top, Math.max(10, rect.height - 10)));
        document.body.appendChild(ghost);
        press.node.classList.add('dragging');
        document.body.classList.add('block-dragging');
        document.addEventListener('touchmove', blockTouchMove, { passive: false });
        s.drag = {
            payload: press.payload, node: press.node, pointerId: press.pointerId,
            ghost: ghost, offsetX: offsetX, offsetY: offsetY,
            x: x, y: y, target: null
        };
        moveGhost(x, y);
        updateTarget(x, y);
        s.scrollFrame = requestAnimationFrame(autoScroll);
    }

    function buildGhost(payload, node, rect) {
        var ghost;
        if (payload.uid) {
            ghost = node.cloneNode(true);
            if (rect.width) ghost.style.width = rect.width + 'px';
        } else {
            var def = Blocks.defs[payload.defId];
            ghost = el('div', 'block ' + (def.kind === 'expression' ? 'block-reporter' : 'block-statement') +
                ' block-cat-' + def.cat, def.name || payload.defId);
            ghost.style.minWidth = '120px';
        }
        ghost.classList.add('block-ghost');
        ghost.classList.remove('selected', 'dragging', 'block-enter');
        ghost.removeAttribute('data-uid');
        ghost.querySelectorAll('[data-uid]').forEach(function (child) { child.removeAttribute('data-uid'); });
        return ghost;
    }

    function moveGhost(x, y) {
        var drag = s.drag;
        drag.ghost.style.left = (x - drag.offsetX) + 'px';
        drag.ghost.style.top = (y - drag.offsetY) + 'px';
    }

    function updateDragPosition(x, y) {
        if (!s.drag) return;
        s.drag.x = x;
        s.drag.y = y;
        moveGhost(x, y);
        updateTarget(x, y);
    }

    function updateTarget(x, y) {
        var drag = s.drag;
        var target = findTarget(x, y, drag.payload.kind, drag.node);
        if (drag.target && target && drag.target.el === target.el) return;
        clearHot();
        drag.target = target;
        if (target) target.el.classList.add('hot');
    }

    function findTarget(x, y, kind, source) {
        var host = s.workspace;
        if (!host) return null;
        var rect = host.getBoundingClientRect();
        if (x < rect.left || x > rect.right || y < rect.top || y > rect.bottom) return null;
        if (kind === 'expression') {
            var socket = nearest(host.querySelectorAll('.block-socket'), x, y, SOCKET_REACH, source);
            return socket ? { type: 'input', el: socket, owner: socket.dataset.owner, key: socket.dataset.key } : null;
        }
        var gap = nearest(host.querySelectorAll('[data-pos]'), x, y, GAP_REACH, source);
        return gap ? { type: 'gap', el: gap, pos: gap.dataset.pos } : null;
    }

    function nearest(nodes, x, y, reach, source) {
        var best = null;
        var bestDistance = reach;
        for (var i = 0; i < nodes.length; i++) {
            var node = nodes[i];
            if (source && (node.contains(source) || source.contains(node))) continue;
            var distance = rectDistance(x, y, node.getBoundingClientRect());
            if (distance >= bestDistance) continue;
            bestDistance = distance;
            best = node;
        }
        return best;
    }

    function rectDistance(x, y, rect) {
        var dx = x < rect.left ? rect.left - x : x > rect.right ? x - rect.right : 0;
        var dy = y < rect.top ? rect.top - y : y > rect.bottom ? y - rect.bottom : 0;
        return Math.sqrt(dx * dx + dy * dy);
    }

    function clearHot() {
        if (!s.workspace) return;
        s.workspace.querySelectorAll('.hot').forEach(function (node) { node.classList.remove('hot'); });
    }

    function autoScroll() {
        if (!s.drag) return;
        var host = s.workspace;
        var rect = host.getBoundingClientRect();
        var delta = s.drag.y < rect.top + SCROLL_EDGE ? -SCROLL_STEP
            : s.drag.y > rect.bottom - SCROLL_EDGE ? SCROLL_STEP : 0;
        if (delta) {
            var top = host.scrollTop;
            host.scrollTop = top + delta;
            if (host.scrollTop !== top) updateDragPosition(s.drag.x, s.drag.y);
        }
        s.scrollFrame = requestAnimationFrame(autoScroll);
    }

    function finishDrop() {
        var drag = s.drag;
        if (!drag) return;
        var payload = drag.payload;
        var target = drag.target;
        cleanupDrag();
        detachPointer();
        if (!target) return;
        var w = ws();
        if (!w) return;

        if (payload.kind === 'expression') {
            if (target.type !== 'input') return;
            var landed;
            if (payload.uid) {
                if (Blocks.moveInput(w, payload.uid, target.owner, target.key)) landed = Blocks.find(w, payload.uid);
            } else {
                var made = Blocks.make(payload.defId);
                if (made && Blocks.attach(w, target.owner, target.key, made)) landed = made;
            }
            if (!landed) return;
            s.selected = landed.uid;
            commit(landed.uid);
            return;
        }

        if (target.type !== 'gap') return;
        var block = payload.uid ? Blocks.find(w, payload.uid) : Blocks.make(payload.defId);
        if (!block) return;
        var moved = payload.uid ? Blocks.move(w, payload.uid, target.pos) : Blocks.insert(w, target.pos, block);
        if (!moved) return;
        s.selected = block.uid;
        commit(block.uid);
    }

    function cleanupDrag() {
        if (s.scrollFrame) { cancelAnimationFrame(s.scrollFrame); s.scrollFrame = null; }
        document.removeEventListener('touchmove', blockTouchMove);
        if (s.drag) {
            var drag = s.drag;
            if (drag.ghost.parentNode) drag.ghost.parentNode.removeChild(drag.ghost);
            drag.node.classList.remove('dragging');
            releaseCapture(drag);
            s.drag = null;
        }
        document.body.classList.remove('block-dragging');
        clearHot();
    }

    function tapAdd(defId) {
        var def = Blocks.defs[defId];
        var w = ws();
        if (!def || !w) return;

        if (def.kind === 'expression') {
            var slot = freeSocket(w);
            if (!slot) return;
            var reporter = Blocks.make(defId);
            if (!Blocks.attach(w, slot.uid, slot.key, reporter)) return;
            s.selected = reporter.uid;
            commit(reporter.uid);
            return;
        }

        var position = 'root:' + w.blocks.length;
        if (s.selected) {
            var selected = Blocks.find(w, s.selected);
            var selectedDef = selected && Blocks.defs[selected.def];
            if (selectedDef && selectedDef.inner && selectedDef.inner.length) {
                var key = selectedDef.inner[0].key;
                position = 'inner:' + selected.uid + ':' + key + ':' + (selected.inner[key] || []).length;
            }
        }
        var block = Blocks.make(defId);
        if (!block || !Blocks.insert(w, position, block)) return;
        s.selected = block.uid;
        commit(block.uid);
    }

    function freeSocket(w) {
        var found = null;
        function scan(block) {
            var def = Blocks.defs[block.def];
            (def && def.inputs ? def.inputs : []).forEach(function (input) {
                if (found) return;
                if (block.inputs[input.key]) scan(block.inputs[input.key]);
                else found = { uid: block.uid, key: input.key };
            });
        }
        var selected = s.selected ? Blocks.find(w, s.selected) : null;
        if (selected) scan(selected);
        if (!found) w.blocks.some(function (block) { scan(block); return !!found; });
        return found;
    }

    function selectBlock(uid) {
        s.selected = uid;
        renderWorkspace();
        renderBar();
    }

    function deleteSelected() {
        if (!s.selected) return;
        Blocks.remove(ws(), s.selected);
        s.selected = null;
        commit();
    }

    function moveBlock(uid, direction) {
        var loc = Blocks.locate(ws(), uid);
        if (!loc) return;
        var array = loc.array;
        var index = loc.index;
        var block = array[index];
        if (direction < 0 && index > 0) {
            array.splice(index, 1);
            array.splice(index - 1, 0, block);
            commit();
        } else if (direction > 0 && index < array.length - 1) {
            array.splice(index, 1);
            array.splice(index + 1, 0, block);
            commit();
        }
    }

    function indentBlock(uid) {
        var loc = Blocks.locate(ws(), uid);
        if (!loc || loc.index <= 0) return;
        var previous = loc.array[loc.index - 1];
        var def = Blocks.defs[previous.def];
        if (!def || !def.inner || !def.inner.length) return;
        if (Blocks.move(ws(), uid, 'inner:' + previous.uid + ':' + def.inner[0].key)) commit();
    }

    function outdentBlock(uid) {
        var loc = Blocks.locate(ws(), uid);
        if (!loc || !loc.owner) return;
        if (Blocks.move(ws(), uid, 'after:' + loc.owner.uid)) commit();
    }

    function toolBtn(label, fn, cls) {
        var button = el('button', 'tool wide' + (cls ? ' ' + cls : ''), label);
        button.addEventListener('click', function (e) { e.stopPropagation(); fn(); });
        return button;
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
                var copy = Blocks.clone(block);
                if (Blocks.insert(ws(), 'after:' + block.uid, copy)) {
                    s.selected = copy.uid;
                    commit(copy.uid);
                }
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
        selectedUid: function () { return s.selected; },
        deleteSelected: deleteSelected
    };
})();
