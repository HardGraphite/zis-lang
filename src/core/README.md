# Core

Source code for the runtime core.

## Files

- runtime-independent utilities (`*util.*`, etc.)
- runtime context (`context.h` and related)
- bytecode compiler
- object system support (`obj*.*`, etc.)
- built-in types (`*obj.*`)


## Conventions and precautions

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

### Object-creating functions

Creating a new object may trigger garbage collection (GC).
Functions that create objects shall make sure that
pointers to objects are in GC roots during GC.
Otherwise, the objects might be freed or moved
and the pointers then became dangling ones.

An easy solution is to put the pointers to objects onto the runtime callstack before creating objects
and update the local variables from the callstack after that.

#### Native function local references

The **locals** mechanism is available
to *protect* the references stored in local variables.

Here is an example:

```c
struct xyz_obj *xyz_obj_add(zis_context *z, struct xyz_obj *_a, struct xyz_obj *_b) {
    zis_locals_decl(
        z, var,
        struct xyz_obj *a, *b;
    ); // Declare local references.
    // zis_locals_zero(var); // This line is not needed here.
    var.a = _a. var.b = _b; // Initialize the variables.
    struct xyz_adder_obj *adder = xyz_adder_obj_new(z); // Create object.
    struct xyz_obj *y = xyz_adder_obj_add(adder, var.a, var.b);
    zis_locals_drop(z, var); // Drop the references.
    return y;
}
```

Considering that this method can slightly impact performance,
do not use it in unnecessary situations or paths.

#### Temporary callstack registers

If the function cannot access the callstack directly,
`zis_callstack_frame_alloc_temp()` can be used to allocate temporary stack storages.

Here is an example:

```c
struct xyz_obj *xyz_obj_add(zis_context *z, struct xyz_obj *a, struct xyz_obj *b) {
    struct zis_object **temp_regs = zis_callstack_frame_alloc_temp(z, 2);
    temp_regs[0] = zis_object_from(a), temp_regs[1] = zis_object_from(b); // Put onto stack.
    struct xyz_adder_obj *adder = xyz_adder_obj_new(z); // Create object.
    a = zis_object_cast(temp_regs[0], struct xyz_obj),
    b = zis_object_cast(temp_regs[1], struct xyz_obj); // Update references.
    zis_callstack_frame_free_temp(z, 2);
    return xyz_adder_obj_add(adder, a, b);
}
```

Compared with the **locals** mechanism,
this method is slightly slower and less convenient for reference keeping.
However, this is the only way to allocate slots of arbitrary size from the stack.

#### Callstack registers as arguments

**This approach has been deprecated. DO NOT use it if no necessary.**

A function may accept a vector of callstack registers
instead of allocated temporary ones.

Here is an example:

```c
/// R = { [0] = a, [1] = b, [2] = out_sum }
void xyz_obj_add_r(zis_context *z, struct zis_object *regs[static 3]) {
    struct xyz_adder_obj *adder = xyz_adder_obj_new(z);
    CHECK_XYZ_OBJ_TYPE(regs[0]), CHECK_XYZ_OBJ_TYPE(regs[1]);
    struct xyz_obj *a = zis_object_cast(regs[0], struct xyz_obj);
    struct xyz_obj *b = zis_object_cast(regs[1], struct xyz_obj);
    regs[2] = xyz_adder_obj_add(adder, a, b);
}
```

Do not forget the function name suffix "`_r`" and the "`R = {...}`" comment!
Since some compilers do not support the `static` keyword in a parameter array declaration,
you may need the macro `ZIS_PARAMARRAY_STATIC` in file "`compat.h`".
