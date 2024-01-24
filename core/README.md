# Core

Source code for the runtime core.

## Files

+ runtime-independent utilities (`*util.*`)
  - [`algorithm.h`](algorithm.h)
  - [`attributes.h`](attributes.h)
  - [`bits.h`](bits.h)
  - [`compat.h`](compat.h)
  - [`fsutil.h`](fsutil.h), [`fsutil.c`](fsutil.c)
  - [`memory.h`](memory.h), [`memory.c`](memory.c)
  - [`platform.h`](platform.h)
  - [`strutil.h`](strutil.h), [`strutil.c`](strutil.c)
+ runtime context
  - [`api.c`](api.c)
  - [`context.h`](context.h), [`context.c`](context.c)
  - [`debug.h`](debug.h), [`debug.c`](debug.c)
  - [`globals.h`](globals.h), [`globals.c`](globals.c)
  - [`invoke.h`](invoke.h), [`invoke.c`](invoke.c)
  - [`loader.h`](loader.h), [`loader.c`](loader.c)
  - [`stack.h`](stack.h), [`stack.c`](stack.c)
+ object system support (`obj*.*`)
  - [`locals.h`](locals.h), [`locals.c`](locals.c)
  - [`ndefutil.h`](ndefutil.h)
  - [`object.h`](object.h), [`object.c`](object.c)
  - [`objmem.h`](objmem.h), [`objmem.c`](objmem.c)
  - [`smallint.h`](smallint.h)
+ built-in types (`*obj.*`)
  - [`arrayobj.h`](arrayobj.h), [`arrayobj.c`](arrayobj.c)
  - [`boolobj.h`](boolobj.h), [`boolobj.c`](boolobj.c)
  - [`bytesobj.h`](bytesobj.h), [`bytesobj.c`](bytesobj.c)
  - [`exceptobj.h`](exceptobj.h), [`exceptobj.c`](exceptobj.c)
  - [`floatobj.h`](floatobj.h), [`floatobj.c`](floatobj.c)
  - [`funcobj.h`](funcobj.h), [`funcobj.c`](funcobj.c)
  - [`intobj.h`](intobj.h), [`intobj.c`](intobj.c)
  - [`mapobj.h`](mapobj.h), [`mapobj.c`](mapobj.c)
  - [`moduleobj.h`](moduleobj.h), [`moduleobj.c`](moduleobj.c)
  - [`nilobj.h`](nilobj.h), [`nilobj.c`](nilobj.c)
  - [`pathobj.h`](pathobj.h), [`pathobj.c`](pathobj.c)
  - [`streamobj.h`](streamobj.h), [`streamobj.c`](streamobj.c)
  - [`stringobj.h`](stringobj.h), [`stringobj.c`](stringobj.c)
  - [`symbolobj.h`](symbolobj.h), [`symbolobj.c`](symbolobj.c)
  - [`tupleobj.h`](tupleobj.h), [`tupleobj.c`](tupleobj.c)
  - [`typeobj.h`](typeobj.h), [`typeobj.c`](typeobj.c)

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
    // zis_locals_zero(z, var); // This line is not needed here.
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
