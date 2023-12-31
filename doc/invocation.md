# Invocation of Callable Objects

## Callable objects

The basic callable objects are instances of type `Function`,
inside which are actually bytecode or native functions.

### Bytecode functions

Bytecode functions consist of bytecode instruction sequences.

### Native functions

The native function (C function) in a `Function` object
shall follow the signature `zis_native_func_t` declared in "`include/zis.h`".
It takes a `zis_t` (aka `struct zis_context *`) as the argument
and returns an `int` as status (`ZIS_OK` or `ZIS_THR`).
To return normally,
store the return value in the first register (`REG-0`) and use status code `ZIS_OK`;
to throw an object,
store the object in the same place (`REG-0`) and use status code `ZIS_THR`.

Here is an example:

```c
int F_add_int(zis_t z) {
    int64_t lhs, rhs;
    if (zis_read_values(z, 1, "ii", &lhs, &rhs)) { // Parses arguments.
        zis_make_exception(z, 0, "type", (unsigned int)-1, NULL);
        return ZIS_THR; // Throws an exception.
    }
    zis_make_int(z, 0, lhs + rhs);
    return ZIS_OK; // Returns the result.
}
```

In C++, a native function shall be marked
`noexcept` (after C++11) or `throw()` (before C++17).

## Invocation context

### Callstack frame

A callstack frame is allocated for each invocation,
where there are slots for arguments and local variables (aka registers).

```text
| LOC-M | <- frame top
|  ...  |  ^
| LOC-3 | local variables
| LOC-2 |  v
| LOC-1 | ---
| ARG-N | ---
|  ...  |  ^
| ARG-3 | arguments
| ARG-2 |  v
| ARG-1 | <- REG-1
| TEMP  | <- frame base, REG-0
```

The first register (`REG-0`) is for temporary use only.
Any instructions or API functions may modify `REG-0`.
Data should not be stored here except as input or output to instructions or functions.

Function arguments are placed in order starting from the second register (`REG-1`).
The rest of the registers are for local variables and may contain specific data.

### Associated module

Every function has a module associated with it.
Such a module provides global variables.
Global variables can be accessed with either names (symbol) or indices (integer).

### Constants and symbols

Every function has a constant table and a symbol table associated with it.
The tables are read-only and can be visited with indices (integer).

## Invocation steps

When an object is invoked, the following operations are performed:

1. **Checks the type of the object.**
If it is a `Function`, continues the invocation.
Otherwise, throws an exception to report the type error.
2. **Enters a new frame.**
Records current context.
Moves the stack frame pointer and top pointer to make space for the frame.
Panics if there is no sufficient space in the stack.
Fills the registers with known objects to avoid dangling references.
3. **Places arguments.**
Copies arguments to the new frame.
Throws an exception if the arguments do not match that declared in the function.
4. **Executes the function.**
Calls the C function if given;
jump to the bytecode otherwise.
5. **Leaves the frame.**
Copies the return value to the caller frame and pops current frame.
Recover the previously recorded context.
