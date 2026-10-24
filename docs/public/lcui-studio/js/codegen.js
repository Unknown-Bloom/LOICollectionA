var LCUI = globalThis.LCUI = globalThis.LCUI || {};

LCUI.Codegen = (function () {
    'use strict';

    var Model = LCUI.Model;
    var Schema = LCUI.Schema;

    var optionsClasses = ['TextOptions', 'ButtonOptions', 'TextFieldOptions', 'DropdownOptions', 'SliderOptions', 'ToggleOptions', 'DividerOptions', 'SpacingOptions'];
    var observableClasses = { ObservableString: 'String', ObservableBoolean: 'Boolean', ObservableNumber: 'Number' };
    var recordClasses = ['MenuItemData', 'ShopData', 'ShopItemData', 'ScoreRequirement'];
    var resultMembers = {
        selection: 'resultSelection',
        selectionIndex: 'resultIndex',
        closeReason: 'resultCloseReason',
        actionIndex: 'resultActionIndex',
        resultCode: 'resultCode',
        itemIndex: 'resultItemIndex',
        fromId: 'resultFromId'
    };
    var optionKeys = ['description', 'tooltip', 'disabled', 'visible', 'step'];

    function isOptionProperty(propertyDescriptor) {
        return !propertyDescriptor.argument && optionKeys.indexOf(propertyDescriptor.key) !== -1;
    }

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
            return isOptionProperty(propertyDescriptor) && !isDefault(control, propertyDescriptor);
        });
    }

    function emit(document) {
        var issues = Model.validate(document);
        if (issues.length) return { ok: false, issues: issues };

        var prefix = Model.isIdentifier(document.properties.variable) ? document.properties.variable : 'form';

        var helperLines = [];
        var ordinals = {};
        var entries = document.controls.map(function (control) {
            var descriptor = Schema.findControl(control.kind);
            var ordinal = (ordinals[descriptor.name] = (ordinals[descriptor.name] || 0) + 1);
            return { control: control, descriptor: descriptor, base: prefix + '_' + descriptor.name + '_' + ordinal };
        });

        function scoreHelpers(base, value) {
            var references = [];
            Model.scoreEntries(value).forEach(function (entry, index) {
                if (!entry.valid) return;
                var name = base + '_score_' + index;
                helperLines.push('let ' + name + ' = new ScoreRequirement();');
                helperLines.push(name + '.objective = ' + quote(entry.objective) + ';');
                helperLines.push(name + '.value = ' + numberLiteral(entry.value) + ';');
                references.push(name);
            });
            return references;
        }

        entries.forEach(function (entry) {
            var control = entry.control;
            var descriptor = entry.descriptor;
            var base = entry.base;

            if (control.kind === 'MenuButton') {
                helperLines.push('let ' + base + '_action = new MenuItemData();');
                helperLines.push(base + '_action.type = ' + quote(control.properties.actionType || 'button') + ';');
                helperLines.push(base + '_action.title = ' + quote(control.properties.text) + ';');
                if (String(control.properties.actionId || '').trim())
                    helperLines.push(base + '_action.id = ' + quote(control.properties.actionId) + ';');
                helperLines.push(base + '_action.permission = ' + numberLiteral(control.properties.permission) + ';');
                var menuRun = Model.splitLines(control.properties.run);
                if (menuRun.length)
                    helperLines.push(base + '_action.run = [' + menuRun.map(quote).join(', ') + '];');
                var menuScores = scoreHelpers(base, control.properties.scores);
                if (menuScores.length)
                    helperLines.push(base + '_action.scores = [' + menuScores.join(', ') + '];');
                if (hasCustomOptions(control, descriptor)) {
                    helperLines.push('let ' + base + '_options = new ButtonOptions();');
                    descriptor.properties.forEach(function (propertyDescriptor) {
                        if (isOptionProperty(propertyDescriptor) && !isDefault(control, propertyDescriptor))
                            helperLines.push(base + '_options.' + propertyDescriptor.key + ' = ' + optionValue(propertyDescriptor, control.properties[propertyDescriptor.key]) + ';');
                    });
                }
            } else if (control.kind === 'ShopItem') {
                helperLines.push('let ' + base + ' = new ShopItemData();');
                helperLines.push(base + '.type = ' + quote(control.properties.type || 'commodity') + ';');
                helperLines.push(base + '.title = ' + quote(control.properties.title) + ';');
                ['id', 'introduce', 'number', 'nbt', 'confirmButton', 'cancelButton'].forEach(function (key) {
                    if (String(control.properties[key] || '').trim())
                        helperLines.push(base + '.' + key + ' = ' + quote(control.properties[key]) + ';');
                });
                if (Number(control.properties.time))
                    helperLines.push(base + '.time = ' + numberLiteral(control.properties.time) + ';');
                var itemScores = scoreHelpers(base, control.properties.scores);
                if (itemScores.length)
                    helperLines.push(base + '.scores = [' + itemScores.join(', ') + '];');
            } else {
                if (descriptor.observable === 'String')
                    helperLines.push('let ' + base + '_value = new ObservableString(' + quote(control.properties[descriptor.observableKey]) + ', true);');
                if (descriptor.observable === 'Boolean')
                    helperLines.push('let ' + base + '_value = new ObservableBoolean(' + (control.properties[descriptor.observableKey] ? 'true' : 'false') + ', true);');
                if (descriptor.observable === 'Number')
                    helperLines.push('let ' + base + '_value = new ObservableNumber(' + numberLiteral(control.properties[descriptor.observableKey]) + ', true);');

                if (control.kind === 'Dropdown') {
                    Model.splitLines(control.properties.items).forEach(function (item, index) {
                        helperLines.push('let ' + base + '_items_' + index + ' = new DropdownItem();');
                        helperLines.push(base + '_items_' + index + '.label = ' + quote(item) + ';');
                        helperLines.push(base + '_items_' + index + '.value = ' + index + ';');
                    });
                    var references = Model.splitLines(control.properties.items).map(function (item, index) { return base + '_items_' + index; });
                    helperLines.push('let ' + base + '_items = [' + references.join(', ') + '];');
                }

                if (hasCustomOptions(control, descriptor)) {
                    helperLines.push('let ' + base + '_options = new ' + descriptor.optionsClass + '();');
                    descriptor.properties.forEach(function (propertyDescriptor) {
                        if (isOptionProperty(propertyDescriptor) && !isDefault(control, propertyDescriptor))
                            helperLines.push(base + '_options.' + propertyDescriptor.key + ' = ' + optionValue(propertyDescriptor, control.properties[propertyDescriptor.key]) + ';');
                    });
                }
            }
        });

        var shopVariable = prefix + '_shop';
        if (document.kind === 'Shop') {
            helperLines.push('let ' + shopVariable + ' = new ShopData();');
            helperLines.push(shopVariable + '.id = ' + quote(document.properties.id) + ';');
            helperLines.push(shopVariable + '.type = ' + quote(document.properties.type || 'buy') + ';');
            helperLines.push(shopVariable + '.title = ' + quote(document.properties.title) + ';');
            ['content', 'exitCommand', 'scoreCommand', 'titleCommand', 'itemCommand'].forEach(function (key) {
                if (String(document.properties[key] || '').trim())
                    helperLines.push(shopVariable + '.' + key + ' = ' + quote(document.properties[key]) + ';');
            });
            var itemReferences = entries.filter(function (entry) { return entry.control.kind === 'ShopItem'; }).map(function (entry) { return entry.base; });
            if (itemReferences.length)
                helperLines.push(shopVariable + '.items = [' + itemReferences.join(', ') + '];');
        }

        var formDescriptor = Schema.findForm(document.kind);
        var constructor = 'let ' + prefix + ' = new ' + formDescriptor.name + '(' + quote(document.properties.id);
        if (document.kind === 'Shop') {
            constructor += ', ' + shopVariable + ') {';
        } else {
            constructor += ', ' + quote(document.properties.title);
            if (document.kind === 'Paginated') {
                var pages = Model.splitLines(document.properties.pages).map(function (entry) { return quote(entry); }).join(', ');
                constructor += ', [' + pages + '], ' + numberLiteral(document.properties.pageSize);
            }
            constructor += ') {';
        }

        var source = '';
        if (helperLines.length) source += helperLines.join('\n') + '\n\n';
        source += constructor + '\n';

        if (document.kind !== 'Shop') {
            entries.forEach(function (entry) {
                var control = entry.control;
                var descriptor = entry.descriptor;
                var base = entry.base;
                var args = [];

                if (control.kind === 'MenuButton') {
                    args.push(quote(control.properties.text));
                    args.push(base + '_action');
                } else {
                    descriptor.properties.forEach(function (propertyDescriptor) {
                        if (!propertyDescriptor.argument) return;
                        if (!propertyDescriptor.required && propertyDescriptor.type === 'text' &&
                            propertyDescriptor.key !== descriptor.observableKey &&
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
                }

                if (descriptor.hasCallback) {
                    var bcb = control.callbacks && control.callbacks.on;
                    if (bcb && bcb.blocks && bcb.blocks.length) {
                        args.push('on: func () -> void {\n' + LCUI.Blocks.generate(bcb, { indent: 2 }) + '\n    }');
                    } else {
                        args.push('on: func () -> void {\n    }');
                    }
                }

                if (descriptor.optionsClass) {
                    if (hasCustomOptions(control, descriptor)) args.push(base + '_options');
                    else args.push('new ' + descriptor.optionsClass + '()');
                }

                source += '    ' + descriptor.name + '(' + args.join(', ') + ');\n';
            });
        }

        var shownWs = document.callbacks && document.callbacks.shown;
        if (shownWs && shownWs.blocks && shownWs.blocks.length) {
            source += '    show(func (result) -> void {\n' + LCUI.Blocks.generate(shownWs, { indent: 2 }) + '\n    });\n};\n';
        } else {
            source += '    show();\n};\n';
        }

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

            if (entry === '=' && text.charAt(position + 1) === '=') { tokens.push({ type: 'punct', value: '==', line: startLine }); position += 2; continue; }
            if (entry === '!' && text.charAt(position + 1) === '=') { tokens.push({ type: 'punct', value: '!=', line: startLine }); position += 2; continue; }
            if (entry === '>' && text.charAt(position + 1) === '=') { tokens.push({ type: 'punct', value: '>=', line: startLine }); position += 2; continue; }
            if (entry === '<' && text.charAt(position + 1) === '=') { tokens.push({ type: 'punct', value: '<=', line: startLine }); position += 2; continue; }
            if (entry === '&' && text.charAt(position + 1) === '&') { tokens.push({ type: 'punct', value: '&&', line: startLine }); position += 2; continue; }
            if (entry === '|' && text.charAt(position + 1) === '|') { tokens.push({ type: 'punct', value: '||', line: startLine }); position += 2; continue; }

            if ('(){}[];,.:=!+'.indexOf(entry) !== -1) {
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
                            args.push(parseFuncLiteral());
                            continue;
                        }

                        if (token.value === 'func') {
                            args.push(parseFuncLiteral());
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

            function parseReferenceArray() {
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
                return elements;
            }

            function readLiteral() {
                var token = peek();
                if (isString(token) || isNumber(token)) { next(); return token.value; }
                if (isBool(token)) { next(); return boolValue(token); }
                fail('字段赋值仅支持字面量或变量数组', token);
            }

            function mk(defId) { return LCUI.Blocks.make(defId); }
            function setI(block, key, child) { if (child) block.inputs[key] = child; }
            function parseFuncLiteral() {
                eat('func');
                eat('(');
                if (!at(')')) { var p = next(); if (!isIdent(p)) fail('func 参数应为标识符', p); }
                eat(')');
                eat('->');
                eat('void');
                eat('{');
                var body = parseStatements('}');
                return { kind: 'callback', body: body };
            }
            function parseStatements(term) {
                var list = [];
                while (!at(term) && !at('eof')) {
                    var st = parseStatement();
                    if (st) list.push(st);
                }
                if (!at(term)) fail('回调体缺少 ' + term, peek());
                next();
                return list;
            }
            function bin(op, l, r, extra) {
                var b = mk(op);
                setI(b, 'a', l); setI(b, 'b', r);
                if (extra != null) b.fields.op = extra;
                return b;
            }
            function un(op, e) { var b = mk(op); setI(b, 'a', e); return b; }
            function parseExpression() { return parseOr(); }
            function parseOr() { var l = parseAnd(); while (at('||')) { next(); l = bin('or', l, parseAnd()); } return l; }
            function parseAnd() { var l = parseCmp(); while (at('&&')) { next(); l = bin('and', l, parseCmp()); } return l; }
            function parseCmp() {
                var l = parseAdd();
                while (peek().type === 'punct' && ['==', '!=', '>', '<', '>=', '<='].indexOf(peek().value) >= 0) {
                    var op = next().value;
                    l = bin('compare', l, parseAdd(), op);
                }
                return l;
            }
            function parseAdd() { var l = parseUnary(); while (at('+')) { next(); l = bin('concat', l, parseUnary()); } return l; }
            function parseUnary() { if (at('!')) { next(); return un('not', parseUnary()); } return parsePrimary(); }
            function parsePrimary() {
                var t = peek();
                if (at('(')) { next(); var e = parseExpression(); eat(')'); return e; }
                if (at('[')) {
                    next();
                    if (at(']')) { next(); return mk('emptyList'); }
                    var a = parseExpression();
                    if (at(',')) { next(); var b = parseExpression(); eat(']'); var bl = mk('list2'); setI(bl, 'a', a); setI(bl, 'b', b); return bl; }
                    eat(']'); var bl2 = mk('list2'); setI(bl2, 'a', a); setI(bl2, 'b', mk('emptyList')); return bl2;
                }
                if (isString(t)) { next(); var b = mk('text'); b.fields.value = t.value; return b; }
                if (isNumber(t)) { next(); var b = mk('number'); b.fields.value = t.value; return b; }
                if (isBool(t)) { next(); var b = mk('bool'); b.fields.value = boolValue(t); return b; }
                if (isIdent(t)) {
                    var w = next().value;
                    if (w === 'GUIManager' && peek().value === ':') {
                        next(); eat(':'); var m = next(); eat('(');
                        if (m.value === 'request') { var act = next(); eat(','); var ar = parseExpression(); eat(')'); var b = mk('request'); b.fields.action = act.value; setI(b, 'args', ar); return b; }
                        if (m.value === 'value') { var k = next(); eat(')'); var b = mk('valueOf'); b.fields.key = k.value; return b; }
                        fail('不支持的表达式 GUIManager.' + m.value, m);
                    }
                    if (peek().value === '.') {
                        next(); var mem = next();
                        if (mem.value === 'value') { var b = mk('obsValue'); b.fields.obs = w; return b; }
                        if (mem.value === 'getData') { eat('('); eat(')'); var b = mk('obsData'); b.fields.obs = w; return b; }
                        if (resultMembers[mem.value]) return mk(resultMembers[mem.value]);
                        fail('无法识别的成员 .' + mem.value, mem);
                    }
                    var b = mk('varRef'); b.fields.name = w; return b;
                }
                fail('无法识别的表达式', t);
            }
            function parseStatement() {
                var token = peek();
                if (!isIdent(token)) fail('无法识别的语句', token);
                var w = token.value;
                if (peek(1).type === 'punct' && peek(1).value === '.' && peek(2).value === 'close') {
                    next(); eat('.'); eat('close'); eat('('); eat(')'); expectSemicolon(); return mk('closeForm');
                }
                if (w === 'this') {
                    next(); eat('.'); eat('close'); eat('('); eat(')'); expectSemicolon(); return mk('closeForm');
                }
                if (w === 'break') { next(); expectSemicolon(); return mk('break'); }
                if (w === 'continue') { next(); expectSemicolon(); return mk('continue'); }
                if (w === 'panic') { next(); eat('('); var e = parseExpression(); eat(')'); expectSemicolon(); var b = mk('panic'); setI(b, 'msg', e); return b; }
                if (w === 'let') {
                    next(); var name = next(); if (!isIdent(name)) fail('let 后应为变量名', name);
                    eat('='); var e = parseExpression(); expectSemicolon();
                    var b = mk('setVar'); b.fields.name = name.value; setI(b, 'value', e); return b;
                }
                if (w === 'if') {
                    next(); var c = parseExpression(); eat('[');
                    var body = parseStatements(']');
                    var els = [];
                    if (at(':')) { next(); eat('['); els = parseStatements(']'); }
                    var b = mk('if'); setI(b, 'cond', c); b.inner.body = body; b.inner.else = els; return b;
                }
                if (w === 'while') {
                    next(); var c = parseExpression(); eat('[');
                    var body = parseStatements(']'); var b = mk('while'); setI(b, 'cond', c); b.inner.body = body; return b;
                }
                if (w === 'for') {
                    next(); eat('(');
                    if (at('let')) {
                        next(); var v = next(); eat('='); eat('0'); eat(';'); var vn = v.value; eat(';');
                        var cnt = parseExpression(); eat('++'); eat(')'); eat('[');
                        var body = parseStatements(']'); var b = mk('forCount'); b.fields.name = vn; setI(b, 'count', cnt); b.inner.body = body; return b;
                    }
                    var v2 = next(); eat('in'); var lst = parseExpression(); eat(')'); eat('[');
                    var body = parseStatements(']'); var b = mk('forIn'); b.fields.name = v2.value; setI(b, 'list', lst); b.inner.body = body; return b;
                }
                if (w === 'GUIManager') {
                    next(); eat(':'); eat(':'); var method = next();
                    if (!isIdent(method)) fail('GUIManager 方法调用', method);
                    eat('(');
                    if (method.value === 'switchTo') {
                        var id = next(); eat(','); var ty = next(); eat(')'); expectSemicolon();
                        var b = mk('switchTo'); b.fields.formId = id.value; b.fields.formType = ty.value; return b;
                    }
                    if (method.value === 'open') {
                        var owner = next(); eat(','); var fid = next(); eat(','); var ty = next(); eat(','); var params = parseExpression(); eat(')'); expectSemicolon();
                        var b = mk('openForm'); b.fields.owner = owner.value; b.fields.formId = fid.value; b.fields.formType = ty.value; setI(b, 'params', params); return b;
                    }
                    if (method.value === 'callback') {
                        var act = next(); eat(','); var ar = parseExpression(); eat(')'); expectSemicolon();
                        var b = mk('callback'); b.fields.action = act.value; setI(b, 'args', ar); return b;
                    }
                    if (method.value === 'request') {
                        var act = next(); eat(','); var ar = parseExpression(); eat(')');
                        var b = mk('request'); b.fields.action = act.value; setI(b, 'args', ar); return b;
                    }
                    if (method.value === 'value') {
                        var k = next(); eat(')');
                        var b = mk('valueOf'); b.fields.key = k.value; return b;
                    }
                    fail('不支持的 GUIManager 方法 ' + method.value, method);
                }
                if (peek(1).type === 'punct' && peek(1).value === '.') {
                    next(); eat('.'); var mem = next();
                    if (mem.value === 'value' && peek().value === '=') {
                        next(); var e = parseExpression(); expectSemicolon();
                        var b = mk('assignObs'); b.fields.obs = w; setI(b, 'value', e); return b;
                    }
                    if (mem.value === 'getData' && peek().value === '(') { next(); eat(')'); var b = mk('obsData'); b.fields.obs = w; return b; }
                    if (resultMembers[mem.value]) return mk(resultMembers[mem.value]);
                    fail('无法识别的成员 .' + mem.value, mem);
                }
                if (peek().value === '=') {
                    next(); var e = parseExpression(); expectSemicolon();
                    var b = mk('assignObs'); b.fields.obs = w; setI(b, 'value', e); return b;
                }
                fail('无法识别的语句 ' + w, token);
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
                    } else if (recordClasses.indexOf(className) !== -1) {
                        var recordArgs = parseArguments();
                        if (recordArgs.length) fail(className + ' 应为无参构造', classNameToken);
                        state.helpers[name.value] = { kind: 'record', className: className, fields: {} };
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

                    var helper = state.helpers[target.value];
                    if (!helper) fail('赋值目标 ' + target.value + ' 未预先声明', target);

                    if (helper.kind === 'record') {
                        if (at('[')) helper.fields[field.value] = { kind: 'array', elements: parseReferenceArray() };
                        else helper.fields[field.value] = readLiteral();
                    } else {
                        var valueToken = next();
                        if (!isString(valueToken) && !isNumber(valueToken) && !isBool(valueToken)) fail('字段赋值仅支持字面量', valueToken);
                        if (helper.kind === 'options' || helper.kind === 'item')
                            helper.fields[field.value] = isBool(valueToken) ? boolValue(valueToken) : valueToken.value;
                        else fail(target.value + ' 不是选项或下拉条目变量', target);
                    }
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

    function recordOf(helpers, arg) {
        if (!arg || arg.kind !== 'reference' || !helpers[arg.name] || helpers[arg.name].kind !== 'record') return null;
        return helpers[arg.name];
    }

    function recordArray(record, key) {
        var value = record && record.fields[key];
        return value && value.kind === 'array' ? value.elements : [];
    }

    function arrayStrings(value, helpers) {
        if (!value || value.kind !== 'array') return [];
        return value.elements.map(function (element) {
            if (element.kind === 'string') return element.value;
            var item = helpers[element.name];
            return item && item.kind === 'record' && item.fields.label !== undefined ? String(item.fields.label) : element.name;
        });
    }

    function scoreLines(record, helpers) {
        return recordArray(record, 'scores').map(function (element) {
            var score = recordOf(helpers, element);
            if (!score || score.className !== 'ScoreRequirement') return '';
            return String(score.fields.objective || '') + ':' + Number(score.fields.value || 0);
        }).filter(function (line) { return line.length > 0; }).join('\n');
    }

    function controlForCall(formDescriptor, call, helpers) {
        if (call.name === 'button' && formDescriptor.kind === 'Menu') {
            var action = recordOf(helpers, call.args[1]);
            if (action && action.className === 'MenuItemData') return Schema.findControl('MenuButton');
        }
        return Schema.findControlByName(call.name, formDescriptor.kind);
    }

    function applyOptions(control, descriptor, arg, helpers, errors, line) {
        if (arg && arg.kind === 'reference' && helpers[arg.name] && helpers[arg.name].kind === 'options') {
            var fields = helpers[arg.name].fields;
            if (helpers[arg.name].className !== descriptor.optionsClass)
                errors.push({ line: line, message: descriptor.label + ' 的选项类型应为 ' + descriptor.optionsClass });
            descriptor.properties.forEach(function (propertyDescriptor) {
                if (!isOptionProperty(propertyDescriptor)) return;
                if (Object.prototype.hasOwnProperty.call(fields, propertyDescriptor.key))
                    control.properties[propertyDescriptor.key] = fields[propertyDescriptor.key];
            });
        } else if (!(arg && arg.kind === 'inline' && arg.className === descriptor.optionsClass)) {
            errors.push({ line: line, message: descriptor.label + ' 缺少 ' + descriptor.optionsClass + ' 选项参数' });
        }
    }

    function attachCallback(control, callback) {
        if (callback && callback.body && callback.body.length) {
            control.callbacks = control.callbacks || {};
            control.callbacks.on = LCUI.Blocks.workspace();
            control.callbacks.on.blocks = callback.body;
        }
    }

    function shopDocument(binding, helpers, formDescriptor) {
        var errors = [];
        var document = Model.makeDocument(formDescriptor.kind);
        document.properties.variable = binding.variable;

        if (binding.args.length !== 2 || binding.args[0].kind !== 'string') {
            errors.push({ line: binding.line, message: '商店表单构造应为 (标识, ShopData 变量)' });
        } else {
            document.properties.id = binding.args[0].value;
        }

        var data = recordOf(helpers, binding.args[1]);
        if (!data || data.className !== 'ShopData') {
            errors.push({ line: binding.line, message: '商店表单缺少 ShopData 数据变量' });
        } else {
            document.properties.title = String(data.fields.title || '');
            document.properties.type = String(data.fields.type || 'buy');
            ['content', 'exitCommand', 'scoreCommand', 'titleCommand', 'itemCommand'].forEach(function (key) {
                document.properties[key] = String(data.fields[key] || '');
            });
            recordArray(data, 'items').forEach(function (element) {
                var item = recordOf(helpers, element);
                if (!item || item.className !== 'ShopItemData') {
                    errors.push({ line: binding.line, message: '商店条目应为 ShopItemData 变量' });
                    return;
                }
                var control = Model.makeControl('ShopItem');
                ['type', 'title', 'id', 'introduce', 'number', 'nbt', 'confirmButton', 'cancelButton'].forEach(function (key) {
                    if (item.fields[key] !== undefined) control.properties[key] = String(item.fields[key]);
                });
                if (item.fields.time !== undefined) control.properties.time = Number(item.fields.time);
                control.properties.scores = scoreLines(item, helpers);
                document.controls.push(control);
            });
        }

        var showSeen = false;
        binding.calls.forEach(function (call) {
            if (call.name === 'show') {
                if (call.args.length === 1 && call.args[0].kind === 'callback') document.shownBody = call.args[0].body;
                else if (call.args.length) errors.push({ line: call.line, message: 'show() 仅支持 show(func (result) -> void { … }) 形式' });
                showSeen = true;
                return;
            }
            errors.push({ line: call.line, message: '商店表单不支持 ' + call.name + '()' });
        });
        if (!showSeen) errors.push({ line: binding.line, message: '声明式块末尾必须调用 show();' });

        if (document.shownBody && document.shownBody.length) {
            document.callbacks = document.callbacks || {};
            document.callbacks.shown = LCUI.Blocks.workspace();
            document.callbacks.shown.blocks = document.shownBody;
        }

        if (errors.length) return { ok: false, errors: errors };
        return { ok: true, document: document };
    }

    function documentFromBinding(binding, helpers) {
        var formDescriptor = Schema.findFormByName(binding.className);
        if (!formDescriptor) return { ok: false, errors: [{ line: binding.line, message: '未知表单类 ' + binding.className }] };
        if (formDescriptor.kind === 'Shop') return shopDocument(binding, helpers, formDescriptor);

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
                if (call.args.length === 1 && call.args[0].kind === 'callback') {
                    binding.shownBody = call.args[0].body;
                } else if (call.args.length) {
                    errors.push({ line: call.line, message: 'show() 仅支持 show(func (result) -> void { … }) 形式' });
                }
                showSeen = true;
                return;
            }

            var descriptor = controlForCall(formDescriptor, call, helpers);
            if (!descriptor) {
                errors.push({ line: call.line, message: '控件 ' + call.name + ' 不能用于 ' + formDescriptor.name });
                return;
            }

            if (descriptor.kind === 'MenuButton') {
                var menuControl = Model.makeControl('MenuButton');
                if (call.args.length !== 4 || call.args[0].kind !== 'string') {
                    errors.push({ line: call.line, message: '菜单按钮应为 button(文本, 动作, on: 回调, 选项)' });
                    return;
                }
                menuControl.properties.text = call.args[0].value;
                var action = recordOf(helpers, call.args[1]);
                if (!action || action.className !== 'MenuItemData') {
                    errors.push({ line: call.line, message: '菜单按钮缺少 MenuItemData 动作变量' });
                } else {
                    if (action.fields.id !== undefined) menuControl.properties.actionId = String(action.fields.id);
                    if (action.fields.type !== undefined) menuControl.properties.actionType = String(action.fields.type);
                    if (action.fields.permission !== undefined) menuControl.properties.permission = Number(action.fields.permission);
                    menuControl.properties.run = arrayStrings(action.fields.run, helpers).join('\n');
                    menuControl.properties.scores = scoreLines(action, helpers);
                }
                var menuCallback = call.args[2];
                if (!menuCallback || menuCallback.kind !== 'callback')
                    errors.push({ line: call.line, message: '菜单按钮必须携带 on: 回调占位' });
                attachCallback(menuControl, menuCallback);
                applyOptions(menuControl, descriptor, call.args[3], helpers, errors, call.line);
                document.controls.push(menuControl);
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
                applyOptions(control, descriptor, call.args[position++], helpers, errors, call.line);
            }

            if (call.args.length < minimumCount || call.args.length > maximumCount)
                errors.push({ line: call.line, message: descriptor.label + ' 参数数量不符（期望 ' + minimumCount + ' 到 ' + maximumCount + ' 个，实际 ' + call.args.length + ' 个）' });

            document.controls.push(control);

            if (descriptor.hasCallback) {
                var cbArg = null;
                for (var ci = 0; ci < call.args.length; ci++) {
                    if (call.args[ci] && call.args[ci].kind === 'callback') { cbArg = call.args[ci]; break; }
                }
                if (cbArg && cbArg.body && cbArg.body.length) {
                    control.callbacks = control.callbacks || {};
                    control.callbacks.on = LCUI.Blocks.workspace();
                    control.callbacks.on.blocks = cbArg.body;
                }
            }
        });

        if (binding.shownBody && binding.shownBody.length) {
            document.callbacks = document.callbacks || {};
            document.callbacks.shown = LCUI.Blocks.workspace();
            document.callbacks.shown.blocks = binding.shownBody;
        }

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
