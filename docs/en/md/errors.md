<!-- markdownlint-disable MD033 -->

# Common Error Meanings

## 1. Unclosed string (unclosed string)

**Explanation**  
The string is missing a matching closing quote, i.e. `"..."` or `'...'` does not appear as a pair.

**Details**  

- When parsing a string starting with `"` or `'`, the Lexer keeps reading characters until it encounters the matching closing quote or the end of input.  
- If no closing quote is found by the end of input (`currentChar == 0`), this error is triggered.  
- Example error message: `Unclosed string`

**Typical erroneous code**  

```cpp
"hello       // missing closing double quote
'world       // missing closing single quote
"mixed'      // mismatched quotes, also treated as unclosed
```

**Solution**  

- Check whether the string is wrapped in a matching pair of quotes of the same type.  
- Make sure quotes of the same type inside the string content are properly escaped (e.g. `"He said \"Hi\""`), so as to avoid premature closing.

---

## 2. Invalid numeric literal (invalid numeric literal)

**Explanation**  
When the Lexer tries to recognize a numeric value, the resulting character sequence cannot form a valid integer or floating-point number.

**Details**  

- The Lexer enters number parsing when it encounters a digit `0-9` or a decimal point `.`.  
- It reads consecutive digits and at most one decimal point until it encounters other characters.  
- If the resulting string is an empty string (for example, entering number parsing after two consecutive non-digit characters, though this does not actually happen) or a lone dot `"."`, it is considered an invalid numeric literal.  
- Example error message: `Invalid numeric literal: '.'`

**Typical erroneous code**  

```cpp
.           // a lone decimal point with no digits on either side
.5          // in this codebase, a leading '.' enters parseNumber, reads '.5', finally num = ".5", hasDot=true, returns TOKEN_FLOAT, no error is reported
```

In fact, in the current implementation, **only the case where `.` is not followed by a digit reports an error** (such as a lone `.` followed by a space or other characters; after reading, num is `"."`).  
For example:  

```cpp
a = . ;      // a dot directly followed by a space or semicolon, num="."
```

Whereas with `.5`, the `5` is read, so `num=".5"` is valid and a float token is returned.

**Solution**  

- Make sure the numeric literal contains at least one digit, and that a floating-point number has a digit on at least one side of the decimal point.  
- If a pure decimal is needed, prepend `0` before the decimal point, e.g. `.5` → `0.5`.  
- Avoid lone decimal points appearing in expressions.

## 3. Syntax error (syntax error: expected token mismatch)

**Explanation**  
At a position where a specific syntactic element is expected, a token of a different type is actually encountered.

**Details**  

- The Parser checks whether the current token type equals the expected value through the `eat()` function, and throws this error if they do not match.  
- Almost all syntactic structures rely on this check, for example expecting `(` but encountering `{`, or expecting `;` but encountering an identifier.  
- Error message format:  
  `Syntax error: Expected <expected token>, got <actual token>`

**Typical erroneous code**  

```cpp
if x > 0      // missing left parenthesis, expected '(', actually got IDENT 'x'
{ a = 1 }     // the if body should use '[' instead of '{'
```

**Solution**  

- Carefully check the syntax rules to ensure correct symbols are used at keywords, identifiers, operators, parentheses, etc.  
- Use the "expected/actual" comparison in the error message to locate and fix the erroneous token.  
- In most cases, this kind of error is caused by a chain of preceding mismatched parentheses, misspelled keywords, or missing symbols; after fixing the first error, the rest may disappear automatically.

---

## 4. Missing `;` or EOF after statement (missing semicolon or end-of-file marker after a statement)

**Explanation**  
After a statement ends, a semicolon `;` or end of file (EOF) is expected, but another token is actually encountered.

**Details**  

- In the main `parse()` loop, after each statement is parsed, it checks whether the next token is `;` or `EOF`.  
- If not, this error is reported, and the error message includes the type and content of the token actually encountered.  
- This commonly occurs when a semicolon is omitted, or an unexpected expression is written after the statement.

**Typical erroneous code**  

```cpp
a = 5
b = 6;   // missing semicolon after a = 5, so the next line's b is treated as a continuation of the same statement
```

**Solution**  

- Make sure every statement ends correctly with a semicolon.  
- Check whether other symbols were mistakenly written at the end of the statement (such as redundant operators or parentheses), and delete or restructure the statement if necessary.

---

<h2 id="too_many_arguments_in_function_call">5. Too many args in function call (too many arguments in a function call)</h2>

**Explanation**  
The number of arguments to a function or macro exceeds the limit (100 here).

**Details**  

- `parseArgs()` counts while collecting arguments and triggers this error when `tpl->parts.size() >= 100`; the error message is `"Too many args in function call"`.  
- After it is triggered, parsing stops, `nullptr` is returned, and an error node is produced.  

**Typical erroneous code**  

```cpp
func(arg1, arg2, ..., arg101);   // more than 100 arguments
```

**Solution**  

- Reduce the number of arguments in the function call.  

---

## 6. Invalid integer literal (invalid integer literal)

**Explanation**  
The integer literal is semantically invalid, usually because the value exceeds the representable range of the `int` type.

**Details**  

- Lexical analysis has recognized a valid integer token (containing only digits), but in the Parser's `parseValue()`, the attempt to convert it to `int` using `std::from_chars` fails.  
- The failure may be caused by numeric overflow (e.g. exceeding the `int` maximum `2147483647`), or by unexpected non-digit characters (since the Lexer guarantees the format, overflow is the main cause).  
- Example error message: `Invalid integer literal: 9999999999`

**Typical erroneous code**  

```cpp
a = 3000000000;   // exceeds the 32-bit signed integer range
```

**Solution**  

- Check the size of the integer literal to ensure it is within the valid range of the target type.  

---

## 7. Invalid float literal (invalid floating-point literal)

**Explanation**  
The floating-point literal cannot be correctly converted to a `float` value, usually due to overflow or a format error.

**Details**  

- Similar to integers, after lexical analysis obtains TOKEN_FLOAT, the Parser converts it to `float` using `std::from_chars`.  
- Possible failure cases: the value exceeds the `float` range (approximately ±3.4e38), or extreme values are produced by scientific notation, etc., causing the conversion to return `std::errc::result_out_of_range`; in rare cases, issues such as memory alignment may lead to an unexpected format.  
- Example error message: `Invalid float literal: 1e999`

**Typical erroneous code**  

```cpp
f = 1e999;   // exceeds the float representable range
```

**Solution**  

- Keep the magnitude of the floating-point literal within the `float` range (approximately 1.175e-38 to 3.402e+38).  

---

## 8. Unexpected value type (unexpected value type)

**Explanation**  
When the Parser expects a value (literal), it encounters an unrecognizable token type.

**Details**  

- Triggered in the `default` branch of `parseValue()`; when the token type is not `INT`, `FLOAT`, `STRING`, or `BOOL_LIT`, it means the current token cannot serve as a valid value.  
- This is usually caused by tokens being out of place due to a syntax error, for example when an operator or separator appears where an expression should begin.  
- Example error message: `Unexpected value type: +`

**Typical erroneous code**  

```cpp
x = + ;      // missing operand after the plus sign, + is parsed as a value
```

**Solution**  

- Check whether the expression is complete: whether both sides of the operator have valid operands.  
- Look at the tokens before the error position to confirm whether an identifier, literal, or subexpression is missing.

## 9. Unknown compare op (unknown comparison operator)

**Explanation**  
When processing a comparison expression, the compiler encounters an unrecognized comparison operator.

**Details**  

- In `visit(CompareNode& node)`, the corresponding bytecode instruction (such as `CMP_EQ`, `CMP_GT`, etc.) is selected based on the `node.op` string.  
- If `node.op` is not in the expected set `{==, !=, >, <, >=, <=}`, this error is triggered.  
- In the normal parsing flow, comparison operators are already restricted by the Parser to the six above, so this error usually means the AST was built abnormally or an incorrect operator string was passed internally.  
- Error message format: `Unknown compare op: <operator>`, with the position information placeholder as `{0, 0, 0}`.

**Typical erroneous code**  

```cpp
// if the AST is manually constructed and an illegal comparison operator is used
a <=> b    // the language does not support three-way comparison, but "<=>" appears in the AST
```

**Solution**  

- Check the AST generation process to ensure the comparison operator string is correct.  
- If caused by user input, confirm whether the comparison operator used in the code is supported by the language (e.g. `==`, `!=`, `>`, `<`, `>=`, `<=`).  
- If the problem comes from custom AST construction, fix the operator string and use legal comparison operators.

---

## 10. Unknown arithmetic op (unknown arithmetic operator)

**Explanation**  
When processing an arithmetic expression, the compiler encounters an unrecognized arithmetic operator.

**Details**  

- In `visit(ArithmeticNode& node)`, the corresponding bytecode instruction (such as `ADD`, `SUB`, `MUL`, `DIV`, `MOD`, `POW`) is selected based on the `node.op` string.  
- If `node.op` is not within `{+, -, *, /, %, ^}`, this error is triggered.  
- Similar to comparison operators, the Parser normally guarantees that arithmetic operators come from these six, so this error usually indicates an internal data error.  
- Example error message: `Unknown arithmetic op: ~`

**Typical erroneous code**  

```cpp
// for example, incorrectly using an unsupported arithmetic operator
a ~ b    // if the AST incorrectly records "~" as an arithmetic operator
```

**Solution**  

- Confirm that the arithmetic operator used in the expression is one of the six supported by the language (`+`, `-`, `*`, `/`, `%`, `^`).  
- If the language is being extended, make sure the compiler side has added handling branches for the new operators.  
- Fix operator string errors that may occur during AST construction.

---

## 11. Unknown unary op (unknown unary operator)

**Explanation**  
When processing a unary expression, the compiler encounters an unrecognized unary operator.

**Details**  

- In `visit(UnaryNode& node)`, the operation is selected based on the `node.op` string: `-` corresponds to `NEG`, `!` corresponds to `NOT`, and `+` does nothing (ignored directly).  
- If `node.op` is not in `{-, !, +}`, this error is reported.  
- This error is also a defensive check, indicating an abnormal unary operator string.  
- Example error message: `Unknown unary op: #`

**Typical erroneous code**  

```cpp
// incorrectly using a unary operator
~a    // bitwise NOT is not supported, but the AST records "~"
```

**Solution**  

- Check whether the unary operator is correct; the supported unary operators include: `-` (negation), `!` (logical NOT), `+` (unary plus, no actual effect).  
- If new unary operators are introduced, synchronously modify the `visit(UnaryNode&)` method in the compiler to add corresponding handling.  
- Investigate whether the AST construction stage incorrectly generated an illegal unary operator string.

## 12. Stack underflow (stack underflow)

**Explanation**  
An attempt is made to pop a value from an empty operand stack, including ordinary pop operations and the case where the stack is empty when duplicating the stack top (DUP).

**Details**  

- The `pop()` method reports `"Stack underflow"` and returns a default value when the stack is empty.  
- When executing the `DUP` instruction with an empty stack, `"Stack underflow during DUP"` is reported separately.  
- Almost all instructions that require operands (arithmetic, comparison, logic, assignment, etc.) call `pop` first, so this error can be triggered in various operations.  
- The root cause is usually code logic that makes stack consumption and production mismatched, such as an expression missing an operand or a value being popped unexpectedly.

**Typical erroneous code**  

```cpp
+ 5;        // insufficient operand for unary plus; at runtime, attempting to pop the stack top finds the stack empty
x = ;       // the assignment right-hand value is missing, so the stack is empty during DUP
```

**Solution**  

- Check whether the expression is complete and whether every operator has enough operands.  
- For assignment statements, make sure the right-hand value exists and can be pushed onto the stack correctly.  
- Use debug output of the stack state to trace where the stack operations are unbalanced.

---

## 13. Undefined variable (undefined variable)

**Explanation**  
An attempt is made to read a variable that has never been assigned or declared.

**Details**  

- When executing the `LOAD_VAR` instruction, the variable name is looked up in the variable table `this->variables`; if it is not found, an error is reported.  
- The error message includes the variable name: `"Undefined variable: <name>"`.  
- A variable is recorded only after `STORE_VAR` is executed, so referencing it before assignment causes this error.

**Typical erroneous code**  

```cpp
y = x + 1;   // x is never assigned
```

**Solution**  

- Make sure all variables are assigned before use.  
- Check that variable names are spelled correctly, paying attention to case.  
- If the variable should indeed be defined externally, check the initialization order or scope.

---

## 14. Type mismatch in arithmetic (type mismatch in arithmetic operations)

**Explanation**  
An arithmetic operation is performed on types that do not support arithmetic (such as mixing strings with numbers where the operator is not `+`).

**Details**  

- In `applyArithmetic`, this is triggered when the left and right operands are not both arithmetic types (int/float/bool) and the operator is not `+`, which is used for string concatenation.  
- For example, performing `-`, `*`, `/`, etc. between a string and a number, or performing a non-`+` operation on two strings.  
- The error message is fixed as `"Type mismatch in arithmetic"`.

**Typical erroneous code**  

```cpp
"hello" * 3;   // strings cannot be multiplied
true - "a";    // bool is treated as an arithmetic type, but the right side is a string, so it still enters the type mismatch branch (non-arithmetic type)
```

**Solution**  

- Check the operand types to ensure that all operands of arithmetic operations are numeric (int/float/bool).  
- Strings can only be concatenated with `+`; other operations require converting them to numbers first (if the language supports it) or adjusting the logic.  
- If you intend to operate on numbers, make sure the variable stores a numeric type rather than a string.

---

## 15. Modulo requires integral types (modulo requires integral types)

**Explanation**  
The operands of the modulo operator `%` must be integral types (int or bool); floating-point numbers are not supported.

**Details**  

- When `applyArithmetic` handles `%`, it checks whether both the left and right operands are integral types per `std::is_integral_v` (including bool).  
- If one of them is a `float`, this error is reported.  
- Error message: `"Modulo requires integral types"`.

**Typical erroneous code**  

```cpp
5.0 % 2;     // floating-point modulo
3.14 % 1.5;  
```

**Solution**  

- Or use the library function `fmod` to implement floating-point modulo (which requires your own support).  
- Check variable types to avoid unintentionally writing integers in floating-point form (e.g. `5.0`).

---

## 16. Unknown arithmetic op (unknown arithmetic operator)

**Explanation**  
When executing an arithmetic operation, the virtual machine encounters an unrecognized operator; this is a defensive error.

**Details**  

- Triggered when both left and right operands are numeric types but the operator string is not any of `+`, `-`, `*`, `/`, `%`, `^`.  
- Normally, the compiler (Compiler) and the AST do not generate illegal arithmetic operators, so this error may indicate corrupted bytecode or an incompletely implemented language extension.  
- Example error message: `"Unknown arithmetic op: ~"`.

**Typical erroneous code**  
(Directly constructing an erroneous AST, uncommon)  

```cpp
// some illegal means causes an arithmetic operator "&" to appear in the bytecode
```

**Solution**  

- Check the bytecode generation process to ensure the arithmetic operator string comes from the correct set.  
- If it is a language extension, synchronously update the `applyArithmetic` branch in the VM.

---

## 17. Unknown unary op (unknown unary operator)

**Explanation**  
When executing a unary operation, the virtual machine encounters an unimplemented operator, or unary `+`/`-` is used on an unsupported type.

**Details**  

- In `applyUnary`, the legal unary operators are `+`, `-` (only for numeric types), and `!` (for all types).  
- If `+` or `-` is used on a non-numeric type (such as string), this error is triggered because `is_arithmetic` is not satisfied and the `!` branch is not entered.  
- Other illegal unary operators (such as `~`) also trigger it.  
- Example error message: `"Unknown unary op: -"` (when applied to a string).

**Typical erroneous code**  

```cpp
-"hello";    // negating a string
+true;       // legal, bool is an arithmetic type
~5;          // if the language does not support bitwise NOT and the AST produces "~", this error is reported
```

**Solution**  

- Unary `+`/`-` can only be used on numbers; check whether the operand type is correct.  
- Use `!` for logical NOT on booleans or strings.  
- If other unary operators need to be supported, extend the VM's `applyUnary` method.

---

## 18. Unknown comparison op (unknown comparison operator)

**Explanation**  
When the virtual machine performs a comparison, the operator is not among `==`, `!=`, `>`, `<`, `>=`, `<=`.

**Details**  

- `applyComparison` performs a spaceship operator `<=>` comparison for legal comparison operators; if the operator string does not match, an error is reported.  
- This is also a defensive error; a normal AST does not produce illegal comparison operators.  
- Example error message: `"Unknown comparison op: <=>"`.

**Typical erroneous code**  
(Caused by erroneous bytecode)  

```cpp
// the comparison operator in the bytecode is "==="
```

**Solution**  

- Confirm that comparison operators are limited to the six above.  
- Trace the bytecode generation stage and fix the incorrect operator string.

---

## 19. Type mismatch in comparison (type mismatch in comparison operations)

**Explanation**  
The two operands are neither both arithmetic types nor exactly the same type, so they cannot be compared.

**Details**  

- Comparison requires: either both are numeric types (int/float/bool), which can be uniformly converted to double for comparison; or both are exactly the same data type (e.g. two strings), compared using that type's `<=>`.  
- For example, `5 == "5"` triggers this error because one is an int and the other is a string, which does not satisfy the requirement.  
- Error message: `"Type mismatch in comparison"`.

**Typical erroneous code**  

```cpp
5 == "5";
true > "false";
```

**Solution**  

- Convert the operands to the same type before comparing (e.g. convert the string to a number).  
- Check variable types to ensure both sides of the comparison are type-compatible.  
- If you want to compare a string with a number, explicitly call a conversion function (if the language provides one).

---
## 20. Instruction limit exceeded

**Explanation**  
The virtual machine executed more than 1,000,000 instructions without terminating; it may be stuck in an infinite loop.

**Details**  

- The `run()` method has an execution counter `executed` that increments with each executed instruction; when the limit is reached, it reports an error and terminates.  
- This is a safety protection mechanism that prevents infinite loops from exhausting resources.  
- Error message: `"Instruction limit exceeded (possible infinite loop)"`.

---

## 21. Function/Macro call error

**Explanation**  
When calling an externally registered function or macro, an error was reported inside the function/macro.

**Details**  

- When executing the `CALL` or `CALL_MACRO` instruction, `FunctionCall::callFunction` or `MacroCall::callMacro` is invoked; if an `ll::Unexpected` error is returned, its `message()` is used directly as the error message.  
- The error content depends entirely on the implementation of the external function/macro, such as incorrect argument count, mismatched types, feature limitations, etc.  
- The error message format is `"Function callback threw: <error content>"`, `"Macro callback threw: <error content>"`, etc.; the specific content depends on the implementation of the external function/macro.

**Typical Error Code**  

```cpp
math::sqrt(-1);     // if the sqrt function checks its argument internally and reports an error
some::func(1, 2);   // if this function requires at least 3 arguments
```

**Solution**  

- Check the specific content of the error message, and verify the function name, argument count, and types against the documentation of the external module.  
- Make sure the namespace and function name used are correctly registered.  
- If the error is caused by internal logic (such as division by zero), check the input data.

## 22. Unsupported argument type

**Explanation**  
When calling a function or macro, the type of an argument passed in is not in the list of types supported by the system.

**Details**  

- The `valuesToTypes()` function iterates over all passed argument values (`CallbackTypeValues`) and checks the actual type of each argument via `std::visit`.  
- Only four types are supported: `int`, `float`, `std::string`, and `bool`. If an argument is not one of these four types, this error is triggered.  
- Since the `ValueType` used internally by the AST node `ValueNode` is `std::variant<int, float, std::string, bool>`, other types should not appear under normal circumstances; this error is mainly for defensive programming, in case the type system is extended without being updated in sync.  
- The error message is fixed as: `"Unsupported argument type"`.

**Typical Error Code**  
(Usually caused by internal type extension; not easy to trigger directly in user code)  

```cpp
// If ValueNode supports double in the future but this is not updated, it may be triggered when called
```

**Solution**  

- Check the types of argument values passed in function or macro calls, and make sure only `int`, `float`, `string`, and `bool` type data are used.  
- If other types really need to be passed, extend the `valuesToTypes()` function and the corresponding callback signatures to support the new types.  
- Under normal circumstances, users do not need to handle this error specially; if it occurs, it usually indicates a defect in the internal type handling of the interpreter or compiler.

---

## 23. Function not registered

**Explanation**  
An attempt was made to call an external function that is not registered in the system, including an incorrect function name, an incorrect namespace, or a mismatched parameter signature.

**Details**  

- `FunctionCall::callFunction()` first checks whether the function is registered (via `isRegistered`); if it is not found, this error is reported.  
- Registration matching depends on the complete signature, including the function name, argument count, argument types, and whether it is a combination call. If any item does not match, it is treated as unregistered.  
- Error message format: `"Function not registered: <namespace>::<function name>"`.  
- Common reasons for not being registered: misspelled function name, omitted namespace, argument count or types inconsistent with the actual registration, or the target function has not been registered via `registerFunction`.

**Typical Error Code**  

```cpp
math::sqrt(4);         // if math::sqrt is not registered
unknown::foo("bar");   // namespace or function name does not exist
math::pow(2);          // argument count does not match the registration (if registered with 2 arguments)
```

**Solution**  

- Check whether the called function name and namespace are correct, and make sure they are exactly the same as those used at registration time (including case).  
- Confirm that the number and types of arguments passed in the call match the `CallbackTypeArgs` signature used when registering the function.  
- If the function should indeed exist, check whether it was correctly registered during initialization (for example, the plugin or module was not loaded).  
- Look at the full function name in the error message, search for the corresponding registration code, and compare the signatures.

> [!TIP]
> Looking for a bug? I know~（￣︶￣）↗

---

## 24. Macro not registered

**Explanation**  
An attempt was made to call an unregistered macro; the reasons are similar to those for an unregistered function, including an incorrect name or a mismatched signature.

**Details**  

- `MacroCall::callMacro()` also checks whether the macro is registered before execution; if the lookup fails, this error is reported.  
- Macros have no namespace and are identified only by the macro name and parameter signature, so the error message contains only the macro name: `"Macro not registered: <macro name>"`.  
- Possible reasons: misspelled macro name, mismatched argument count or types, the macro has not been registered via `registerMacro`, or the `isCombination` attribute marked at registration differs from what the call expects.

**Typical Error Code**  

```cpp
{myMacro("hello")};   // if myMacro is not registered
{sum(1, 2, 3)};       // argument count does not match the registration
```

**Solution**  

- Check whether the macro name is the same as at registration time, paying attention to case and special characters.  
- Verify that the number and types of arguments provided in the macro call exactly match the registered signature.  
- Confirm that the macro is registered in the appropriate place (usually during program initialization or plugin loading).  
- If the macro supports combination calls (`isCombination = true`), make sure the correct overloaded version is used at registration time.

---

> [!NOTE]
> The following errors were added with the classes, inheritance, type system, and IR layer of 1.15.0.

## 25. Unclosed block comment

**Explanation**  
The block comment `/* ... */` is missing its closing marker.

**Details**  

- The Lexer starts reading the comment content after encountering `/*` until it meets `*/` or the end of the input.  
- If `*/` is still not found by the end of the input, this error is triggered.  
- Example error message: `Unclosed block comment`.

**Typical Error Code**  

```cpp
/* comment        // missing closing */
```

**Solution**  

- Check whether comments are closed in pairs.

---

## 26. using declaration error (using declaration syntax error)

**Explanation**  
The `using` type alias declaration is missing the alias, `=`, or the type name.

**Details**  

- `using Alias = Type;` must contain the alias, `=`, and the type name in order.  
- Example error messages:  
  - `Expected alias name after 'using'`  
  - `Expected '=' in using declaration, got X`  
  - `Expected type name`

**Typical Error Code**  

```cpp
using = int;        // missing alias
using A int;        // missing '='
using A = ;         // missing type name
```

**Solution**  

- Write in the `using Alias = Type;` format, and end each declaration with `;`.

---

## 27. Type name syntax error (type name or type parameter missing content)

**Explanation**  
A type name is missing where one is expected, or a type parameter is missing its closing angle bracket.

**Details**  

- `parseTypeExpr()` requires the current position to be an identifier and must encounter `>` after parsing the `<...>` type parameters.  
- Example error messages:  
  - `Expected type name`  
  - `Expected '>' to close type 'X'`

**Typical Error Code**  

```cpp
x: = 1;              // type name missing after ':'
a: variant<int       // missing closing angle bracket
```

**Solution**  

- Check whether the type names in variable, parameter, and return value declarations are complete.  
- Type parameters such as `variant<int, string>` must be closed with `>`.

---

## 28. Class name related syntax error (class name missing after class / extends / new / instanceof)  

**Explanation**  
A class name is missing after `class`, `extends`, `new`, or `instanceof`.

**Details**  

- The Parser immediately checks whether the next token is an identifier after encountering these keywords; otherwise it reports an error.  
- Example error messages:  
  - `Expected class name`  
  - `Expected base class name after 'extends'`  
  - `Expected class name after 'new'`  
  - `Expected class name after 'instanceof'`

**Typical Error Code**  

```cpp
class { }                 // class name missing after class
class A extends { }       // base class name missing after extends
x = new ();               // class name missing after new
if (x instanceof ) [...]  // class name missing after instanceof
```

**Solution**  

- Complete the class name after these keywords.

---

## 29. Constructor declaration error (duplicate constructor, static, or mismatched name)

**Explanation**  
The constructor declaration in the class is invalid.

**Details**  

- A class can have only one constructor: `Duplicate constructor in class 'X'`.  
- A constructor cannot be declared `static`: `Constructor cannot be static`.  
- The constructor name must match the class name: `Expected constructor name 'X'`.

**Typical Error Code**  

```cpp
class A {
    A() {}
    A(x) {}        // duplicate constructor
}
class B {
    static B() {}  // constructor cannot be static
}
class C {
    D() {}         // constructor name does not match the class name
}
```

**Solution**  

- Define only one constructor with the same name as the class for each class, and do not use `static`.

---

## 30. Function and method declaration error (name missing after func or parameter list error)

**Explanation**  
The function/method name is missing after `func`, or the parameter list format is wrong.

**Details**  

- Named functions and methods must start with `func name(...)`.  
- Each parameter in the parameter list needs a name, and parameters are separated by `,`.  
- Example error messages:  
  - `Expected function name`  
  - `Expected method name`  
  - `Expected parameter name`  
  - `Expected ',' or ')' in parameter list, got X`

**Typical Error Code**  

```cpp
func () { }        // missing function name
func f(a, ) { }    // trailing comma at the end of the parameter list
```

**Solution**  

- Specify a name for the function/method, and check the parameter list syntax.

---

## 31. Member access syntax error (member name missing after '.')

**Explanation**  
A member name is missing after `.`.

**Details**  

- `parsePostfix()` requires the next token to be an identifier after encountering `.`.  
- Example error message: `Expected member name after '.'`.

**Typical Error Code**  

```cpp
obj.        // member name missing after '.'
obj.();     // member name missing
```

**Solution**  

- Complete the member name after `.`.

---

## 32. Top-level declaration restriction (classes and functions can only be defined at the top level)

**Explanation**  
A class or named function was defined nested inside a function, method, or block.

**Details**  

- Class definitions and named function definitions are only allowed at the top level of the script.  
- Error message: `Class and function definitions are only allowed at top level`.  
- Anonymous functions are not subject to this restriction.

**Typical Error Code**  

```cpp
func f() {
    func g() {}   // nested named function
}
```

**Solution**  

- Move classes or named functions to the top level; use anonymous functions when local logic is needed.

---

## 33. Duplicate class

**Explanation**  
A class with the same name is defined more than once.

**Details**  

- The semantic analysis phase maintains a class name table and reports an error when a duplicate class name is encountered.  
- Example error message: `Duplicate class: X`.

**Typical Error Code**  

```cpp
class A {}
class A {}    // duplicate definition
```

**Solution**  

- Make sure class names are unique.

---

## 34. Duplicate member variable

**Explanation**  
A member variable with the same name is defined more than once in a class.

**Details**  

- Example error message: `Duplicate member variable: X`.

**Typical Error Code**  

```cpp
class A {
public:
    x = 1;
    x = 2;    // duplicate member
}
```

**Solution**  

- Use different names for member variables.

---

## 35. Unknown class / Unknown base class

**Explanation**  
A non-existent class is referenced in `new`, member access, or inheritance.

**Details**  

- Example error messages:  
  - `Unknown class: X`  
  - `Unknown base class: X`

**Typical Error Code**  

```cpp
x = new Missing();          // class does not exist
class B extends Missing {}  // base class does not exist
```

**Solution**  

- Check the class name spelling and the definition order.

---

## 36. Circular inheritance

**Explanation**  
The inheritance relationships of classes form a cycle.

**Details**  

- For example, A extends B and B extends A, or A extends A.  
- Example error message: `Circular inheritance involving class 'X'`.

**Typical Error Code**  

```cpp
class A extends B {}
class B extends A {}   // circular inheritance
```

**Solution**  

- Adjust the inheritance structure to avoid cycles.

---

## 37. Constructor and super call error (super(...) must be called)

**Explanation**  
A subclass constructor did not correctly call the base class constructor, or `super` was used on a class without a base class.

**Details**  

- When the base class constructor requires arguments, the subclass constructor must explicitly call `super(...)`: `Constructor of class 'X' must call super(...)`.  
- A subclass without a constructor cannot pass arguments to a base class constructor with parameters: `Class 'X' must define a constructor to call base constructor with arguments`.  
- Using `super` on a class without a base class: `Class 'X' has no base class`.

**Typical Error Code**  

```cpp
class Base {
    Base(x: int) {}
}
class Child extends Base {
    Child() {}    // super(...) not called
}
class A {
    A() { super(); }   // A has no base class
}
```

**Solution**  

- Call `super(...)` as needed in the subclass constructor, and do not use `super` on classes without a base class.

---

## 38. Cannot access private member

**Explanation**  
A `private` member variable or method was accessed outside the class.

**Details**  

- Private members can only be accessed by methods inside the class.  
- Example error message: `Cannot access private member 'X'`.

**Typical Error Code**  

```cpp
class A {
private:
    secret = 42;
}
a = new A();
a.secret;    // private member access
```

**Solution**  

- Access it through the `public` methods provided by the class, or change the member to `public`.

---

## 39. Class has no member

**Explanation**  
A member or static member that does not exist in the class was accessed.

**Details**  

- Example error messages:  
  - `Class 'X' has no member 'Y'`  
  - `Class 'X' has no static member 'Y'`

**Typical Error Code**  

```cpp
class A { public: x = 1; }
a = new A();
a.y;    // member y does not exist
```

**Solution**  

- Check the member name spelling, or add the corresponding member to the class first.

---

## 40. Method/function/constructor signature mismatch (No matching ...)

**Explanation**  
When calling a method, function, or constructor, the name is correct but the argument count or types do not match any overload.

**Details**  

- Example error messages:  
  - `No matching method 'X' ...`  
  - `No matching function 'X' ...`  
  - `No matching constructor for native class 'X' ...`  
- Constructor-related ones also include:  
  - `Class 'X' has no constructor`  
  - `Constructor of class 'X' expects N argument(s)`  
  - `Type mismatch for constructor parameter 'X' ...`

**Typical Error Code**  

```cpp
func f(x: int) -> int { return x; }
f("string");        // argument type mismatch

class A { A(x: int) {} }
a = new A("s");     // constructor argument type mismatch
```

**Solution**  

- Check the count and types of the call arguments and keep them consistent with the declared or registered signature.

---
## 41. Method call target error (Method call target is not an object)

**Explanation**  
Calling a method on a non-object value, or accessing a member of a non-object value.

**Description**  

- Error message examples:  
  - `Method call target is not an object`  
  - `Cannot access member of a non-object value`

**Typical Error Code**  

```cpp
a = 5;
a.x;        // Accessing a member of a non-object
a.f();      // Calling a method on a non-object
```

**Solution**  

- Make sure the call target is an object (e.g., an object created with `new`).

---

## 42. Array index error (Cannot index a non-array value)

**Explanation**  
Indexing a non-array value, or assigning to an index of a non-array value.

**Description**  

- Error message examples:  
  - `Cannot index a non-array value`  
  - `Cannot assign to an index of a non-array value`

**Typical Error Code**  

```cpp
a = 5;
a[0];       // Indexing a non-array
```

**Solution**  

- Use `[]` only on values of array type.

---

## 43. Type alias error (using alias conflict or circular reference)

**Explanation**  
A `using` type alias conflicts with a class name, is defined repeatedly, uses an invalid name, or has a circular reference.

**Description**  

- Error message examples:  
  - `Type alias conflicts with class name: X`  
  - `Duplicate type alias: X`  
  - `Cannot use 'X' as a type alias name`  
  - `Circular type alias involving 'X'`

**Typical Error Code**  

```cpp
using A = A;            // Circular alias
using A = int;
using A = string;       // Duplicate alias
```

**Solution**  

- Make sure the alias is unique, does not conflict with a class name, and does not reference itself circularly.

> [!TIP]
> I like blue, what about you? φ(*￣0￣)

---

## 44. Type does not exist or invalid type arguments (Unknown type)

**Explanation**  
Used a type that does not exist, or passed type arguments to a type that does not accept them.

**Description**  

- Error message examples:  
  - `Unknown type: X`  
  - `Type 'X' does not accept type arguments`

**Typical Error Code**  

```cpp
x: Missing = 1;          // Type does not exist
x: int<string> = 1;      // int does not accept type arguments
```

**Solution**  

- Check the spelling of the type name; provide type arguments only for types that support them.

---

## 45. Typed declaration missing an initializer (Typed declaration requires an initializer)

**Explanation**  
A typed variable declaration does not provide an initial value.

**Description**  

- Error message example: `Typed declaration of 'X' requires an initializer`.

**Typical Error Code**  

```cpp
x: int;    // Missing initial value
```

**Solution**  

- Add an initial value to the typed declaration, e.g., `x: int = 0;`.

---

## 46. Member has no default value (Member has no default value)

**Explanation**  
A class member has no default value, and the constructor does not assign one to it.

**Description**  

- Error message example: `Member 'X' has no default value ...`.

**Typical Error Code**  

```cpp
class A {
public:
    x;          // No default value
    A() {}      // The constructor does not assign one either
}
```

**Solution**  

- Provide a default value for the member, or assign one in the constructor.

---

## 47. Missing return statement (Missing return statement)

**Explanation**  
A function or anonymous function with a declared return type is missing a `return`.

**Description**  

- Error message examples:  
  - `Missing return statement in function 'X'`  
  - `Missing return statement in anonymous function`

**Typical Error Code**  

```cpp
func f() -> int {
    x = 1;    // No return
}
```

**Solution**  

- Return a value of the corresponding type on all code paths of the function.

---

## 48. Type mismatch (Type mismatch)

**Explanation**  
The type of an argument, return value, or assignment does not match the expected type.

**Description**  

- Error message format: `Type mismatch for <location>: expected <expected type>, got <actual type>` (the exact wording may vary).

**Typical Error Code**  

```cpp
func f(x: int) -> int { return x; }
f("string");    // Argument type mismatch
```

**Solution**  

- Convert the actual value to the expected type, or adjust the declaration.

---

## 49. Member default value must be a constant (Member default value must be a constant literal)

**Explanation**  
A class member default value must be a constant literal.

**Description**  

- Error message example: `Member default value of 'X' must be a constant literal`.  
- Member default values do not support expressions or variables.

**Typical Error Code**  

```cpp
class A {
public:
    x = 1 + 2;    // Not a literal
}
```

**Solution**  

- Change the default value to a literal, or assign one in the constructor.

---

## 50. Modulo by zero (Modulo by zero)

**Explanation**  
The divisor of a modulo operation is 0.

**Description**  

- Error message: `Modulo by zero`.

**Typical Error Code**  

```cpp
5 % 0;
```

**Solution**  

- Make sure the divisor of the modulo operation is not 0.

---

## 51. Operation on an empty optional value (empty optional)

**Explanation**  
Performing arithmetic or comparison operations on an empty `optional` value.

**Description**  

- Error message examples:  
  - `Cannot perform arithmetic on an empty optional value`  
  - `Cannot compare an empty optional value`

**Typical Error Code**  

```cpp
x: optional<int> = none;
y = x + 1;
```

**Solution**  

- Check whether the `optional` contains a value before operating on it.

---

## 52. Reading/writing fields of a non-object (Cannot load/store field)

**Explanation**  
Reading or writing a field of a non-object value.

**Description**  

- Error message examples:  
  - `Cannot load field 'X' from a non-object value`  
  - `Cannot store field 'X' on a non-object value`

**Typical Error Code**  

```cpp
a = 5;
a.x = 1;    // Writing a field of a non-object
```

**Solution**  

- Read and write fields only on objects.

---

## 53. Argument count mismatch (expects N argument(s))

**Explanation**  
The number of arguments in a method or function call does not match the declaration.

**Description**  

- Error message examples:  
  - `Method 'X' expects N argument(s), got M`  
  - `Function expects N argument(s), got M`

**Typical Error Code**  

```cpp
func f(x: int) -> int { return x; }
f();          // Missing argument
```

**Solution**  

- Provide the correct number of arguments as declared.

---

## 54. Function reference error (null function reference)

**Explanation**  
Called a null function reference, or the function reference lacks its owning bytecode.

**Description**  

- Error message examples:  
  - `Cannot call a null function reference`  
  - `Function reference has no owning bytecode chunk`

**Typical Error Code**  

(Usually caused by an internal logic error and is not easy to trigger directly in user code)

**Solution**  

- Check whether the function reference has been assigned correctly; if it is an internal error, please report it to the plugin developer.

---

## 55. Bytecode error (null bytecode / invalid instruction pointer)

**Explanation**  
The runtime executed an empty bytecode chunk, or the instruction pointer or the method/function body index is invalid.

**Description**  

- Error message examples:  
  - `Cannot run a null bytecode chunk`  
  - `Cannot execute a null bytecode chunk`  
  - `Invalid instruction pointer`  
  - `Invalid method ordinal`  
  - `Invalid function body index`  
  - `Invalid object class index`

**Typical Error Code**  

(Usually caused by abnormal compilation output or an internal error)

**Solution**  

- Check that the script is compiled and executed through the normal process; if it is an internal error, please report it to the plugin developer.

---

## 56. Native class error (Failed to create native class)

**Explanation**  
Failed to create a native class or load a native static field, or the method call target does not match the expected class.

**Description**  

- Error message examples:  
  - `Failed to create native class 'X': ...`  
  - `Failed to load native static field 'X': ...`  
  - `Method 'X' does not belong to this object`  
  - `Method call target is not an instance of the expected class`

**Typical Error Code**  

```cpp
new ObservableString("x", true);   // If the constructor arguments are wrong
```

**Solution**  

- Check the constructor arguments and field names of the native class; if it is an internal error, please report it to the plugin developer.

---

## 57. Native callback threw an error (callback threw)

**Explanation**  
When calling a native function, method, constructor, or macro, an error was thrown inside it.

**Description**  

- Error message format:  
  - `Function callback threw: <error content>`  
  - `Method callback threw: <error content>`  
  - `Constructor callback threw: <error content>`  
  - `Macro callback threw: <error content>`  
- The specific content depends on the native implementation.

**Typical Error Code**  

```cpp
math::sqrt(-1);     // If the native implementation checks the argument and reports an error
```

**Solution**  

- Check the arguments and the way of calling based on the error content.

---

## 58. Component errors (component)

**Explanation**  
A component definition or use violates the component system constraints.

**Description**  

- Error message format:  
  - `Component definitions are only allowed at top level` (a component is defined inside a function body or block)  
  - `Duplicate component '<name>'` (same-named components defined at different places)  
  - `Component '<name>' expects N argument(s), got M` (the call argument count does not match the component parameters)  
  - `Recursive component '<name>'` (a component calls itself directly or indirectly)  
  - `Component '<name>' can only be used as a statement inside a declarative UI block` (called outside a declarative block, or used as an expression)

**Typical Error Code**  

```lcui
component Bar(a, b) {
    divider(new DividerOptions());
}

func make() -> void {
    component Inner(x) {          // error: components may only be defined at top level
        divider(new DividerOptions());
    }
}

Bar("only-one");                  // error: argument count mismatch, and not inside a declarative block
```

**Solution**  

- Move the component definition to the top level of the script.
- Keep the call argument count consistent with the definition, and make sure component calls are statements inside a declarative UI block.
- Check whether the components called inside the component body form a cycle.

> [!TIP]
> You've read this far, take a break and listen to some music(～￣▽￣)～
