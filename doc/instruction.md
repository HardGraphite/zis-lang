# Instruction

## List of instructions

See [oplist.txt](../core/oplist.txt).

## Binary formats

Type `Asw`:

```text
31               7 6   0
+-----------------+----+
|        A        | OP |
+-----------------+----+
|<       25      >|< 7>|
```

Type `ABw`/`ABsw`:

```text
31        16 15  7 6   0
+-----------+-----+----+
|     B     |  A  | OP |
+-----------+-----+----+
|<    16   >|< 9 >|< 7>|
```

Type `ABC`:

```text
31  24 23 16 15  7 6   0
+-----+-----+-----+----+
|  C  |  B  |  A  | OP |
+-----+-----+-----+----+
|< 8 >|< 8 >|< 9 >|< 7>|
```

where `OP` is an opcode and `A,B,C` are operands.
Suffix `s` means signed and `w` means wide.

The opcodes are 7-bits unsigned integers.
The operands are 8-/9-/16-/25-bits singed or unsigned integers.
