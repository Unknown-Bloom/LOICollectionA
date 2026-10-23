var LCUI = globalThis.LCUI = globalThis.LCUI || {};

LCUI.Codegen = (function () {
    'use strict';

    var Model = LCUI.Model;
    var Schema = LCUI.Schema;

    var optionsClasses = ['TextOptions', 'ButtonOptions', 'TextFieldOptions', 'DropdownOptions', 'SliderOptions', 'ToggleOptions', 'DividerOptions', 'SpacingOptions'];
    var observableClasses = { ObservableString: 'String', ObservableBoolean: 'Boolean', ObservableNumber: 'Number' };

    function quote(value) {
        var out = '"';
        var text = String(value === null || value === undefined ? '' : value);
        for (var i = 0; i < text.length; ++i) {
            var entry = text.charAt(i);
            if (entry === '\\') out += '\\\\';
            else if (entry === '"') out += '\\"';
            else if (entry === '\n') out += '\\n';
            else if (entry === '\r') out += '\\r';
            else if (entry === '\t') out += '\\t';
            else out += entry;
        }
        return out + '"';
    }

    function numberLiteral(value) {
        var number = Number(value);
        if (!isFinite(number)) return '0';
        if (Math.abs(number) < 1e15 && Math.floor(number) === number) return String(Math.round(number));
        return String(number);
    }

    function optionValue(descriptor, value) {
        if (descriptor.type === 'text') return quote(value);
        if (descriptor.type === 'boolean') return value ? 'true' : 'false';
        return numberLiteral(value);
    }

    function isDefault(control, propertyDescriptor) {
        return String(control.properties[propertyDescriptor.key]) === String(propertyDescriptor.initial);
    }

    function hasCustomOptions(control, descriptor) {
        return descriptor.optionsClass && descriptor.properties.some(function (propertyDescriptor) {
            return !propertyDescriptor.argument && !isDefault(control, propertyDescriptor);
        });
    }

    function emit(document) {
        var issues = Model.validate(document);
        if (issues.length) return { ok: false, issues: issues };

        var prefix = Model.isIdentifier(document.properties.variable) ? document.properties.variable : 'form';

        var helperLines = [];
        var ordinals = {};

        document.controls.forEach(function (control) {
            var descriptor = Schema.findControl(control.kind);
            var ordinal = (ordinals[control.kind] = (ordinals[control.kind] || 0) + 1);
            var base = prefix + '_' + descriptor.name + '_' + ordinal;

            if (descriptor.observable === 'String')
                helperLines.push('let ' + base + '_value = new ObservableString(' + quote(control.properties[descriptor.observableKey]) + ', true);');
            if (descriptor.observable === 'Boolean')
                helperLines.push('let ' + base + '_value = new ObservableBoolean(' + (control.properties[descriptor.observableKey] ? 'true' : 'false') + ', true);');
            if (descriptor.observable === 'Number')
                helperLines.push('let ' + base + '_value = new ObservableNumber(' + numberLiteral(control.properties[descriptor.observableKey]) + ', true);');

            if (control.kind === 'Dropdown') {
                Model.splitLines(control.properties.items).forEach(function (entry, index) {
                    helperLines.push('let ' + base + '_items_' + index + ' = new DropdownItem();');
                    helperLines.push(base + '_items_' + index + '.label = ' + quote(entry) + ';');
                    helperLines.push(base + '_items_' + index + '.value = ' + index + ';');
                });
                var references = Model.splitLines(control.properties.items).map(function (entry, index) { return base + '_items_' + index; });
                helperLines.push('let ' + base + '_items = [' + references.join(', ') + '];');
            }

            if (hasCustomOptions(control, descriptor)) {
                helperLines.push('let ' + base + '_options = new ' + descriptor.optionsClass + '();');
                descriptor.properties.forEach(function (propertyDescriptor) {
                    if (!propertyDescriptor.argument && !isDefault(control, propertyDescriptor))
                        helperLines.push(base + '_options.' + propertyDescriptor.key + ' = ' + optionValue(propertyDescriptor, control.properties[propertyDescriptor.key]) + ';');
                });
            }
        });

        var formDescriptor = Schema.findForm(document.kind);
        var constructor = 'let ' + prefix + ' = new ' + formDescriptor.name + '(' + quote(document.properties.id) + ', ' + quote(document.properties.title);
        if (document.kind === 'Paginated') {
            var pages = Model.splitLines(document.properties.pages).map(function (entry) { return quote(entry); }).join(', ');
            constructor += ', [' + pages + '], ' + numberLiteral(document.properties.pageSize);
        }
        constructor += ') {';

        var source = '';
        if (helperLines.length) source += helperLines.join('\n') + '\n\n';
        source += constructor + '\n';

        ordinals = {};
        document.controls.forEach(function (control) {
            var descriptor = Schema.findControl(control.kind);
            var ordinal = (ordinals[control.kind] = (ordinals[control.kind] || 0) + 1);
            var base = prefix + '_' + descriptor.name + '_' + ordinal;

            var args = [];
            descriptor.properties.forEach(function (propertyDescriptor) {
                if (!propertyDescriptor.argument) return;
                if (!propertyDescriptor.required && propertyDescriptor.type === 'text' &&
                    String(control.properties[propertyDescriptor.key]) === String(propertyDescriptor.initial))
                    return;

                if (descriptor.observable && propertyDescriptor.key === descriptor.observableKey) {
                    args.push(base + '_value');
                    return;
                }

                if (control.kind === 'Dropdown' && propertyDescriptor.key === 'items') {
                    args.push(base + '_items');
                    return;
                }

                if (propertyDescriptor.type === 'text') args.push(quote(control.properties[propertyDescriptor.key]));
                else if (propertyDescriptor.type === 'boolean') args.push(control.properties[propertyDescriptor.key] ? 'true' : 'false');
                else args.push(numberLiteral(control.properties[propertyDescriptor.key]));
            });

            if (descriptor.hasCallback) args.push('on: func () -> void {\n    }');

            if (descriptor.optionsClass) {
                if (hasCustomOptions(control, descriptor)) args.push(base + '_options');
                else args.push('new ' + descriptor.optionsClass + '()');
            }

            source += '    ' + descriptor.name + '(' + args.join(', ') + ');\n';
        });

        source += '    show();\n};\n';

        return { ok: true, source: source };
    }

    function tokenize(text) {
        var tokens = [];
        var line = 1;
        var position = 0;

        function fail(message) {
            throw { line: line, message: message };
        }

        while (position < text.length) {
            var entry = text.charAt(position);

            if (entry === '\n') { line++; position++; continue; }
            if (entry === ' ' || entry === '\t' || entry === '\r') { position++; continue; }
            if (entry === '/' && text.charAt(position + 1) === '/') {
                while (position < text.length && text.charAt(position) !== '\n') position++;
                continue;
            }

            var startLine = line;

            if (entry === '-' && text.charAt(position + 1) === '>') {
                tokens.push({ type: 'punct', value: '->', line: startLine });
                position += 2;
                continue;
            }

            if ('(){}[];,.:='.indexOf(entry) !== -1) {
                tokens.push({ type: 'punct', value: entry, line: startLine });
                position++;
                continue;
            }

            if (entry === '"') {
                var value = '';
                position++;
                while (position < text.length && text.charAt(position) !== '"') {
                    var inner = text.charAt(position);
                    if (inner === '\\' && position + 1 < text.length) {
                        var escaped = text.charAt(position + 1);
                        if (escaped === 'n') value += '\n';
                        else if (escaped === 'r') value += '\r';
                        else if (escaped === 't') value += '\t';
                        else value += escaped;
                        position += 2;
                    } else {
                        if (inner === '\n') fail('字符串未闭合');
                        value += inner;
                        position++;
                    }
                }
                if (position >= text.length) fail('字符串未闭合');
                position++;
                tokens.push({ type: 'string', value: value, line: startLine });
                continue;
            }

            if (/[0-9]/.test(entry) || (entry === '-' && /[0-9]/.test(text.charAt(position + 1) || ''))) {
                var number = '';
                while (position < text.length && /[0-9.]/.test(text.charAt(position))) {
                    number += text.charAt(position);
                    position++;
                }
                tokens.push({ type: 'number', value: Number(number), line: startLine });
                continue;
            }

            if (/[A-Za-z_]/.test(entry)) {
                var word = '';
                while (position < text.length && /[A-Za-z0-9_]/.test(text.charAt(position))) {
                    word += text.charAt(position);
                    position++;
                }
                tokens.push({ type: 'ident', value: word, line: startLine });
                continue;
            }

            fail('无法识别的字符 "' + entry + '"（编辑器仅识别自身生成的脚本结构）');
        }

        tokens.push({ type: 'eof', value: '', line: line });
        return tokens;
    }

    function parse(source) {
        try {
            if (String(source).length > 1048576)
                return { ok: false, errors: [{ line: 0, message: '源码过长，编辑器仅接受 1 MiB 以内的脚本' }] };

            var state = { tokens: tokenize(source), index: 0, helpers: {} };

            function peek(offset) { return state.tokens[Math.min(state.index + (offset || 0), state.tokens.length - 1)]; }
            function next() { return state.tokens[state.index++]; }
            function at(value) { var token = peek(); return token.type !== 'eof' && token.value === value; }
            function eat(value) {
                var token = peek();
                if (token.type === 'eof' || token.value !== value) fail('缺少 "' + value + '"', token);
                state.index++;
                return token;
            }
            function fail(message, token) {
                throw { line: (token || peek()).line, message: message };
            }
            function isString(token) { return token.type === 'string'; }
            function isNumber(token) { return token.type === 'number'; }
            function isIdent(token) { return token.type === 'ident'; }
            function isBool(token) { return token.type === 'ident' && (token.value === 'true' || token.value === 'false'); }
            function boolValue(token) { return token.value === 'true'; }

            function parseArguments() {
                eat('(');
                var args = [];
                while (!at(')')) {
                    if (args.length) eat(',');
                    var token = peek();

                    if (isString(token)) { args.push({ kind: 'string', value: next().value }); continue; }
                    if (isNumber(token)) { args.push({ kind: 'number', value: next().value }); continue; }
                    if (isBool(token)) { args.push({ kind: 'boolean', value: boolValue(next()) }); continue; }

                    if (isIdent(token)) {
                        if (token.value === 'on' && peek(1).value === ':') {
                            state.index += 2;
                            eat('func');
                            eat('(');
                            eat(')');
                            eat('->');
                            eat('void');
                            eat('{');
                            if (!at('}')) fail('按钮回调必须保留为空函数体（由编辑器生成）', peek());
                            eat('}');
                            args.push({ kind: 'callback' });
                            continue;
                        }

                        if (token.value === 'new' && peek(1).type === 'ident') {
                            state.index += 2;
                            var className = state.tokens[state.index - 1].value;
                            eat('(');
                            if (!at(')')) fail('内联构造仅支持无参形式 new ' + className + '()', peek());
                            eat(')');
                            args.push({ kind: 'inline', className: className });
                            continue;
                        }

                        args.push({ kind: 'reference', name: next().value });
                        continue;
                    }

                    if (token.type === 'punct' && token.value === '[') {
                        eat('[');
                        var elements = [];
                        while (!at(']')) {
                            if (elements.length) eat(',');
                            var element = peek();
                            if (isString(element)) elements.push({ kind: 'string', value: next().value });
                            else if (isIdent(element)) elements.push({ kind: 'reference', name: next().value });
                            else fail('数组元素必须是字符串或变量名', element);
                        }
                        eat(']');
                        args.push({ kind: 'array', elements: elements });
                        continue;
                    }

                    fail('无法识别的参数', token);
                }
                eat(')');
                return args;
            }

            function expectSemicolon() {
                if (at(';')) state.index++;
            }

            var formBinding = null;

            while (peek().type !== 'eof') {
                var token = peek();

                if (isIdent(token) && token.value === 'let') {
                    next();
                    var name = next();
                    if (!isIdent(name)) fail('let 后应为变量名', name);
                    eat('=');

                    if (at('[')) {
                        next();
                        var references = [];
                        while (!at(']')) {
                            if (references.length) eat(',');
                            var element = peek();
                            if (isIdent(element)) references.push({ kind: 'reference', name: next().value });
                            else if (isString(element)) references.push({ kind: 'string', value: next().value });
                            else fail('数组元素必须是变量名或字符串', element);
                        }
                        eat(']');
                        state.helpers[name.value] = { kind: 'itemsArray', elements: references };
                        expectSemicolon();
                        continue;
                    }

                    if (!at('new')) fail('let 声明仅支持 new 构造或数组', peek());

                    next();
                    var classNameToken = next();
                    if (!isIdent(classNameToken)) fail('new 后应为类名', classNameToken);
                    var className = classNameToken.value;

                    if (Schema.findFormByName(className)) {
                        if (formBinding) fail('只支持单个表单声明', classNameToken);
                        var args = parseArguments();

                        if (!at('{')) fail('表单构造必须带声明式块 { … }', classNameToken);
                        next();

                        formBinding = { variable: name.value, className: className, args: args, calls: [], line: classNameToken.line };
                        while (!at('}')) {
                            var call = next();
                            if (!isIdent(call)) fail('声明式块内只支持方法调用', call);
                            var callArgs = parseArguments();
                            formBinding.calls.push({ name: call.value, args: callArgs, line: call.line });
                            expectSemicolon();
                        }
                        eat('}');
                        expectSemicolon();
                        if (peek().type !== 'eof') fail('表单声明必须是最后一条语句', peek());
                    } else if (observableClasses[className]) {
                        var observableArgs = parseArguments();
                        if (observableArgs.length !== 2 || observableArgs[1].kind !== 'boolean' || !observableArgs[1].value)
                            fail('可观察对象构造应为 new ' + className + '(初始值, true)', classNameToken);
                        state.helpers[name.value] = { kind: 'observable', className: className, initial: observableArgs[0].value };
                        expectSemicolon();
                    } else if (optionsClasses.indexOf(className) !== -1) {
                        var optionArgs = parseArguments();
                        if (optionArgs.length) fail('选项对象应为无参构造 new ' + className + '()', classNameToken);
                        state.helpers[name.value] = { kind: 'options', className: className, fields: {} };
                        expectSemicolon();
                    } else if (className === 'DropdownItem') {
                        var itemArgs = parseArguments();
                        if (itemArgs.length) fail('下拉条目应为无参构造 new DropdownItem()', classNameToken);
                        state.helpers[name.value] = { kind: 'item', fields: {} };
                        expectSemicolon();
                    } else {
                        fail('不支持的构造类 ' + className, classNameToken);
                    }
                } else if (isIdent(token)) {
                    var target = next();
                    if (!at('.'))
                        fail('仅支持编辑器生成的脚本结构（let 声明或辅助变量赋值）', target);
                    eat('.');
                    var field = next();
                    if (!isIdent(field)) fail('应为字段名', field);
                    eat('=');
                    var valueToken = next();
                    if (!isString(valueToken) && !isNumber(valueToken) && !isBool(valueToken)) fail('字段赋值仅支持字面量', valueToken);

                    var helper = state.helpers[target.value];
                    if (!helper) fail('赋值目标 ' + target.value + ' 未预先声明', target);
                    if (helper.kind === 'options' || helper.kind === 'item')
                        helper.fields[field.value] = isBool(valueToken) ? boolValue(valueToken) : valueToken.value;
                    else fail(target.value + ' 不是选项或下拉条目变量', target);
                    expectSemicolon();
                } else {
                    fail('仅支持 let 声明与辅助变量赋值', token);
                }
            }

            if (!formBinding) return { ok: false, errors: [{ line: 0, message: '未找到表单声明（let 变量 = new 表单类(…) { … };）' }] };

            return { ok: true, binding: formBinding, helpers: state.helpers };
        } catch (error) {
            if (error && error.line !== undefined) return { ok: false, errors: [{ line: error.line, message: error.message }] };
            return { ok: false, errors: [{ line: 0, message: '解析失败: ' + (error && error.message ? error.message : String(error)) }] };
        }
    }

    function documentFromBinding(binding, helpers) {
        var formDescriptor = Schema.findFormByName(binding.className);
        if (!formDescriptor) return { ok: false, errors: [{ line: binding.line, message: '未知表单类 ' + binding.className }] };

        var errors = [];
        var document = Model.makeDocument(formDescriptor.kind);
        document.properties.variable = binding.variable;

        if (binding.args.length < 2 || binding.args[0].kind !== 'string' || binding.args[1].kind !== 'string') {
            errors.push({ line: binding.line, message: '表单构造参数应为 (标识, 标题)' });
        } else {
            document.properties.id = binding.args[0].value;
            document.properties.title = binding.args[1].value;
        }

        if (formDescriptor.kind === 'Paginated') {
            if (binding.args.length < 4 || binding.args[2].kind !== 'array') {
                errors.push({ line: binding.line, message: '分页表单构造应为 (标识, 标题, 条目数组, 每页条数)' });
            } else {
                document.properties.pages = binding.args[2].elements.map(function (element) {
                    if (element.kind === 'string') return element.value;
                    var item = helpers[element.name];
                    return item && item.kind === 'item' && item.fields.label !== undefined ? String(item.fields.label) : element.name;
                }).join('\n');
                if (binding.args[3].kind === 'number') document.properties.pageSize = binding.args[3].value;
            }
        }

        var showSeen = false;

        binding.calls.forEach(function (call) {
            if (call.name === 'show') {
                if (call.args.length) errors.push({ line: call.line, message: 'show() 不能携带参数' });
                showSeen = true;
                return;
            }

            var descriptor = Schema.findControlByName(call.name);
            if (!descriptor || formDescriptor.controls.indexOf(descriptor.kind) === -1) {
                errors.push({ line: call.line, message: '控件 ' + call.name + ' 不能用于 ' + formDescriptor.name });
                return;
            }

            var control = Model.makeControl(descriptor.kind);
            var position = 0;
            var minimumCount = 0;
            var maximumCount = 0;

            descriptor.properties.forEach(function (propertyDescriptor) {
                if (!propertyDescriptor.argument) return;
                maximumCount++;
                if (propertyDescriptor.required) minimumCount++;

                var arg = call.args[position++];

                if (descriptor.observable && propertyDescriptor.key === descriptor.observableKey) {
                    if (!arg || arg.kind !== 'reference' || !helpers[arg.name] || helpers[arg.name].kind !== 'observable') {
                        errors.push({ line: call.line, message: descriptor.label + ' 的 ' + propertyDescriptor.label + ' 应为可观察对象变量' });
                        return;
                    }
                    control.properties[propertyDescriptor.key] = helpers[arg.name].initial;
                    return;
                }

                if (propertyDescriptor.type === 'items') {
                    var elements = null;
                    if (arg && arg.kind === 'reference' && helpers[arg.name] && helpers[arg.name].kind === 'itemsArray')
                        elements = helpers[arg.name].elements;
                    else if (arg && arg.kind === 'array')
                        elements = arg.elements;

                    if (!elements) {
                        errors.push({ line: call.line, message: descriptor.label + ' 的选项列表应为条目数组变量' });
                        return;
                    }
                    control.properties.items = elements.map(function (element) {
                        if (element.kind === 'string') return element.value;
                        var item = helpers[element.name];
                        return item && item.kind === 'item' && item.fields.label !== undefined ? String(item.fields.label) : element.name;
                    }).join('\n');
                    return;
                }

                if (!arg) return;

                if (propertyDescriptor.type === 'text' && arg.kind === 'string') control.properties[propertyDescriptor.key] = arg.value;
                else if (propertyDescriptor.type === 'text' && arg.kind === 'number') control.properties[propertyDescriptor.key] = String(arg.value);
                else if (propertyDescriptor.type === 'boolean' && arg.kind === 'boolean') control.properties[propertyDescriptor.key] = arg.value;
                else if (propertyDescriptor.type === 'number' && arg.kind === 'number') control.properties[propertyDescriptor.key] = arg.value;
                else errors.push({ line: call.line, message: descriptor.label + ' 的 ' + propertyDescriptor.label + ' 参数类型不匹配' });
            });

            if (descriptor.hasCallback) {
                minimumCount++;
                maximumCount++;
                var callback = call.args[position++];
                if (!callback || callback.kind !== 'callback')
                    errors.push({ line: call.line, message: descriptor.label + ' 必须携带 on: 回调占位' });
            }

            if (descriptor.optionsClass) {
                minimumCount++;
                maximumCount++;
                var options = call.args[position++];
                if (options && options.kind === 'reference' && helpers[options.name] && helpers[options.name].kind === 'options') {
                    var fields = helpers[options.name].fields;
                    if (helpers[options.name].className !== descriptor.optionsClass)
                        errors.push({ line: call.line, message: descriptor.label + ' 的选项类型应为 ' + descriptor.optionsClass });
                    descriptor.properties.forEach(function (propertyDescriptor) {
                        if (propertyDescriptor.argument) return;
                        if (Object.prototype.hasOwnProperty.call(fields, propertyDescriptor.key))
                            control.properties[propertyDescriptor.key] = fields[propertyDescriptor.key];
                    });
                } else if (options && options.kind === 'inline' && options.className === descriptor.optionsClass) {
                    void 0;
                } else {
                    errors.push({ line: call.line, message: descriptor.label + ' 缺少 ' + descriptor.optionsClass + ' 选项参数' });
                }
            }

            if (call.args.length < minimumCount || call.args.length > maximumCount)
                errors.push({ line: call.line, message: descriptor.label + ' 参数数量不符（期望 ' + minimumCount + ' 到 ' + maximumCount + ' 个，实际 ' + call.args.length + ' 个）' });

            document.controls.push(control);
        });

        if (!showSeen) errors.push({ line: binding.line, message: '声明式块末尾必须调用 show();' });

        if (errors.length) return { ok: false, errors: errors };
        return { ok: true, document: document };
    }

    function importDocument(source) {
        var parsed = parse(source);
        if (!parsed.ok) return parsed;
        return documentFromBinding(parsed.binding, parsed.helpers);
    }

    return { emit: emit, parse: parse, importDocument: importDocument, quote: quote, numberLiteral: numberLiteral };
})();
