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

## Signatures and behaviors of special functions (C/C++)

### Object-creating functions

Creating a new object may trigger garbage collection (GC).
Functions that create objects shall make sure that
pointers to objects are in GC roots during GC.
Otherwise, the objects might be freed or moved
and the pointers then became dangling ones.

An easy solution is to put pointers onto the runtime callstack before creating objects
and update the local variables after that.
If the function cannot access the callstack directly,
`zis_callstack_frame_alloc_temp()` can be used to allocate temporary stack storages.

Here is an example:

```c
struct xyz_obj *xyz_obj_add(zis_context *z, struct xyz_obj *a, struct xyz_obj *b) {
    struct zis_object **temp_regs = zis_callstack_frame_alloc_temp(z, 2);
    temp_regs[0] = zis_object_from(a), emp_regs[1] = zis_object_from(b); // Put onto stack.
    struct xyz_adder_obj *adder = xyz_adder_obj_new(z); // Create object.
    a = zis_object_cast(temp_regs[0], struct xyz_obj),
    b = zis_object_cast(temp_regs[1], struct xyz_obj); // Update references.
    zis_callstack_frame_free_temp(z, 2);
    return xyz_adder_obj_add(adder, a, b);
}
```

Considering that this method can slightly impact performance,
do not use it in unnecessary situations or paths.

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
