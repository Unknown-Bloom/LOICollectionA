var LCUI = globalThis.LCUI = globalThis.LCUI || {};

LCUI.Model = (function () {
    'use strict';

    function schema() {
        return LCUI.Schema;
    }

    var Limits = { maxControls: 512, maxHistory: 256, maxTextLength: 4096, maxDropdownItems: 64, maxPages: 256, maxScores: 32 };

    function isIdentifier(value) {
        return /^[A-Za-z_][A-Za-z0-9_]{0,63}$/.test(value);
    }

    function isFormId(value) {
        return /^[A-Za-z0-9][A-Za-z0-9._-]{0,63}$/.test(value);
    }

    function splitLines(value) {
        return String(value || '').split('\n').map(function (line) { return line.trim(); }).filter(function (line) { return line.length > 0; });
    }

    function scoreEntries(value) {
        return splitLines(value).map(function (line) {
            var separator = line.lastIndexOf(':');
            if (separator <= 0) return { objective: '', value: 0, valid: false };
            var objective = line.slice(0, separator).trim();
            var amount = Number(line.slice(separator + 1).trim());
            return { objective: objective, value: amount, valid: objective.length > 0 && isFinite(amount) && Math.floor(amount) === amount };
        });
    }

    function materialize(descriptors) {
        var values = {};
        descriptors.forEach(function (descriptor) { values[descriptor.key] = descriptor.initial; });
        return values;
    }

    function makeControl(kind) {
        var descriptor = schema().findControl(kind);
        if (!descriptor) return null;
        return { kind: kind, properties: materialize(descriptor.properties), callbacks: {} };
    }

    function makeDocument(formKind) {
        var descriptor = schema().findForm(formKind);
        if (!descriptor) return null;
        return { kind: formKind, properties: materialize(descriptor.properties), controls: [], callbacks: {} };
    }

    function sameValue(left, right) {
        return String(left) === String(right) && typeof left === typeof right;
    }

    function property(control, key) {
        return Object.prototype.hasOwnProperty.call(control.properties, key) ? control.properties[key] : null;
    }

    function validate(document) {
        var issues = [];
        var s = schema();

        var id = document.properties.id;
        if (!id) issues.push({ target: 'form:id', message: '表单标识不能为空' });
        else if (!isFormId(id)) issues.push({ target: 'form:id', message: '表单标识仅可使用字母、数字、点、下划线与连字符' });

        var title = document.properties.title;
        if (!title) issues.push({ target: 'form:title', message: '表单标题不能为空' });

        var variable = document.properties.variable;
        if (!variable || !isIdentifier(variable)) issues.push({ target: 'form:variable', message: '脚本变量名必须是以字母或下划线开头的标识符' });

        if (document.controls.length > Limits.maxControls)
            issues.push({ target: 'form', message: '组件数量超过上限 ' + Limits.maxControls });

        if (document.kind === 'Paginated') {
            var pages = splitLines(document.properties.pages);
            if (pages.length === 0) issues.push({ target: 'form:pages', message: '分页表单至少需要一个条目' });
            if (pages.length > Limits.maxPages) issues.push({ target: 'form:pages', message: '条目数量超过上限 ' + Limits.maxPages });
            var pageSize = Number(document.properties.pageSize);
            if (!(pageSize >= 1 && pageSize <= 64) || pageSize !== Math.floor(pageSize)) issues.push({ target: 'form:pageSize', message: '每页条数须为 1-64 的整数' });
        }

        var seen = {};
        document.controls.forEach(function (control, index) {
            var descriptor = s.findControl(control.kind);
            if (!descriptor) {
                issues.push({ target: 'control:' + index, message: '未知组件类型 ' + control.kind });
                return;
            }

            if (!s.formAcceptsControl(document.kind, control.kind)) {
                issues.push({ target: 'control:' + index, message: descriptor.label + ' 不能用于 ' + s.findForm(document.kind).name });
            }

            if (descriptor.unique && seen[control.kind]) {
                issues.push({ target: 'control:' + index, message: descriptor.label + ' 只能出现一次' });
            }
            seen[control.kind] = true;

            descriptor.properties.forEach(function (descriptorProperty) {
                var value = property(control, descriptorProperty.key);
                if (value === null || value === undefined) {
                    issues.push({ target: 'control:' + index + ':' + descriptorProperty.key, message: descriptor.label + ' 缺少属性 ' + descriptorProperty.label });
                    return;
                }
                if (descriptorProperty.type === 'text' && String(value).length > Limits.maxTextLength)
                    issues.push({ target: 'control:' + index + ':' + descriptorProperty.key, message: descriptor.label + ' 的 ' + descriptorProperty.label + ' 超过长度上限' });
                if (descriptorProperty.required && descriptorProperty.type === 'text' && String(value).trim() === '')
                    issues.push({ target: 'control:' + index + ':' + descriptorProperty.key, message: descriptor.label + ' 的 ' + descriptorProperty.label + ' 不能为空' });

                if (descriptorProperty.type === 'scores') {
                    var entries = scoreEntries(value);
                    if (entries.length > Limits.maxScores)
                        issues.push({ target: 'control:' + index + ':' + descriptorProperty.key, message: descriptor.label + ' 的 Score 需求超过上限 ' + Limits.maxScores });
                    if (entries.some(function (entry) { return !entry.valid; }))
                        issues.push({ target: 'control:' + index + ':' + descriptorProperty.key, message: descriptor.label + ' 的 Score 需求格式应为 计分板:分数' });
                }
            });

            if (control.kind === 'Dropdown') {
                var entries = splitLines(property(control, 'items'));
                if (entries.length === 0)
                    issues.push({ target: 'control:' + index + ':items', message: '下拉选择至少需要一个选项（每行一个）' });
                if (entries.length > Limits.maxDropdownItems)
                    issues.push({ target: 'control:' + index + ':items', message: '下拉选项超过上限 ' + Limits.maxDropdownItems });
                if (entries.length > 0 && Number(property(control, 'value')) >= entries.length)
                    issues.push({ target: 'control:' + index + ':value', message: '选中项索引超出选项范围（0-' + (entries.length - 1) + '）' });
            }

            if (control.kind === 'Slider') {
                var min = Number(property(control, 'min'));
                var max = Number(property(control, 'max'));
                var step = Number(property(control, 'step'));
                if (min >= max)
                    issues.push({ target: 'control:' + index + ':max', message: '滑块最大值必须大于最小值' });
                if (!(step > 0))
                    issues.push({ target: 'control:' + index + ':step', message: '滑块步长必须大于 0' });
            }
        });

        return issues;
    }

    function Session(initialDocument) {
        this.document = initialDocument || makeDocument('Custom');
        this.undoStack = [];
        this.redoStack = [];
        this.modified = false;
        this.listeners = [];
    }

    Session.prototype.changed = function () {
        var session = this;
        this.listeners.forEach(function (listener) { listener(session); });
    };

    Session.prototype.onChange = function (listener) {
        this.listeners.push(listener);
    };

    Session.prototype.canUndo = function () {
        return this.undoStack.length > 0;
    };

    Session.prototype.canRedo = function () {
        return this.redoStack.length > 0;
    };

    Session.prototype.isModified = function () {
        return this.modified;
    };

    Session.prototype.markSaved = function () {
        this.modified = false;
        this.changed();
    };

    Session.prototype.record = function (entry) {
        this.undoStack.push(entry);
        if (this.undoStack.length > Limits.maxHistory) this.undoStack.shift();
        this.redoStack = [];
        this.modified = true;
        this.changed();
    };

    Session.prototype.clone = function (object) {
        return JSON.parse(JSON.stringify(object));
    };

    Session.prototype.setFormProperty = function (key, value) {
        var previous = this.document.properties[key];
        if (sameValue(previous, value)) return false;

        var top = this.undoStack[this.undoStack.length - 1];
        if (top && top.type === 'formProperty' && top.key === key && top.stamp && Date.now() - top.stamp < 900) {
            top.stamp = Date.now();
            top.next = value;
        } else {
            this.record({ type: 'formProperty', key: key, previous: previous, next: value, stamp: Date.now() });
        }
        this.document.properties[key] = value;
        this.changed();
        return true;
    };

    Session.prototype.setControlProperty = function (index, key, value) {
        var control = this.document.controls[index];
        if (!control) return false;

        var previous = control.properties[key];
        if (sameValue(previous, value)) return false;

        var top = this.undoStack[this.undoStack.length - 1];
        if (top && top.type === 'controlProperty' && top.index === index && top.key === key && top.stamp && Date.now() - top.stamp < 900) {
            top.stamp = Date.now();
            top.next = value;
        } else {
            this.record({ type: 'controlProperty', index: index, key: key, previous: previous, next: value, stamp: Date.now() });
        }
        control.properties[key] = value;
        this.changed();
        return true;
    };

    Session.prototype.insertControl = function (index, kind) {
        if (index < 0 || index > this.document.controls.length) return false;
        if (this.document.controls.length >= Limits.maxControls) return false;
        if (!schema().formAcceptsControl(this.document.kind, kind)) return false;

        var control = makeControl(kind);
        this.record({ type: 'presence', index: index, control: control });
        this.document.controls.splice(index, 0, control);
        this.changed();
        return true;
    };

    Session.prototype.duplicateControl = function (index) {
        var controls = this.document.controls;
        if (index === null || index === undefined || index >= controls.length) return false;
        if (controls.length >= Limits.maxControls) return false;

        var control = this.clone(controls[index]);
        var descriptor = LCUI.Schema.findControl(control.kind);
        if (descriptor && descriptor.unique) return false;

        this.record({ type: 'presence', index: index + 1, control: control });
        controls.splice(index + 1, 0, control);
        return true;
    };

    Session.prototype.eraseControl = function (index) {
        if (index < 0 || index >= this.document.controls.length) return false;

        this.record({ type: 'presence', index: index, control: this.clone(this.document.controls[index]) });
        this.document.controls.splice(index, 1);
        this.changed();
        return true;
    };

    Session.prototype.moveControl = function (from, to) {
        var count = this.document.controls.length;
        if (from < 0 || from >= count || to < 0 || to > count || from === to) return false;

        this.record({ type: 'move', from: from, to: to });
        var control = this.document.controls.splice(from, 1)[0];
        this.document.controls.splice(to > from ? to - 1 : to, 0, control);
        this.changed();
        return true;
    };

    Session.prototype.reset = function (document) {
        this.record({ type: 'reset', previous: this.clone(this.document), next: this.clone(document) });
        this.document = document;
        this.changed();
    };

    Session.prototype.recordCallback = function (index, kind, workspace, baseline) {
        var holder;
        if (index === null || index === undefined) {
            holder = this.document.callbacks = this.document.callbacks || {};
        } else {
            var control = this.document.controls[index];
            if (!control) return false;
            holder = control.callbacks = control.callbacks || {};
        }
        if (!workspace) return false;

        var before = baseline ? this.clone(baseline) : (holder[kind] ? this.clone(holder[kind]) : { blocks: [] });
        var next = this.clone(workspace);
        if (JSON.stringify(before) === JSON.stringify(next)) return false;

        var top = this.undoStack[this.undoStack.length - 1];
        if (top && top.type === 'callback' && top.index === index && top.kind === kind && top.stamp && Date.now() - top.stamp < 800) {
            top.stamp = Date.now();
            top.next = next;
        } else {
            this.record({ type: 'callback', index: index, kind: kind, previous: before, next: next, stamp: Date.now() });
        }
        holder[kind] = workspace;
        this.changed();
        return true;
    };

    Session.prototype.apply = function (entry, forward) {
        var document = this.document;
        if (entry.type === 'formProperty') {
            document.properties[entry.key] = forward ? entry.next : entry.previous;
        } else if (entry.type === 'controlProperty') {
            if (document.controls[entry.index]) document.controls[entry.index].properties[entry.key] = forward ? entry.next : entry.previous;
        } else if (entry.type === 'presence') {
            if (forward) document.controls.splice(entry.index, 0, this.clone(entry.control));
            else document.controls.splice(entry.index, 1);
        } else if (entry.type === 'move') {
            var source = forward ? entry.from : (entry.to > entry.from ? entry.to - 1 : entry.to);
            var target = forward ? (entry.to > entry.from ? entry.to - 1 : entry.to) : entry.from;
            var control = document.controls.splice(source, 1)[0];
            document.controls.splice(target, 0, control);
        } else if (entry.type === 'reset') {
            document = this.clone(forward ? entry.next : entry.previous);
            this.document.kind = document.kind;
            this.document.properties = document.properties;
            this.document.controls = document.controls;
        } else if (entry.type === 'callback') {
            var cbHolder = entry.index === null || entry.index === undefined
                ? (this.document.callbacks = this.document.callbacks || {})
                : (this.document.controls[entry.index]
                    ? (this.document.controls[entry.index].callbacks = this.document.controls[entry.index].callbacks || {})
                    : null);
            if (cbHolder) cbHolder[entry.kind] = this.clone(forward ? entry.next : entry.previous);
        }
    };

    Session.prototype.undo = function () {
        if (!this.undoStack.length) return;
        var entry = this.undoStack.pop();
        this.apply(entry, false);
        this.redoStack.push(entry);
        this.modified = this.undoStack.length > 0;
        this.changed();
    };

    Session.prototype.redo = function () {
        if (!this.redoStack.length) return;
        var entry = this.redoStack.pop();
        this.apply(entry, true);
        this.undoStack.push(entry);
        this.modified = true;
        this.changed();
    };

    return {
        Limits: Limits,
        isIdentifier: isIdentifier,
        isFormId: isFormId,
        splitLines: splitLines,
        scoreEntries: scoreEntries,
        makeControl: makeControl,
        makeDocument: makeDocument,
        validate: validate,
        Session: Session,
        schema: schema
    };
})();
