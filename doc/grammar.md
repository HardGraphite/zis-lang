# Grammar Specification

## Lexical elements

### Literals and identifier

#### Integer literals

```
lit_int
    = ( { ?/[0-9]/? } )                         (* DEC *)
    | ( "0" ("b" | "B") { ?/[0-1_]/? } )        (* BIN *)
    | ( "0" ("o" | "O") { ?/[0-7_]/? } )        (* OCT *)
    | ( "0" ("o" | "X") { ?/[0-9a-fA-F_]/? } )  (* HEX *)
    ;
```

Optional case-insensitive prefix "`0b`", "`0o`", or "`0x`"
indicates the base of the integer literal.
Optional underscore "`_`" may be inserted between the digits as a separator.

Examples:

```
0       #=>  0
123     #=>  123
0123    #=>  123
0b0110  #=>  6
0Xff    #=>  255
12_34   #=>  1234
```

#### Floating-point literals

```
lit_float
    = lit_int "." ?lit_int without prefix?
    ;
```

Examples:

```
0.0    #=> 0.0
1.1    #=> 1.1
0xf.f  #=> 15.9375
```

#### String literals

```
lit_string
    = ?lit_str_delim? ?lit_str_char_or_esc_seq? ?lit_str_delim?
    | "@" ?lit_str_delim? ?lit_str_char_seq? ?lit_str_delim?
    ;
```

The `?lit_str_delim?` is a quotation mark (`'` or `"`).
The `?lit_str_char_or_esc_seq?` and `?lit_str_char_seq?` are
sequences of any characters excepting standalone `?lit_str_delim?`s.
In `?lit_str_char_or_esc_seq?`,
escape sequences (starting with backslash `\`) are used to represent special characters;
while in `?lit_str_char_seq?`,
each character is scanned as it is.

Escape sequences:

| Sequence  | Description                |
|:---------:|----------------------------|
|   `\'`    | single quote    (`U+0027`) |
|   `\"`    | double quote    (`U+0022`) |
|   `\\`    | backslash       (`U+005C`) |
|   `\a`    | audible bell    (`U+0007`) |
|   `\b`    | backspace       (`U+0008`) |
|   `\f`    | form feed       (`U+000C`) |
|   `\n`    | line feed       (`U+000A`) |
|   `\r`    | carriage return (`U+000D`) |
|   `\t`    | horizontal tab  (`U+0009`) |
|   `\v`    | vertical tab    (`U+000B`) |
| `\x`*nn*  | byte `0x`*nn*              |
| `\u{...}` | Unicode character `U+`...  |

Examples:

```
"hello, world"
'bye, world'
'*line-1*\n*line-2*'   #=> *line-1*<LF>*line-2*
"\x7e1"                #=> ~1
'\u{4f60}\u{597D}^_^'  #=> <U+4F60><U+597D>^_^
@"\\\"                 #=> \\\
@'*line-1*\n*line-1*'  #=> *line-1*\n*line-1*
```

#### Identifiers

An identifier consists of any visible characters excepting whitespaces and punctuations.
Specially, the first character of an identifier must not be a digit (0,1,2,...,9).
A normal identifier cannot be the same with any of the keywords literally.
To include special characters in an identifier,
a string-like syntax can be used: `"\" ?lit_string?`.

Examples:

```
i
foo_bar_1
AnIdentifier
\"func"       #=>  func
\"<\x41>"     #=>  <A>
```

### Keywords

```
nil
true
false
func
struct
if
elif
else
while
for
break
continue
return
throw
end
```

### Operators

Unary operators:

| Operator | Precedence | Description |
|:--------:|:----------:|-------------|
|   `+`    |     -3     | positive    |
|   `-`    |     -3     | negative    |
|   `~`    |     -3     | bitwise not |
|   `!`    |     -3     | logical not |

Binary operators:

|   Operator   | Precedence | Description               |
|:------------:|:----------:|---------------------------|
|     `+`      |     5      | addition                  |
|     `-`      |     5      | subtraction               |
|     `*`      |     4      | multiplication            |
|     `/`      |     4      | division                  |
|     `%`      |     4      | remainder                 |
|     `<<`     |     6      | left shift                |
|     `>>`     |     6      | right shift               |
|     `&`      |     10     | bitwise and               |
|    &#124;    |     12     | bitwise or                |
|     `^`      |     11     | bitwise exclusive-or      |
|     `**`     |     3      | power                     |
|     `=`      |    -15     | assignment                |
|     `+=`     |    -15     | assignment by sum         |
|     `-=`     |    -15     | assignment by difference  |
|     `*=`     |    -15     | assignment by product     |
|     `/=`     |    -15     | assignment by quotient    |
|     `%=`     |    -15     | assignment by remainder   |
|    `<<=`     |    -15     | assignment by left shift  |
|    `>>=`     |    -15     | assignment by right shift |
|     `&=`     |    -15     | assignment by and         |
|   &#124;=    |    -15     | assignment by or          |
|     `^=`     |    -15     | assignment by xor         |
|     `==`     |     9      | equal to                  |
|     `!=`     |     9      | not equal to              |
|     `<`      |     8      | less than                 |
|     `<=`     |     8      | less than or equal to     |
|     `>`      |     8      | greater than              |
|     `>=`     |     8      | greater than or equal to  |
|     `&&`     |     13     | logical and               |
| &#124;&#124; |     14     | logical or                |
|     `..`     |     13     | exclusive-end range       |
|    `...`     |     13     | range                     |
|     `.`      |     1      | field access              |
|     `:`      |     1      | method access             |
|   `(...)`    |     2      | invocation                |
|   `[...]`    |     2      | element access            |

### Others

#### End of statement mark

Both line-feed (`U+000A`) and semicolon (`;`) characters act as the mark of the end of a statement.
But they are ignore in some cases like being inside parentheses.

#### Comments

A "`#`" is used to start a line comment.
The content from it to the end of the line are ignored.

## Syntax rules

### Expression

```
expr
    = lit_int | lit_float | lit_string | identifier
    | "nil" | "true" | "false"
    | un_op expr
    | expr bin_op expr
    | call_expr
    | subscript_expr
    | tuple_expr
    | array_expr
    | map_expr
    | "(" expr ")"
    ;

tuple_expr
    = "(" ")"
    | "(" expr "," ")"
    | "(" expr { "," expr } [ "," ] ")"
    ;

call_expr
    = expr "(" ")"
    | expr "(" expr [{ "," expr }] [ "," ] ")"
    ;

array_expr
    = "[" "]"
    | "[" expr [{ "," expr }] [ "," ] "]"
    ;

subscript_expr
    = expr "[" expr "]"                          (* key is passed as it is *)
    | expr "[" expr [{ "," expr }] [ "," ] "]"   (* keys are packed as a tuple *)
    ;

map_expr
    = "{" "}"
    | "{" map_elem_expr [{ "," map_elem_expr }] [ "," ] "}"
    ;
map_elem_expr
    = expr "->" expr
    ;
```

Notes:

- For unary and binary expressions, the precedences and associativities matter.
- Trailing commas are allowing in bracket-rounded expressions.
- In a `tuple_expr`, the trailing comma cannot be omitted if there is exactly one element.
- In a `subscript_expr` where the keys are expected to be passed as a tuple,
    the trailing comma cannot be omitted if there is exactly one key.
- In a bracket-rounded expression, end-of-line tokens are ignored.

Examples:

```
pi = 4 - (4 / (3 * 2 * 1)) - (12 / 5 / 4 / 3 / 2)
pos = (x,)
coord = (pos[1], y)
seq = [ 3, 2, 1, coord[1], coord[2] ]
digits = {
    'one'   -> 1,
    'two'   -> 2,
    'three' -> 3,
}
q = seq[digits[num_to_str(pi:floor())]]
```

#### Assignment

```
assign_expr
    = identifier           ASSIGN_OP expr
    | expr "." identifier  ASSIGN_OP expr
    | subscript_expr       ASSIGN_OP expr
    ;
```

### Import statement

```
import_stmt = "import" expr EOS
```

### Return statement

```
return_stmt = "return" [ expr ] EOS
```

### Throw statement

```
throw_stmt = "throw" [ expr ] EOS
```

### Break & continue statement

```
break_stmt = "break" EOS
continue_stmt = "continue" EOS
```

### If-else statement

```
cond_stmt =
    "if" expr EOS
        block
    [{ "elif" expr EOS
        block }]
    [ "else" expr EOS
        block ]
    "end" EOS;
```

Examples:

```
if num < 0
    num = -num
end

if num < 50
    desc = "failed"
elif num < 70
    desc = "passed"
elif num < 80
    desc = "good"
else
    desc = "excitant"
end
```

### While statement

```
while_stmt =
    "while" expr EOS
        block
    "end" EOS;
```

Examples:

```
while !ok
    retry()
end
```

### Func statement

```
func_stmt =
    "func" identifier "(" func_arg_list ")" EOS
        block
    "end" EOS;
func_arg_list
    = (* empty *)
    | identifier [{ "," identifier }] [ "," ]
    ;
```

## Concepts

### Variables and scopes

A variable is defined in current scope at the first time it is assigned to.
To look up for a variable, the local scope is first search, and then the global scope.

A function body introduces a new variable scope.
The followings introduce weak variable scopes (scopes that inherit from the parent function scope):

- `for` block (loop)
- `while` block (loop)

### The `main()` function

The `main()` function is a module's top level function named "`main`".
It is optional.
It is automatically called after module being initialized
if the module is run as the entry point.

The function may accept one argument, an array of command line arguments.
Or it can take on argument, which does not cause an error.
The function can return an integer as the exit code.
A non-Int return value will be replaced with "`0`", which represents success.

Here is an example:

```
$ cat test.zis
print("TEST")
func main(args)
    print(args)
end

$ zis test.zis "Hello, world!"
["test.zis", "Hello, world!"]
```
