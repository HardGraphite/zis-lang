# The ZiS Assembly

## Syntax

```
# Comment line.

<OP_NAME> <OPERAND1>[, <OPERAND2>[, <OPERAND3>]]

.<PSEUDO_OP> <OPERANDS>
```

It is not case-sensitive.

## List of operations

See [oplist.txt](../core/oplist.txt).

## List of pseudo operations

### Define a function

```
.FUNC <NA>,<NO>,<NR>

# body ...

.END
```

See `struct zis_func_obj_meta` for details about the operands `<NA>,<NO>,<NR>`.

### Define a constant

```
# Integer
.CONST I:123

# Floating-point
.CONST F:12.3

# String
.CONST S:abc
```

### Define a symbol

```
.SYM abc
```
