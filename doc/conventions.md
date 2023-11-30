# Conventions

## C/C++ coding style

### Characters

- Use UTF-8 encoding. Do not use non-ASCII characters if possible.
- Use LF (`U+000A`) as the end of line.

### Indentation and line width

- Use spaces instead of tabs for indentation.
- Each level of indentation must be 4 spaces.
- The recommended maximum width for a line is 80 columns. Never exceed 100.
- Indentations for preprocessing directives are inserted after "`#`".

### Braces and spaces

- For a code block,
  opening brace shall be put at the end of a line
  and closing brace shall be put at the beginning of a line.
- Use a space after keywords `if`, `switch`, `case`, `for`, `while`, `do`.
- Use a space around (on each side of) binary and ternary operators.
- Use a space around (on each side of) "`//`" and "`/*`"
  unless they are at the beginning of a line.
- May add extra spaces around operators to align them.
- No space around the `.` and `->` operators.
- No space after unary operators.
- No trailing whitespace.

### Breaking long lines

A line longer than 80 columns shall be broken,
unless it increases readability to keep them in a line.
Prefer block indent rather than visual indent.
Indentation level shall be increased for wrapped line,
but do not insert extra spaces to align with the first line.
To break parentheses in function argument list
or `if () {}`, `for () {}`, `while () {}` statements,
start new line (optional) after opening parenthesis ("`(`")
and move the closing parenthesis and opening brace ("`) {`") to a new line
without extra indentation level.
If return type and specifiers are too long (longer than 40 columns)
while function name and argument list are short,
instead of breaking the argument list,
the return type and specifiers can be put in stand-alone lines.

### Naming

- Identifiers (namespaces, variables, functions, and types) shall be `snake_case`.
- Macros shall be `SCREAMING_SNAKE_CASE`.
- Use prefix "`zis_`".

## Signatures and behaviors of special functions

### Object-creating functions

Functions that creates a new object shall a pointer to an object pointer (the "receiver").
The created object is going to be stored in the "receiver".

```c
void xyz_obj_new(zis_context *z, struct zis_object **ret, int flags) {
    struct xyz_obj *obj = make_xyz(z, flags); // Create an object.
    *ret = (struct zis_object *)obj; // Assign to the "receiver".
}
```

It is not possible to place a write barrier in such a function.
So, the caller must make sure that the "receiver" do not need a write barrier.
To be practical, the "receiver" can be on the stack, which is a GC root.

### Collection-traversing functions

Functions that traverse a collection shall follow the example:

```c
typedef int(*xyz_visitor_t)(void *item, void *arg);

int xzy_foreach(struct xyz *data, xyz_visitor_t fn, void *fn_arg) {
    for (auto && item : *data) {
        int fn_ret = fn(&item, fn_arg); // Pass `fn_arg` as the last argument.
        if (fn_ret) // Stop traversing when `fn` returns non-zero value.
            return fn_ret; // Return the returned value.
    }
    return 0; // Return 0 if all items have been visited and `fn` always returns 0.
}
```
