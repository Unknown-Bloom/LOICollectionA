var LCUI = globalThis.LCUI = globalThis.LCUI || {};

LCUI.Model = (function () {
    'use strict';

    function schema() {
        return LCUI.Schema;
    }

    var Limits = {
        maxControls: 512,
        maxHistory: 256,
        maxTextLength: 4096,
        maxDropdownItems: 64,
        maxPages: 256,
        maxScores: 32,
        maxDocuments: 32,
        maxVariants: 64,
        maxParams: 16,
        maxSourceLength: 1048576
    };

    var sequence = 0;

    function uid() {
        sequence += 1;
        return 'doc-' + Date.now().toString(36) + '-' + sequence.toString(36);
    }

    function isIdentifier(value) {
        return /^[A-Za-z_][A-Za-z0-9_]{0,63}$/.test(value);
    }

    function isFormId(value) {
        return /^[A-Za-z0-9][A-Za-z0-9._-]{0,63}$/.test(value);
    }

    function isFileName(value) {
        return /^[A-Za-z0-9][A-Za-z0-9._-]{0,63}\.lcui$/.test(value);
    }

    function splitLines(value) {
        return String(value || '').split('\n').map(function (line) { return line.trim(); }).filter(function (line) { return line.length > 0; });
    }

    function splitList(value) {
        return String(value || '').split(/[\n,]/).map(function (entry) { return entry.trim(); }).filter(function (entry) { return entry.length > 0; });
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

    function assign(target, source) {
        Object.keys(source || {}).forEach(function (key) { target[key] = source[key]; });
        return target;
    }

    function makeControl(kind) {
        var descriptor = schema().findControl(kind);
        if (!descriptor) return null;
        return { kind: kind, properties: materialize(descriptor.properties), callbacks: {} };
    }

    function makeDocument(formKind) {
        var descriptor = schema().findForm(formKind);
        if (!descriptor) return null;
        var document = {
            id: uid(),
            docType: 'form',
            kind: formKind,
            properties: materialize(descriptor.properties),
            controls: [],
            callbacks: {},
            imports: []
        };
        if (descriptor.file) document.properties.file = descriptor.file;
        return document;
    }

    function makeDeclaration(kind, name) {
        var descriptor = schema().findForm(kind);
        if (!descriptor) return null;
        var document = {
            id: uid(),
            docType: kind === 'Enum' ? 'enum' : 'component',
            kind: kind,
            properties: materialize(descriptor.properties),
            controls: [],
            callbacks: {},
            imports: []
        };
        if (name) document.properties.name = name;
        return document;
    }

    function normalizeControl(raw) {
        var descriptor = raw && schema().findControl(raw.kind);
        if (!descriptor) return raw;
        return {
            kind: raw.kind,
            properties: assign(materialize(descriptor.properties), raw.properties),
            callbacks: raw.callbacks || {},
            callbackParam: raw.callbackParam
        };
    }

    function normalizeDocument(raw) {
        if (!raw || typeof raw !== 'object') return null;
        var docType = raw.kind === 'Enum' ? 'enum' : raw.kind === 'Component' ? 'component' : (raw.docType || 'form');
        if (docType !== 'form' && docType !== 'enum' && docType !== 'component') docType = 'form';
        var kind = docType === 'enum' ? 'Enum' : docType === 'component' ? 'Component' : (raw.kind || 'Custom');
        var descriptor = schema().findForm(kind);
        if (!descriptor) return null;
        var document = {
            id: raw.id || uid(),
            docType: docType,
            kind: kind,
            properties: assign(materialize(descriptor.properties), raw.properties),
            controls: (raw.controls || []).map(normalizeControl),
            callbacks: raw.callbacks || {},
            imports: (raw.imports || []).map(function (entry) {
                if (typeof entry === 'string') return { file: entry, symbols: [] };
                return { file: String(entry.file || ''), symbols: (entry.symbols || []).slice() };
            })
        };
        return document;
    }

    function documentName(document) {
        if (document.docType === 'enum' || document.docType === 'component') return String(document.properties.name || '');
        return String(document.properties.title || document.properties.id || '');
    }

    function documentKind(document) {
        if (document.docType === 'enum') return '枚举';
        if (document.docType === 'component') return '组件';
        var descriptor = schema().findForm(document.kind);
        return descriptor ? descriptor.label : String(document.kind || '');
    }

    function componentParams(document) {
        return splitList(document.properties.params);
    }

    function balanced(value) {
        var pairs = { '(': ')', '[': ']', '<': '>' };
        var stack = [];
        for (var i = 0; i < value.length; ++i) {
            var ch = value.charAt(i);
            if (pairs[ch]) stack.push(pairs[ch]);
            else if (ch === ')' || ch === ']' || ch === '>') {
                if (stack.pop() !== ch) return false;
            }
        }
        return stack.length === 0;
    }

    function splitTypes(value) {
        var types = [];
        var depth = 0;
        var start = 0;
        for (var i = 0; i < value.length; ++i) {
            var ch = value.charAt(i);
            if (ch === '<' || ch === '(' || ch === '[') depth++;
            else if (ch === '>' || ch === ')' || ch === ']') depth--;
            else if (ch === ',' && depth === 0) {
                types.push(value.slice(start, i).trim());
                start = i + 1;
            }
        }
        types.push(value.slice(start).trim());
        return types.filter(function (type) { return type.length > 0; });
    }

    function variantEntry(line, number) {
        var text = line.trim();
        var open = text.indexOf('(');
        var name = open === -1 ? text : text.slice(0, open).trim();
        var params = [];
        var valid = isIdentifier(name);
        var message = valid ? '' : '变体名必须是标识符';

        if (open !== -1) {
            if (text.charAt(text.length - 1) !== ')') {
                valid = false;
                message = '变体载荷缺少右括号';
            } else if (!balanced(text.slice(open))) {
                valid = false;
                message = '变体载荷括号不匹配';
            } else {
                params = splitTypes(text.slice(open + 1, -1));
                if (params.some(function (type) { return type.length === 0 || !balanced(type) || /[;{}]/.test(type); })) {
                    valid = false;
                    message = '变体载荷类型不合法';
                }
            }
        }

        return { name: name, params: params, line: number, valid: valid, message: message };
    }

    function enumVariants(document) {
        return splitLines(document.properties.variants).map(function (line, index) {
            return variantEntry(line, index + 1);
        });
    }

    function isRawExpression(value) {
        var text = String(value || '').trim();
        return text.length > 0 && !/[;{}\n\r]/.test(text) && balanced(text);
    }

    function callArguments(value) {
        return splitLines(value);
    }

    function property(control, key) {
        return Object.prototype.hasOwnProperty.call(control.properties, key) ? control.properties[key] : null;
    }

    function validateControlProperties(document, control, descriptor, index, issues) {
        descriptor.properties.forEach(function (descriptorProperty) {
            var value = property(control, descriptorProperty.key);
            if (value === null || value === undefined) {
                issues.push({ target: 'control:' + index + ':' + descriptorProperty.key, message: descriptor.label + ' 缺少属性 ' + descriptorProperty.label });
                return;
            }
            if (descriptorProperty.type === 'text' && String(value).length > Limits.maxTextLength)
                issues.push({ target: 'control:' + index + ':' + descriptorProperty.key, message: descriptor.label + ' 的 ' + descriptorProperty.label + ' 超过长度上限' });
            if (descriptorProperty.required && String(value).trim() === '')
                issues.push({ target: 'control:' + index + ':' + descriptorProperty.key, message: descriptor.label + ' 的 ' + descriptorProperty.label + ' 不能为空' });

            if (descriptorProperty.type === 'scores') {
                var entries = scoreEntries(value);
                if (entries.length > Limits.maxScores)
                    issues.push({ target: 'control:' + index + ':' + descriptorProperty.key, message: descriptor.label + ' 的 Score 需求超过上限 ' + Limits.maxScores });
                if (entries.some(function (entry) { return !entry.valid; }))
                    issues.push({ target: 'control:' + index + ':' + descriptorProperty.key, message: descriptor.label + ' 的 Score 需求格式应为 计分板:分数' });
            }

            if (descriptorProperty.type === 'text' && document.docType === 'component' && String(value).charAt(0) === '$') {
                var reference = String(value).slice(1);
                if (componentParams(document).indexOf(reference) === -1)
                    issues.push({ target: 'control:' + index + ':' + descriptorProperty.key, message: descriptor.label + ' 引用了未声明的组件参数 $' + reference });
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

        if (control.callbackParam) {
            if (document.docType !== 'component' || componentParams(document).indexOf(control.callbackParam) === -1)
                issues.push({ target: 'control:' + index + ':callbackParam', message: descriptor.label + ' 的回调参数不存在' });
        }
    }

    function validateControls(document, issues) {
        if (document.controls.length > Limits.maxControls)
            issues.push({ target: 'form', message: '组件数量超过上限 ' + Limits.maxControls });

        var accepted = document.docType === 'component'
            ? function (kind) { return schema().componentAcceptsControl(kind); }
            : function (kind) { return schema().formAcceptsControl(document.kind, kind); };
        var label = document.docType === 'component' ? '组件' : (schema().findForm(document.kind) || {}).name;

        var seen = {};
        document.controls.forEach(function (control, index) {
            var descriptor = schema().findControl(control.kind);
            if (!descriptor) {
                issues.push({ target: 'control:' + index, message: '未知组件类型 ' + control.kind });
                return;
            }

            if (!accepted(control.kind))
                issues.push({ target: 'control:' + index, message: descriptor.label + ' 不能用于 ' + label });

            if (descriptor.unique && seen[control.kind])
                issues.push({ target: 'control:' + index, message: descriptor.label + ' 只能出现一次' });
            seen[control.kind] = true;

            validateControlProperties(document, control, descriptor, index, issues);

            if (control.kind === 'ComponentCall') {
                if (!String(property(control, 'target') || '').trim())
                    issues.push({ target: 'control:' + index + ':target', message: '组件调用缺少目标组件' });
                var args = callArguments(property(control, 'arguments'));
                if (args.length > Limits.maxParams)
                    issues.push({ target: 'control:' + index + ':arguments', message: '组件调用参数超过上限 ' + Limits.maxParams });
                args.forEach(function (argument) {
                    if (argument.charAt(0) === '$' && document.docType === 'component') {
                        if (componentParams(document).indexOf(argument.slice(1)) === -1)
                            issues.push({ target: 'control:' + index + ':arguments', message: '组件调用引用了未声明的参数 ' + argument });
                    }
                    if (argument.charAt(0) === '=' && !isRawExpression(argument.slice(1)))
                        issues.push({ target: 'control:' + index + ':arguments', message: '组件调用的表达式参数不合法' });
                });
            }
        });
    }

    function validateForm(document, issues) {
        var id = document.properties.id;
        if (!id) issues.push({ target: 'form:id', message: '表单标识不能为空' });
        else if (!isFormId(id)) issues.push({ target: 'form:id', message: '表单标识仅可使用字母、数字、点、下划线与连字符' });

        var title = document.properties.title;
        if (!title) issues.push({ target: 'form:title', message: '表单标题不能为空' });

        var variable = document.properties.variable;
        if (!variable || !isIdentifier(variable)) issues.push({ target: 'form:variable', message: '脚本变量名必须是以字母或下划线开头的标识符' });

        if (document.kind === 'Paginated') {
            var pages = splitLines(document.properties.pages);
            if (pages.length === 0) issues.push({ target: 'form:pages', message: '分页表单至少需要一个条目' });
            if (pages.length > Limits.maxPages) issues.push({ target: 'form:pages', message: '条目数量超过上限 ' + Limits.maxPages });
            var pageSize = Number(document.properties.pageSize);
            if (!(pageSize >= 1 && pageSize <= 64) || pageSize !== Math.floor(pageSize)) issues.push({ target: 'form:pageSize', message: '每页条数须为 1-64 的整数' });
        }

        validateControls(document, issues);
    }

    function validateEnum(document, issues) {
        if (!isIdentifier(document.properties.name))
            issues.push({ target: 'declaration:name', message: '枚举名必须是以字母或下划线开头的标识符' });

        var variants = enumVariants(document);
        if (!variants.length) issues.push({ target: 'declaration:variants', message: '枚举至少需要一个变体' });
        if (variants.length > Limits.maxVariants) issues.push({ target: 'declaration:variants', message: '变体数量超过上限 ' + Limits.maxVariants });

        var seen = {};
        variants.forEach(function (entry) {
            if (!entry.valid) {
                issues.push({ target: 'declaration:variants', message: '第 ' + entry.line + ' 个变体：' + entry.message });
                return;
            }
            if (seen[entry.name])
                issues.push({ target: 'declaration:variants', message: '变体 ' + entry.name + ' 重复定义' });
            seen[entry.name] = true;
        });
    }

    function validateComponent(document, issues) {
        if (!isIdentifier(document.properties.name))
            issues.push({ target: 'declaration:name', message: '组件名必须是以字母或下划线开头的标识符' });

        var params = componentParams(document);
        if (params.length > Limits.maxParams)
            issues.push({ target: 'declaration:params', message: '组件参数超过上限 ' + Limits.maxParams });
        var seen = {};
        params.forEach(function (name) {
            if (!isIdentifier(name))
                issues.push({ target: 'declaration:params', message: '组件参数 ' + name + ' 不是合法标识符' });
            if (seen[name])
                issues.push({ target: 'declaration:params', message: '组件参数 ' + name + ' 重复' });
            seen[name] = true;
        });

        validateControls(document, issues);
    }

    function validate(document) {
        var issues = [];
        var descriptor = schema().findForm(document.kind);
        if (!descriptor) return [{ target: 'form', message: '未知文档类型 ' + document.kind }];

        if (!isFileName(document.properties.file))
            issues.push({ target: 'form:file', message: '文件名须为字母或数字开头、以 .lcui 结尾的合法文件名' });

        if (document.docType === 'enum') validateEnum(document, issues);
        else if (document.docType === 'component') validateComponent(document, issues);
        else validateForm(document, issues);

        return issues;
    }

    function findComponentByName(project, name) {
        if (!project || !name) return null;
        return project.documents.filter(function (document) {
            return document.docType === 'component' && document.properties.name === name;
        })[0] || null;
    }

    function findDocumentByFile(project, file) {
        if (!project || !file) return null;
        return project.documents.filter(function (document) { return document.properties.file === file; })[0] || null;
    }

    function dependencies(document, project) {
        var edges = [];
        var seen = {};

        function push(target, kind) {
            if (!target || target.id === document.id) return;
            if (seen[target.id]) return;
            seen[target.id] = true;
            edges.push({ document: target, kind: kind });
        }

        document.controls.forEach(function (control) {
            if (control.kind !== 'ComponentCall') return;
            push(findComponentByName(project, control.properties.target), 'call');
        });

        (document.imports || []).forEach(function (entry) {
            push(findDocumentByFile(project, entry.file), 'import');
        });

        return edges;
    }

    function projectEdges(project) {
        var edges = [];
        project.documents.forEach(function (document) {
            dependencies(document, project).forEach(function (edge) {
                edges.push({ from: document.id, to: edge.document.id, kind: edge.kind });
            });
        });
        return edges;
    }

    function uniqueFileName(project, desired, ignoreId) {
        var name = String(desired || '').replace(/[^A-Za-z0-9._-]/g, '_');
        if (!/^[A-Za-z0-9]/.test(name)) name = 'document' + name;
        if (!/\.lcui$/.test(name)) name += '.lcui';
        name = name.slice(0, 64);
        var used = {};
        project.documents.forEach(function (document) {
            if (document.id !== ignoreId) used[document.properties.file] = true;
        });
        if (!used[name]) return name;
        var stem = name.replace(/\.lcui$/, '');
        var index = 2;
        while (used[stem + '-' + index + '.lcui']) index++;
        return stem + '-' + index + '.lcui';
    }

    function projectIssues(project) {
        var issues = [];
        if (!project || !project.documents || !project.documents.length)
            return [{ level: 'error', target: 'project', message: '项目至少需要一个文档' }];
        if (project.documents.length > Limits.maxDocuments)
            issues.push({ level: 'error', target: 'project', message: '文档数量超过上限 ' + Limits.maxDocuments });

        var files = {};
        var names = {};
        var variants = {};

        project.documents.forEach(function (document) {
            var file = document.properties.file;
            if (!isFileName(file))
                issues.push({ level: 'error', target: 'document:' + document.id, message: (documentName(document) || '文档') + ' 的文件名不合法' });
            else if (files[file])
                issues.push({ level: 'error', target: 'document:' + document.id, message: '文件名 ' + file + ' 重复' });
            files[file] = true;

            if (document.docType === 'enum' || document.docType === 'component') {
                var name = document.properties.name;
                if (names[name])
                    issues.push({ level: 'error', target: 'document:' + document.id, message: documentKind(document) + '名 ' + name + ' 与其他声明重复' });
                names[name] = true;
            }

            if (document.docType === 'enum') {
                enumVariants(document).forEach(function (entry) {
                    if (!entry.valid) return;
                    if (variants[entry.name])
                        issues.push({ level: 'error', target: 'document:' + document.id, message: '变体 ' + entry.name + ' 与 ' + variants[entry.name] + ' 中的同名变体冲突' });
                    else variants[entry.name] = document.properties.name;
                });
            }

            (document.imports || []).forEach(function (entry) {
                if (!isFileName(entry.file))
                    issues.push({ level: 'error', target: 'document:' + document.id, message: '导入路径 ' + entry.file + ' 不是合法文件名' });
                if (entry.file === file)
                    issues.push({ level: 'error', target: 'document:' + document.id, message: '文档不能导入自身' });
            });

            if (document.docType === 'form') {
                project.documents.forEach(function (other) {
                    if (other.id !== document.id && (other.docType === 'enum' || other.docType === 'component') &&
                        other.properties.name === document.properties.variable)
                        issues.push({ level: 'error', target: 'document:' + document.id, message: '变量名 ' + document.properties.variable + ' 与 ' + other.properties.name + ' 冲突' });
                });
            }
        });

        project.documents.forEach(function (document) {
            document.controls.forEach(function (control, index) {
                if (control.kind !== 'ComponentCall') return;
                var target = findComponentByName(project, control.properties.target);
                if (!target) {
                    issues.push({ level: 'error', target: 'document:' + document.id + ':control:' + index, message: '组件调用 ' + control.properties.target + ' 不存在' });
                    return;
                }
                if (target.id === document.id)
                    issues.push({ level: 'error', target: 'document:' + document.id + ':control:' + index, message: '组件 ' + target.properties.name + ' 不能调用自身' });
                var expected = componentParams(target).length;
                var actual = callArguments(control.properties.arguments).length;
                if (expected !== actual)
                    issues.push({ level: 'error', target: 'document:' + document.id + ':control:' + index, message: target.properties.name + ' 需要 ' + expected + ' 个参数，实际提供 ' + actual + ' 个' });
            });

            (document.imports || []).forEach(function (entry) {
                var target = findDocumentByFile(project, entry.file);
                if (!target) {
                    issues.push({ level: 'warning', target: 'document:' + document.id, message: '导入文件 ' + entry.file + ' 不在项目中，导出时将保留该导入' });
                    return;
                }
                if (target.docType === 'form')
                    issues.push({ level: 'error', target: 'document:' + document.id, message: '被导入的文件 ' + entry.file + ' 包含可执行表单，不能作为定义文件导入' });
            });
        });

        var color = {};
        var stack = [];
        var reported = {};

        function visit(document) {
            color[document.id] = 1;
            stack.push(document);
            dependencies(document, project).forEach(function (edge) {
                var target = edge.document;
                if (color[target.id] === 1) {
                    var start = 0;
                    stack.forEach(function (entry, index) { if (entry.id === target.id) start = index; });
                    var cycle = stack.slice(start);
                    var key = cycle.map(function (entry) { return entry.id; }).sort().join('|');
                    if (reported[key]) return;
                    reported[key] = true;
                    issues.push({
                        level: 'error',
                        target: 'project:cycle',
                        message: '循环依赖：' + cycle.map(documentName).concat(documentName(target)).join(' → ')
                    });
                } else if (color[target.id] !== 2) {
                    visit(target);
                }
            });
            stack.pop();
            color[document.id] = 2;
        }

        project.documents.forEach(function (document) {
            if (!color[document.id]) visit(document);
        });

        return issues;
    }

    function makeProject(documents, activeId) {
        var project = { documents: documents || [], activeId: activeId || null };
        if (!project.activeId && project.documents.length) project.activeId = project.documents[0].id;
        return project;
    }

    function normalizeProject(raw, fallback) {
        var source = raw && raw.documents && raw.documents.length ? raw.documents : (fallback ? [fallback] : []);
        var seen = {};
        var documents = source.map(normalizeDocument).filter(Boolean).map(function (document) {
            if (seen[document.id]) document.id = uid();
            seen[document.id] = true;
            return document;
        });
        var project = makeProject(documents, raw && raw.activeId);
        if (!project.documents.some(function (document) { return document.id === project.activeId; }))
            project.activeId = documents.length ? documents[0].id : null;
        return project;
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
        if (String(previous) === String(value) && typeof previous === typeof value) return false;

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
        if (String(previous) === String(value) && typeof previous === typeof value) return false;

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

    Session.prototype.setControlCallbackParam = function (index, value) {
        var control = this.document.controls[index];
        if (!control) return false;
        var previous = control.callbackParam || '';
        if (previous === value) return false;
        this.record({ type: 'callbackParam', index: index, previous: previous, next: value });
        if (value) control.callbackParam = value;
        else delete control.callbackParam;
        this.changed();
        return true;
    };

    Session.prototype.accepts = function (kind) {
        if (this.document.docType === 'component') return schema().componentAcceptsControl(kind);
        if (this.document.docType !== 'form') return false;
        return schema().formAcceptsControl(this.document.kind, kind);
    };

    Session.prototype.insertControl = function (index, kind) {
        if (index < 0 || index > this.document.controls.length) return false;
        if (this.document.controls.length >= Limits.maxControls) return false;
        if (!this.accepts(kind)) return false;

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
        } else if (entry.type === 'callbackParam') {
            if (document.controls[entry.index]) {
                var value = forward ? entry.next : entry.previous;
                if (value) document.controls[entry.index].callbackParam = value;
                else delete document.controls[entry.index].callbackParam;
            }
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
            this.document.docType = document.docType;
            this.document.properties = document.properties;
            this.document.controls = document.controls;
            this.document.callbacks = document.callbacks;
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
        uid: uid,
        isIdentifier: isIdentifier,
        isFormId: isFormId,
        isFileName: isFileName,
        splitLines: splitLines,
        splitList: splitList,
        scoreEntries: scoreEntries,
        makeControl: makeControl,
        makeDocument: makeDocument,
        makeDeclaration: makeDeclaration,
        normalizeDocument: normalizeDocument,
        normalizeProject: normalizeProject,
        makeProject: makeProject,
        documentName: documentName,
        documentKind: documentKind,
        componentParams: componentParams,
        enumVariants: enumVariants,
        callArguments: callArguments,
        findComponentByName: findComponentByName,
        findDocumentByFile: findDocumentByFile,
        dependencies: dependencies,
        projectEdges: projectEdges,
        projectIssues: projectIssues,
        uniqueFileName: uniqueFileName,
        validate: validate,
        Session: Session,
        schema: schema
    };
})();
