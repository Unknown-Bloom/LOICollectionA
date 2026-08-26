<!-- markdownlint-disable MD033 -->

# 常见错误含义

## 1. Unclosed string（未闭合的字符串）

**解释**  
字符串缺少匹配的闭合引号，即 `"..."` 或 `'...'` 没有成对出现。

**说明**  

- 在解析以 `"` 或 `'` 开头的字符串时，Lexer 会不断读入字符，直到遇见对应的闭合引号或输入结束。  
- 如果直到输入结束（`currentChar == 0`）仍未找到闭合引号，则触发该错误。  
- 错误信息示例：`Unclosed string`

**典型错误代码**  

```cpp
"hello       // 缺少闭合双引号
'world       // 缺少闭合单引号
"mixed'      // 引号不匹配，也会被视为未闭合
```

**解决办法**  

- 检查字符串是否以相同类型的引号成对包裹。  
- 确保字符串内容中的同种引号已正确转义（如 `"He said \"Hi\""`），避免提前闭合。

---

## 2. Invalid numeric literal（无效的数值字面量）

**解释**  
Lexer 在尝试识别一个数值时，得到的字符序列无法构成合法的整数或浮点数。

**说明**  

- Lexer 在遇到数字 `0-9` 或小数点 `.` 时进入数字解析。  
- 它会读入连续的数字和至多一个小数点，直到遇见其他字符为止。  
- 如果最终得到的字符串为空字符串（例如连续遇到两个非数字字符但进入了数字解析，实际不会）或单独一个点 `"."`，则认为是无效的数值字面量。  
- 错误信息示例：`Invalid numeric literal: '.'`

**典型错误代码**  

```cpp
.           // 单独的小数点，前后无数字
.5          // 本代码中 '.' 开头会进入 parseNumber，读入 '.5'，最终 num = ".5"，hasDot=true，返回 TOKEN_FLOAT，不会报错
```

实际上，在当前实现中，**只有 `.` 后面不跟数字的情况会报错**（如单独的 `.` 后面是空格或其他字符，读取后 num 为 `"."`）。  
例如：  

```cpp
a = . ;      // 点号后直接跟空格或分号，num="."
```

而在 `.5` 时，会读到 `5`，结果 `num=".5"` 合法，返回浮点标记。

**解决办法**  

- 确保数值字面量至少包含一位数字，浮点数的小数点前后至少有一侧有数字。  
- 如果需要一个纯小数，请在小数点前补 `0`，如 `.5` → `0.5`。  
- 避免孤立的小数点出现在表达式中。

## 3. Syntax error（语法错误：期望的标记不匹配）

**解释**  
在需要特定语法元素的位置，实际遇到了其他类型的标记。

**说明**  

- Parser 通过 `eat()` 函数检查当前 token 类型是否等于期望值，若不相等则抛出此错误。  
- 几乎所有语法结构都依赖此检查，例如期望 `(` 却遇到 `{`，期望 `;` 却遇到标识符等。  
- 错误信息格式：  
  `Syntax error: Expected <期望标记>, got <实际标记>`

**典型错误代码**  

```cpp
if x > 0      // 缺少左括号，期望 '('，实际得到 IDENT 'x'
{ a = 1 }     // if 体本该用 '[' 而非 '{'
```

**解决办法**  

- 仔细检查语法规则，确保在关键字、标识符、运算符、括号等位置使用了正确的符号。  
- 利用错误信息中的“期望/实际”对比，定位错误的 token 并修正。  
- 多数情况下，这类错误是由前面的括号不匹配、关键字拼写错误或缺少符号连锁引发的，修正第一个错误后其余可能自动消失。

---

## 4. Missing `;` or EOF after statement（语句后缺少分号或文件结束标记）

**解释**  
语句结束后，应当遇到分号 `;` 或文件结束（EOF），但实际遇到了其他 token。

**说明**  

- 在 `parse()` 主循环中，每解析完一条语句，会检查下一个 token 是否为 `;` 或 `EOF`。  
- 如果不是，则报告此错误，错误信息包含实际遇到的 token 类型及其内容。  
- 常见于漏掉分号，或语句后多写了不期望的表达式。

**典型错误代码**  

```cpp
a = 5
b = 6;   // a = 5 后缺少分号，下一行 b 被当作同一语句的延续
```

**解决办法**  

- 确保每条语句都以分号正确结束。  
- 检查是否在语句末尾误写了其他符号（如多余的运算符、括号等），必要时删去或重新组织语句。

---

<h2 id="too_many_arguments_in_function_call">5. Too many args in function call（函数调用参数过多）</h2>

**解释**  
函数或宏的参数个数超过了限制（此处为 100 个）。

**说明**  

- `parseArgs()` 在收集参数时会计数，当 `tpl->parts.size() >= 100` 时触发此错误，错误信息为 `"Too many args in function call"`。  
- 触发后解析停止，返回 `nullptr` 并产生一个错误节点。

**典型错误代码**  

```cpp
func(arg1, arg2, ..., arg101);   // 参数超过100个
```

**解决办法**  

- 减少函数调用的参数个数。  

---

## 6. Invalid integer literal（无效的整数字面量）

**解释**  
整数字面量在语义上不合法，通常是因为数值超出了 `int` 类型的表示范围。

**说明**  

- 词法分析已识别出合法的整数 token（只含数字），但在 Parser 的 `parseValue()` 中，尝试用 `std::from_chars` 将其转换为 `int` 时失败。  
- 失败原因可能为数值溢出（如超出 `int` 最大值 `2147483647`），或含有意外的非数字字符（由于 Lexer 保证格式，故溢出为主因）。  
- 错误信息示例：`Invalid integer literal: 9999999999`

**典型错误代码**  

```cpp
a = 3000000000;   // 超出32位有符号整数范围
```

**解决办法**  

- 检查整数字面量的大小，确保在目标类型的有效范围内。  

---

## 7. Invalid float literal（无效的浮点数字面量）

**解释**  
浮点数字面量无法被正确转换为 `float` 值，原因多为溢出或格式错误。

**说明**  

- 类似整数，词法分析得到 TOKEN_FLOAT 后，Parser 用 `std::from_chars` 转换为 `float`。  
- 可能失败的情况：数值超出 `float` 范围（约 ±3.4e38），或者由于科学计数法等产生极值，导致转换返回 `std::errc::result_out_of_range`；极少情况下可能因为内存对齐等问题导致格式非预期。  
- 错误信息示例：`Invalid float literal: 1e999`

**典型错误代码**  

```cpp
f = 1e999;   // 超出 float 表示范围
```

**解决办法**  

- 控制浮点字面量的量级，使其在 `float` 的范围内（约 1.175e-38 到 3.402e+38）。  

---

## 8. Unexpected value type（意外的值类型）

**解释**  
Parser 在期望一个值（字面量）时，遇到了无法识别的 token 类型。

**说明**  

- 在 `parseValue()` 的 `default` 分支触发，当 token 类型不是 `INT`、`FLOAT`、`STRING`、`BOOL_LIT` 时，即表示当前 token 不能作为一个合法的值。  
- 通常是由于语法错误导致 token 错位，例如本应是表达式的开始，却出现了一个运算符或分隔符。  
- 错误信息示例：`Unexpected value type: +`

**典型错误代码**  

```cpp
x = + ;      // 加号后缺少操作数，+ 被当作值来解析
```

**解决办法**  

- 检查表达式是否完整：运算符两端是否都有合法的操作数。  
- 查看错误位置前的 token，确认是否遗漏了标识符、字面量或子表达式。

## 9. Unknown compare op（未知的比较运算符）

**解释**  
编译器在处理比较表达式时，遇到了无法识别的比较运算符。

**说明**  

- `visit(CompareNode& node)` 中，根据 `node.op` 字符串选择对应的字节码指令（如 `CMP_EQ`、`CMP_GT` 等）。  
- 若 `node.op` 不在预期的集合 `{==, !=, >, <, >=, <=}` 中，则会触发此错误。  
- 在正常的语法分析流程中，比较运算符已被 Parser 限定为上述六种，因此该错误通常意味着 AST 构建异常或内部传递了错误的运算符字符串。  
- 错误信息格式：`Unknown compare op: <运算符>`，位置信息占位为 `{0, 0, 0}`。

**典型错误代码**  

```cpp
// 如果 AST 被手工构造且使用了非法比较符
a <=> b    // 语言不支持三路比较，但 AST 中却出现了 "<=>"
```

**解决办法**  

- 检查 AST 生成过程，确保比较运算符字符串正确无误。  
- 若由用户输入引起，请确认代码中使用的比较运算符是否为语言所支持（如 `==`、`!=`、`>`、`<`、`>=`、`<=`）。  
- 若问题来自自定义 AST 构造，请修正运算符字符串，使用合法的比较符。

---

## 10. Unknown arithmetic op（未知的算术运算符）

**解释**  
编译器在处理算术表达式时，遇到了无法识别的算术运算符。

**说明**  

- `visit(ArithmeticNode& node)` 中，根据 `node.op` 字符串选择对应的字节码指令（如 `ADD`、`SUB`、`MUL`、`DIV`、`MOD`、`POW`）。  
- 若 `node.op` 不在 `{+, -, *, /, %, ^}` 范围内，则触发此错误。  
- 与比较运算符类似，正常情况下 Parser 已保证算术运算符来自这六种，此错误通常指示内部数据错误。  
- 错误信息示例：`Unknown arithmetic op: ~`

**典型错误代码**  

```cpp
// 例如错误地使用了不支持的算术符
a ~ b    // 若 AST 错误记录了 "~" 作为算术运算符
```

**解决办法**  

- 确认表达式中使用的算术运算符是语言支持的六种之一（`+`、`-`、`*`、`/`、`%`、`^`）。  
- 如果正在扩展语言，请确保编译器侧已增加对新运算符的处理分支。  
- 修正 AST 构造过程中可能出现的运算符字符串错误。

---

## 11. Unknown unary op（未知的一元运算符）

**解释**  
编译器在处理一元表达式时，遇到了无法识别的一元运算符。

**说明**  

- `visit(UnaryNode& node)` 中，根据 `node.op` 字符串选择操作：`-` 对应 `NEG`，`!` 对应 `NOT`，`+` 无操作（直接忽略）。  
- 若 `node.op` 不在 `{-, !, +}` 中，则报告此错误。  
- 该错误同样属于防御性检查，表明一元运算符字符串异常。  
- 错误信息示例：`Unknown unary op: #`

**典型错误代码**  

```cpp
// 错误地使用了一元运算符
~a    // 不支持按位取反，但 AST 却记录了 "~"
```

**解决办法**  

- 检查一元运算符是否正确，支持的一元运算符包括：`-`（取负）、`!`（逻辑非）、`+`（一元加，无实际效果）。  
- 如果引入了新的一元操作符，请同步修改编译器中的 `visit(UnaryNode&)` 方法，增加对应处理。  
- 排查 AST 构建阶段是否错误地生成了非法的一元运算符字符串。

## 12. Stack underflow（栈下溢）

**解释**  
试图从空的操作数栈中弹出值，包括普通的弹出操作和复制栈顶（DUP）时栈为空的情况。

**说明**  

- `pop()` 方法在栈为空时会报告 `"Stack underflow"` 并返回一个默认值。  
- 执行 `DUP` 指令时若栈为空，会单独报告 `"Stack underflow during DUP"`。  
- 几乎所有需要操作数的指令（算术、比较、逻辑、赋值等）都会先调用 `pop`，因此该错误可能在多种运算中触发。  
- 根本原因通常是代码逻辑导致栈的消耗与生成不匹配，比如表达式缺少操作数或值被意外弹出。

**典型错误代码**  

```cpp
+ 5;        // 一元加操作数不足，运行时会尝试弹出栈顶发现栈空
x = ;       // 赋值右值缺失，DUP 时栈空
```

**解决办法**  

- 检查表达式是否完整，每个运算符是否都有足够的操作数。  
- 对于赋值语句，确保右值存在且能正确入栈。  
- 借助调试输出栈状态，追踪栈操作不平衡的位置。

---

## 13. Undefined variable（未定义变量）

**解释**  
尝试读取一个从未被赋值或声明的变量。

**说明**  

- 执行 `LOAD_VAR` 指令时，会在变量表 `this->variables` 中查找变量名，若找不到则报错。  
- 错误信息包含变量名：`"Undefined variable: <name>"`。  
- 变量只有在执行 `STORE_VAR` 后才会被记录，因此在赋值前就引用会导致此错误。

**典型错误代码**  

```cpp
y = x + 1;   // x 从未赋值
```

**解决办法**  

- 确保所有变量在使用前已被赋值。  
- 检查变量名是否拼写正确，注意大小写。  
- 如果变量确实应在外部定义，检查初始化顺序或作用域。

---

## 14. Type mismatch in arithmetic（算术运算类型不匹配）

**解释**  
对不可进行算术运算的类型（如字符串与数字混合，且运算符不是 `+`）执行了算术操作。

**说明**  

- 在 `applyArithmetic` 中，当左右操作数不全是算术类型（int/float/bool），且运算符不是用于字符串连接的 `+` 时触发。  
- 例如字符串与数字执行 `-`、`*`、`/` 等运算，或两个字符串执行非 `+` 运算。  
- 错误消息固定为 `"Type mismatch in arithmetic"`。

**典型错误代码**  

```cpp
"hello" * 3;   // 字符串不能做乘法
true - "a";    // bool 被视为算术类型，但右侧是 string，仍会进入类型不匹配分支（非算术类型）
```

**解决办法**  

- 检查运算数类型，确保算术运算的操作数均为数值（int/float/bool）。  
- 字符串只能使用 `+` 进行拼接，其他运算需先转换成数值（若语言支持）或调整逻辑。  
- 如果期望对数值操作，确保变量中存储的是数值类型而非字符串。

---

## 15. Modulo requires integral types（取模运算需要整数类型）

**解释**  
取模运算符 `%` 的操作数必须是整数类型（int 或 bool），不支持浮点数。

**说明**  

- `applyArithmetic` 处理 `%` 时，会检查左右操作数是否均为 `std::is_integral_v` 的整数类型（包括 bool）。  
- 如果其中一个是 `float`，则报告此错误。  
- 错误信息：`"Modulo requires integral types"`。

**典型错误代码**  

```cpp
5.0 % 2;     // 浮点数取模
3.14 % 1.5;  
```

**解决办法**  

- 或者改用库函数 `fmod` 实现浮点取模（需要自行支持）。  
- 检查变量类型，避免无意中将整数写成浮点形式（如 `5.0`）。

---

## 16. Unknown arithmetic op（未知算术运算符）

**解释**  
虚拟机在执行算术运算时遇到了未识别的运算符，属于防御性错误。

**说明**  

- 当左右操作数均为数值类型，但运算符字符串不是 `+`、`-`、`*`、`/`、`%`、`^` 中的任何一个时触发。  
- 正常情况下，编译器（Compiler）和 AST 不会生成非法的算术运算符，因此该错误可能指示字节码损坏或语言扩展未完全实现。  
- 错误信息示例：`"Unknown arithmetic op: ~"`。

**典型错误代码**  
（AST 直接构造错误，不常见）  

```cpp
// 某些非法手段导致字节码中出现算术运算符为 "&"
```

**解决办法**  

- 检查字节码生成过程，确保算术运算符字符串来自正确集合。  
- 若为语言扩展，需同步更新 VM 中的 `applyArithmetic` 分支。

---

## 17. Unknown unary op（未知一元运算符）

**解释**  
虚拟机执行一元运算时遇到未实现的运算符，或对不支持的类型使用了一元 `+`/`-`。

**说明**  

- `applyUnary` 中，合法的一元运算符为 `+`、`-`（仅对数值类型）、`!`（对所有类型）。  
- 若对非数值类型（如 string）使用 `+` 或 `-`，会因不满足 `is_arithmetic` 且未进入 `!` 分支而触发此错误。  
- 若出现其他非法一元运算符（如 `~`），也会触发。  
- 错误信息示例：`"Unknown unary op: -"`（当应用于字符串时）。

**典型错误代码**  

```cpp
-"hello";    // 对字符串取负
+true;       // 合法，bool 是算术类型
~5;          // 如果语言不支持按位取反，且 AST 产生 "~"，会报此错
```

**解决办法**  

- 一元 `+`/`-` 只能用于数值，检查操作数类型是否正确。  
- 对布尔值或字符串进行逻辑非应使用 `!`。  
- 若需支持其他一元操作符，需扩展 VM 的 `applyUnary` 方法。

---

## 18. Unknown comparison op（未知比较运算符）

**解释**  
虚拟机执行比较时运算符不在 `==`、`!=`、`>`、`<`、`>=`、`<=` 之中。

**说明**  

- `applyComparison` 对合法比较符进行飞船运算符 `<=>` 比较，若运算符字符串不匹配则报错。  
- 同样属于防御性错误，正常 AST 不会产生非法比较符。  
- 错误信息示例：`"Unknown comparison op: <=>"`。

**典型错误代码**  
（由错误的字节码引起）  

```cpp
// 字节码中比较运算符为 "==="
```

**解决办法**  

- 确认比较运算符仅限上述六种。  
- 跟踪字节码生成阶段，修复错误的运算符字符串。

---

## 19. Type mismatch in comparison（比较运算类型不匹配）

**解释**  
两个操作数既不是同为算术类型，也不是完全相同类型，无法进行比较。

**说明**  

- 比较运算要求：要么两者均为数值类型（int/float/bool），可以统一转为 double 比较；要么两者为完全相同的数据类型（如两个 string），使用该类型的 `<=>` 比较。  
- 例如 `5 == "5"` 会触发此错误，因为一个是 int 一个是 string，不满足条件。  
- 错误信息：`"Type mismatch in comparison"`。

**典型错误代码**  

```cpp
5 == "5";
true > "false";
```

**解决办法**  

- 比较前将操作数转换为相同类型（如将字符串转为数字）。  
- 检查变量类型，确保比较双方类型兼容。  
- 如果希望字符串与数字比较，需要显式调用转换函数（如果语言提供）。

---

## 20. Instruction limit exceeded（指令执行超限）

**解释**  
虚拟机执行了超过 1,000,000 条指令仍未结束，可能陷入无限循环。

**说明**  

- `run()` 方法中有一个执行计数器 `executed`，每执行一条指令递增，达到上限后报错并终止。  
- 这是一种安全保护机制，防止死循环耗尽资源。  
- 错误信息：`"Instruction limit exceeded (possible infinite loop)"`。

---

## 21. Function/Macro call error（外部函数/宏调用错误）

**解释**  
调用外部注册的函数或宏时，函数/宏内部报告了错误。

**说明**  

- 执行 `CALL` 或 `CALL_MACRO` 指令时，会调用 `FunctionCall::callFunction` 或 `MacroCall::callMacro`，如果返回 `ll::Unexpected` 错误，则将其 `message()` 直接作为错误信息。  
- 错误内容完全取决于外部函数/宏的实现，例如参数数量不对、类型不符、功能限制等。  
- 错误信息格式为 `"Function callback threw: <错误内容>"`、`"Macro callback threw: <错误内容>"` 等，具体内容取决于外部函数/宏的实现。

**典型错误代码**  

```cpp
math::sqrt(-1);     // 若 sqrt 函数内部检查参数并报错
some::func(1, 2);   // 若该函数要求至少3个参数
```

**解决办法**  

- 查看错误信息的具体内容，根据外部模块的文档检查函数名称、参数数量和类型。  
- 确保使用的命名空间和函数名正确注册。  
- 如果错误是内部逻辑引起的（如除零），请检查输入数据。

## 22. Unsupported argument type（不支持的参数类型）

**解释**  
在调用函数或宏时，传入的某个参数的类型不在系统支持的类型列表之中。

**说明**  

- `valuesToTypes()` 函数遍历所有传入的参数值（`CallbackTypeValues`），通过 `std::visit` 检查每个参数的实际类型。  
- 支持的类型仅有 `int`、`float`、`std::string`、`bool` 四种。若某个参数不是这四种类型之一，则会触发此错误。  
- 由于 AST 节点 `ValueNode` 内部使用的 `ValueType` 是 `std::variant<int, float, std::string, bool>`，正常情况下不应出现其他类型；该错误主要用于防御性编程，以防类型系统扩展后未同步更新。  
- 错误信息固定为：`"Unsupported argument type"`。

**典型错误代码**  
（通常由内部类型扩展引起，不易在用户代码中直接触发）  

```cpp
// 如果将来 ValueNode 支持了 double，但此处未更新，调用时可能触发
```

**解决办法**  

- 检查函数调用或宏调用时传入的参数值类型，确保仅使用 `int`、`float`、`string`、`bool` 类型的数据。  
- 若确实需要传递其他类型，需扩展 `valuesToTypes()` 函数以及相应的回调签名，使其支持新类型。  
- 正常情况下，用户无需特别处理此错误，若出现通常表明解释器或编译器内部类型处理存在缺陷。

---

## 23. Function not registered（函数未注册）

**解释**  
尝试调用一个未在系统中注册的外部函数，包括函数名错误、命名空间错误或参数签名不匹配。

**说明**  

- `FunctionCall::callFunction()` 会先检查函数是否已注册（通过 `isRegistered`），若未找到则报告此错误。  
- 注册匹配依赖完整的签名，包括函数名、参数个数、各参数类型以及是否为组合调用。如果任何一项不匹配，都会被视为未注册。  
- 错误信息格式：`"Function not registered: <命名空间>::<函数名>"`。  
- 常见的未注册原因：函数名拼写错误、命名空间遗漏、参数数量或类型与实际注册的不一致，或目标函数尚未通过 `registerFunction` 注册。

**典型错误代码**  

```cpp
math::sqrt(4);         // 若 math::sqrt 未注册
unknown::foo("bar");   // 命名空间或函数名不存在
math::pow(2);          // 参数个数与注册的不符（若注册为2个参数）
```

**解决办法**  

- 检查调用的函数名和命名空间是否正确，确保与注册时使用的完全一致（包括大小写）。  
- 确认调用时传入的参数数量和类型与注册函数时的 `CallbackTypeArgs` 签名匹配。  
- 如果函数确实应该存在，检查是否在初始化阶段正确注册了该函数（例如插件或模块未加载）。  
- 查看错误信息中的完整函数名，搜索对应注册代码，对比签名。

> [!TIP]
> 找 bug？我懂~（￣︶￣）↗

---

## 24. Macro not registered（宏未注册）

**解释**  
尝试调用一个未注册的宏，原因与函数未注册类似，包括名称错误或签名不匹配。

**说明**  

- `MacroCall::callMacro()` 执行前同样检查宏是否注册，若查找失败则报告此错误。  
- 宏没有命名空间，只通过宏名称和参数签名识别，因此错误信息仅包含宏名：`"Macro not registered: <宏名>"`。  
- 可能的原因：宏名拼写错误、参数个数或类型不匹配、宏尚未被 `registerMacro` 注册、或注册时标记的 `isCombination` 属性与调用期望不同。

**典型错误代码**  

```cpp
{myMacro("hello")};   // 若 myMacro 未注册
{sum(1, 2, 3)};       // 参数个数与注册不符
```

**解决办法**  

- 检查宏名称是否与注册时一致，注意大小写和特殊字符。  
- 核对调用宏时提供的参数个数和类型，使其与注册签名完全匹配。  
- 确认宏已在适当位置被注册（通常在程序初始化或插件加载时）。  
- 若宏支持组合调用（`isCombination = true`），需确保注册时使用了正确的重载版本。

---

> [!NOTE]
> 以下错误随 1.15.0 的类、继承、类型系统与 IR 层新增。

## 25. Unclosed block comment（未闭合的块注释）

**解释**  
块注释 `/* ... */` 缺少闭合标记。

**说明**  

- Lexer 在遇到 `/*` 后开始读取注释内容，直到遇见 `*/` 或输入结束。  
- 如果直到输入结束仍未找到 `*/`，则触发该错误。  
- 错误信息示例：`Unclosed block comment`。

**典型错误代码**  

```cpp
/* comment        // 缺少闭合的 */
```

**解决办法**  

- 检查注释是否成对闭合。

---

## 26. using 声明错误（using 声明语法错误）

**解释**  
`using` 类型别名声明缺少别名、`=` 或类型名。

**说明**  

- `using Alias = Type;` 必须依次包含别名、`=` 与类型名。  
- 错误信息示例：  
  - `Expected alias name after 'using'`  
  - `Expected '=' in using declaration, got X`  
  - `Expected type name`

**典型错误代码**  

```cpp
using = int;        // 缺少别名
using A int;        // 缺少 '='
using A = ;         // 缺少类型名
```

**解决办法**  

- 按照 `using Alias = Type;` 格式书写，每条声明以 `;` 结尾。

---

## 27. 类型名语法错误（类型名或类型参数缺少内容）

**解释**  
在期望类型名的位置缺少类型名，或类型参数缺少右尖括号。

**说明**  

- `parseTypeExpr()` 要求当前位置是标识符，并在解析 `<...>` 类型参数后必须遇到 `>`。  
- 错误信息示例：  
  - `Expected type name`  
  - `Expected '>' to close type 'X'`

**典型错误代码**  

```cpp
x: = 1;              // ':' 后缺少类型名
a: variant<int       // 缺少右尖括号
```

**解决办法**  

- 检查变量、参数与返回值声明中的类型名是否完整。  
- 类型参数如 `variant<int, string>` 必须使用 `>` 闭合。

---

## 28. 类名相关语法错误（class / extends / new / instanceof 后缺少类名）  

**解释**  
`class`、`extends`、`new` 或 `instanceof` 后缺少类名。

**说明**  

- Parser 在遇到这些关键字后会立即检查下一个 token 是否为标识符，否则报错。  
- 错误信息示例：  
  - `Expected class name`  
  - `Expected base class name after 'extends'`  
  - `Expected class name after 'new'`  
  - `Expected class name after 'instanceof'`

**典型错误代码**  

```cpp
class { }                 // class 后缺少类名
class A extends { }       // extends 后缺少基类名
x = new ();               // new 后缺少类名
if (x instanceof ) [...]  // instanceof 后缺少类名
```

**解决办法**  

- 在这些关键字后补全类名。

---

## 29. 构造器声明错误（构造器重复、static 或名称不符）

**解释**  
类中的构造器声明不合法。

**说明**  

- 一个类只能有一个构造器：`Duplicate constructor in class 'X'`。  
- 构造器不能声明为 `static`：`Constructor cannot be static`。  
- 构造器名称必须与类名一致：`Expected constructor name 'X'`。

**典型错误代码**  

```cpp
class A {
    A() {}
    A(x) {}        // 重复构造器
}
class B {
    static B() {}  // 构造器不能为 static
}
class C {
    D() {}         // 构造器名称与类名不一致
}
```

**解决办法**  

- 每个类只定义一个与类名相同的构造器，且不要使用 `static`。

---

## 30. 函数与方法声明错误（func 后缺少名称或参数列表错误）

**解释**  
`func` 后缺少函数/方法名，或参数列表格式错误。

**说明**  

- 命名函数与方法必须以 `func 名称(...)` 开头。  
- 参数列表中每个参数都需要名称，参数之间使用 `,` 分隔。  
- 错误信息示例：  
  - `Expected function name`  
  - `Expected method name`  
  - `Expected parameter name`  
  - `Expected ',' or ')' in parameter list, got X`

**典型错误代码**  

```cpp
func () { }        // 缺少函数名
func f(a, ) { }    // 参数列表末尾多出逗号
```

**解决办法**  

- 为函数/方法指定名称，并检查参数列表语法。

---

## 31. 成员访问语法错误（'.' 后缺少成员名）

**解释**  
`.` 后缺少成员名。

**说明**  

- `parsePostfix()` 在遇到 `.` 后会要求下一个 token 是标识符。  
- 错误信息示例：`Expected member name after '.'`。

**典型错误代码**  

```cpp
obj.        // '.' 后缺少成员名
obj.();     // 成员名缺失
```

**解决办法**  

- 在 `.` 后补全成员名。

---

## 32. 顶层声明限制（类与函数只能在顶层定义）

**解释**  
在函数、方法或块内嵌套定义了类或命名函数。

**说明**  

- 类定义与命名函数定义只允许出现在脚本顶层。  
- 错误信息：`Class and function definitions are only allowed at top level`。  
- 匿名函数不受此限制。

**典型错误代码**  

```cpp
func f() {
    func g() {}   // 嵌套命名函数
}
```

**解决办法**  

- 将类或命名函数移动到顶层；需要局部逻辑时使用匿名函数。

---

## 33. 重复定义类（Duplicate class）

**解释**  
同名类被重复定义。

**说明**  

- 语义分析阶段会维护类名表，遇到重复类名时报错。  
- 错误信息示例：`Duplicate class: X`。

**典型错误代码**  

```cpp
class A {}
class A {}    // 重复定义
```

**解决办法**  

- 确保类名唯一。

---

## 34. 重复定义成员变量（Duplicate member variable）

**解释**  
类中同名成员变量被重复定义。

**说明**  

- 错误信息示例：`Duplicate member variable: X`。

**典型错误代码**  

```cpp
class A {
public:
    x = 1;
    x = 2;    // 重复成员
}
```

**解决办法**  

- 为成员变量使用不同的名称。

---

## 35. 未知类或未知基类（Unknown class / Unknown base class）

**解释**  
`new`、成员访问或继承时引用了不存在的类。

**说明**  

- 错误信息示例：  
  - `Unknown class: X`  
  - `Unknown base class: X`

**典型错误代码**  

```cpp
x = new Missing();          // 类不存在
class B extends Missing {}  // 基类不存在
```

**解决办法**  

- 检查类名拼写与定义顺序。

---

## 36. 循环继承（Circular inheritance）

**解释**  
类的继承关系形成环。

**说明**  

- 例如 A extends B 且 B extends A，或 A extends A。  
- 错误信息示例：`Circular inheritance involving class 'X'`。

**典型错误代码**  

```cpp
class A extends B {}
class B extends A {}   // 循环继承
```

**解决办法**  

- 调整继承结构，避免循环。

---

## 37. 构造器与 super 调用错误（必须调用 super(...)）

**解释**  
子类构造器未正确调用基类构造器，或对无基类的类使用 `super`。

**说明**  

- 基类构造器需要参数时，子类构造器必须显式调用 `super(...)`：`Constructor of class 'X' must call super(...)`。  
- 子类没有构造器时无法向带参基类构造器传参：`Class 'X' must define a constructor to call base constructor with arguments`。  
- 对没有基类的类使用 `super`：`Class 'X' has no base class`。

**典型错误代码**  

```cpp
class Base {
    Base(x: int) {}
}
class Child extends Base {
    Child() {}    // 未调用 super(...)
}
class A {
    A() { super(); }   // A 没有基类
}
```

**解决办法**  

- 在子类构造器中按需调用 `super(...)`，且不要对无基类的类使用 `super`。

---

## 38. 私有成员访问（Cannot access private member）

**解释**  
在类外部访问了 `private` 成员变量或方法。

**说明**  

- 私有成员只能由类内部的方法访问。  
- 错误信息示例：`Cannot access private member 'X'`。

**典型错误代码**  

```cpp
class A {
private:
    secret = 42;
}
a = new A();
a.secret;    // 私有成员访问
```

**解决办法**  

- 通过类提供的 `public` 方法访问，或将该成员改为 `public`。

---

## 39. 成员不存在（Class has no member）

**解释**  
访问了类中不存在的成员或静态成员。

**说明**  

- 错误信息示例：  
  - `Class 'X' has no member 'Y'`  
  - `Class 'X' has no static member 'Y'`

**典型错误代码**  

```cpp
class A { public: x = 1; }
a = new A();
a.y;    // 不存在成员 y
```

**解决办法**  

- 检查成员名拼写，或先为该类添加对应成员。

---

## 40. 方法/函数/构造器签名不匹配（No matching ...）

**解释**  
调用方法、函数或构造器时，名称正确但参数数量或类型与所有重载都不匹配。

**说明**  

- 错误信息示例：  
  - `No matching method 'X' ...`  
  - `No matching function 'X' ...`  
  - `No matching constructor for native class 'X' ...`  
- 构造器相关还包括：  
  - `Class 'X' has no constructor`  
  - `Constructor of class 'X' expects N argument(s)`  
  - `Type mismatch for constructor parameter 'X' ...`

**典型错误代码**  

```cpp
func f(x: int) -> int { return x; }
f("string");        // 参数类型不匹配

class A { A(x: int) {} }
a = new A("s");     // 构造器参数类型不匹配
```

**解决办法**  

- 检查调用参数的数量与类型，与声明或注册的签名保持一致。

---

## 41. 方法调用目标错误（Method call target is not an object）

**解释**  
对非对象值调用方法，或对非对象值访问成员。

**说明**  

- 错误信息示例：  
  - `Method call target is not an object`  
  - `Cannot access member of a non-object value`

**典型错误代码**  

```cpp
a = 5;
a.x;        // 对非对象访问成员
a.f();      // 对非对象调用方法
```

**解决办法**  

- 确保调用目标为对象（如 `new` 创建的对象）。

---

## 42. 数组索引错误（Cannot index a non-array value）

**解释**  
对非数组值进行索引，或对非数组值进行索引赋值。

**说明**  

- 错误信息示例：  
  - `Cannot index a non-array value`  
  - `Cannot assign to an index of a non-array value`

**典型错误代码**  

```cpp
a = 5;
a[0];       // 对非数组索引
```

**解决办法**  

- 仅对数组类型的值使用 `[]`。

---

## 43. 类型别名错误（using 别名冲突或循环）

**解释**  
`using` 类型别名与类名冲突、重复定义、非法命名或循环引用。

**说明**  

- 错误信息示例：  
  - `Type alias conflicts with class name: X`  
  - `Duplicate type alias: X`  
  - `Cannot use 'X' as a type alias name`  
  - `Circular type alias involving 'X'`

**典型错误代码**  

```cpp
using A = A;            // 别名循环
using A = int;
using A = string;       // 重复别名
```

**解决办法**  

- 确保别名唯一、不与类名冲突且不循环引用自身。

> [!TIP]
> 我喜欢蓝色，你呢？φ(*￣0￣)

---

## 44. 类型不存在或类型参数非法（Unknown type）

**解释**  
使用了不存在的类型，或对不接受类型参数的类型传入了类型参数。

**说明**  

- 错误信息示例：  
  - `Unknown type: X`  
  - `Type 'X' does not accept type arguments`

**典型错误代码**  

```cpp
x: Missing = 1;          // 类型不存在
x: int<string> = 1;      // int 不接受类型参数
```

**解决办法**  

- 检查类型名拼写；仅为支持的类型提供类型参数。

---

## 45. 类型化声明缺少初始化（Typed declaration requires an initializer）

**解释**  
带类型的变量声明没有提供初始值。

**说明**  

- 错误信息示例：`Typed declaration of 'X' requires an initializer`。

**典型错误代码**  

```cpp
x: int;    // 缺少初始值
```

**解决办法**  

- 为类型化声明补充初始值，如 `x: int = 0;`。

---

## 46. 成员缺少默认值（Member has no default value）

**解释**  
类成员没有默认值，且构造器未对其赋值。

**说明**  

- 错误信息示例：`Member 'X' has no default value ...`。

**典型错误代码**  

```cpp
class A {
public:
    x;          // 无默认值
    A() {}      // 构造器也未赋值
}
```

**解决办法**  

- 为成员提供默认值，或在构造器中赋值。

---

## 47. 缺少 return 语句（Missing return statement）

**解释**  
声明了返回类型的函数或匿名函数缺少 `return`。

**说明**  

- 错误信息示例：  
  - `Missing return statement in function 'X'`  
  - `Missing return statement in anonymous function`

**典型错误代码**  

```cpp
func f() -> int {
    x = 1;    // 没有 return
}
```

**解决办法**  

- 在函数所有路径上返回对应类型的值。

---

## 48. 类型不匹配（Type mismatch）

**解释**  
参数、返回值或赋值时的类型与期望类型不一致。

**说明**  

- 错误信息格式：`Type mismatch for <位置>: expected <期望类型>, got <实际类型>`（具体文案以实际为准）。

**典型错误代码**  

```cpp
func f(x: int) -> int { return x; }
f("string");    // 参数类型不匹配
```

**解决办法**  

- 将实际值转换为期望类型，或调整声明。

---

## 49. 成员默认值必须为常量（Member default value must be a constant literal）

**解释**  
类成员默认值必须是常量字面量。

**说明**  

- 错误信息示例：`Member default value of 'X' must be a constant literal`。  
- 成员默认值不支持表达式或变量。

**典型错误代码**  

```cpp
class A {
public:
    x = 1 + 2;    // 非字面量
}
```

**解决办法**  

- 将默认值改为字面量，或在构造器中赋值。

---

## 50. 取模除零（Modulo by zero）

**解释**  
取模运算的除数为 0。

**说明**  

- 错误信息：`Modulo by zero`。

**典型错误代码**  

```cpp
5 % 0;
```

**解决办法**  

- 确保取模运算的除数不为 0。

---

## 51. 空 optional 值运算（empty optional）

**解释**  
对空的 `optional` 值进行算术或比较运算。

**说明**  

- 错误信息示例：  
  - `Cannot perform arithmetic on an empty optional value`  
  - `Cannot compare an empty optional value`

**典型错误代码**  

```cpp
x: optional<int> = none;
y = x + 1;
```

**解决办法**  

- 运算前检查 `optional` 是否包含值。

---

## 52. 对非对象读写字段（Cannot load/store field）

**解释**  
对非对象值读取或写入字段。

**说明**  

- 错误信息示例：  
  - `Cannot load field 'X' from a non-object value`  
  - `Cannot store field 'X' on a non-object value`

**典型错误代码**  

```cpp
a = 5;
a.x = 1;    // 对非对象写字段
```

**解决办法**  

- 仅对对象读写字段。

---

## 53. 参数数量不匹配（expects N argument(s)）

**解释**  
方法或函数调用时参数数量与声明不一致。

**说明**  

- 错误信息示例：  
  - `Method 'X' expects N argument(s), got M`  
  - `Function expects N argument(s), got M`

**典型错误代码**  

```cpp
func f(x: int) -> int { return x; }
f();          // 缺少参数
```

**解决办法**  

- 按声明提供正确的参数数量。

---

## 54. 函数引用错误（null function reference）

**解释**  
调用了一个空函数引用，或函数引用缺少所属字节码。

**说明**  

- 错误信息示例：  
  - `Cannot call a null function reference`  
  - `Function reference has no owning bytecode chunk`

**典型错误代码**  

（通常由内部逻辑错误引起，不易在用户代码中直接触发）

**解决办法**  

- 检查函数引用是否被正确赋值；若为内部错误请反馈插件开发者。

---

## 55. 字节码错误（null bytecode / invalid instruction pointer）

**解释**  
运行时执行了空的字节码块，或指令指针、方法/函数体索引非法。

**说明**  

- 错误信息示例：  
  - `Cannot run a null bytecode chunk`  
  - `Cannot execute a null bytecode chunk`  
  - `Invalid instruction pointer`  
  - `Invalid method ordinal`  
  - `Invalid function body index`  
  - `Invalid object class index`

**典型错误代码**  

（通常由编译产物异常或内部错误引起）

**解决办法**  

- 检查脚本是否通过正常流程编译执行；若为内部错误请反馈插件开发者。

---

## 56. 原生类错误（Failed to create native class）

**解释**  
创建原生类、加载原生静态字段失败，或方法调用目标与期望类不符。

**说明**  

- 错误信息示例：  
  - `Failed to create native class 'X': ...`  
  - `Failed to load native static field 'X': ...`  
  - `Method 'X' does not belong to this object`  
  - `Method call target is not an instance of the expected class`

**典型错误代码**  

```cpp
new ObservableString("x", true);   // 若构造参数错误
```

**解决办法**  

- 检查原生类的构造参数与字段名；若为内部错误请反馈插件开发者。

---

## 57. 原生回调抛出错误（callback threw）

**解释**  
调用原生函数、方法、构造器或宏时，其内部抛出了错误。

**说明**  

- 错误信息格式：  
  - `Function callback threw: <错误内容>`  
  - `Method callback threw: <错误内容>`  
  - `Constructor callback threw: <错误内容>`  
  - `Macro callback threw: <错误内容>`  
- 具体内容取决于原生实现。

**典型错误代码**  

```cpp
math::sqrt(-1);     // 若原生实现检查参数并报错
```

**解决办法**

- 根据错误内容检查参数与调用方式。

---

## 58. 组件错误（component）

**解释**  
组件定义或使用违反了组件系统的约束。

**说明**  

- 错误信息格式：  
  - `Component definitions are only allowed at top level`（组件定义嵌套在函数体或块内）  
  - `Duplicate component '<名称>'`（不同位置定义了同名组件）  
  - `Component '<名称>' expects N argument(s), got M`（调用参数个数与组件参数不一致）  
  - `Recursive component '<名称>'`（组件直接或间接调用自身）  
  - `Component '<名称>' can only be used as a statement inside a declarative UI block`（在声明式块外调用，或把组件调用当作表达式使用）

**典型错误代码**

```lcui
component Bar(a, b) {
    divider(new DividerOptions());
}

func make() -> void {
    component Inner(x) {          // 错误：组件只能定义在顶层
        divider(new DividerOptions());
    }
}

Bar("only-one");                  // 错误：参数个数不匹配，且不在声明式块内
```

**解决办法**

- 把组件定义移动到脚本顶层。
- 调用时参数个数与定义保持一致，并确保组件调用是声明式 UI 块内的语句。
- 检查组件体内部调用的其他组件是否形成了环。

> [!TIP]
> 看到这了，听首音乐休息下吧(～￣▽￣)～
