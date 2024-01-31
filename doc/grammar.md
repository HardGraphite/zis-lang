# Grammar Specification

## Lexical elements

### Literals and identifier

#### Integer literals

```
lit_int
  = ( { ?/[0-9]/? } )                        (* DEC *)
  | ( "0" ("b" | "B") { ?/[0-1]/? } )        (* BIN *)
  | ( "0" ("o" | "O") { ?/[0-7]/? } )        (* OCT *)
  | ( "0" ("o" | "X") { ?/[0-9a-fA-F]/? } )  (* HEX *)
  ;
```

Examples:

```
0       #=>  0
123     #=>  123
0123    #=>  123
0b0110  #=>  6
0Xff    #=>  255
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
  = ( "'" { (- "'") } "'" )
  | ( '"' { (- '"') } '"' )
  ;
```

Examples:

```
"hello, world"
'bye, world'
```

#### Identifiers

An identifier consists of any visible characters excepting whitespaces and punctuations.
Specially, the first character of an identifier must not be a digit (0,1,2,...,9).
An identifier cannot be the same with any of the keywords literally.

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
