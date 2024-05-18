# Structure methods

Methods are special functions associated with structures.

## Arguments

The first argument of a method is always an instance of the type
that the method is associated with.

## Standard named methods

### `hash()`

```
func Object:hash() :: Int
```

Generates hash code for the object.

### `to_string()`

```
func Object:to_string(?format :: String|Bool) :: String
```

Generates a string representing the object.

The optional argument `format` should be a string specifying how to format it
or `true` to convert to a string representation.

### Comparison: `<=>()` and `==()`

```
func Object:\'<=>'(other) :: Int
func Object:\'=='(other) :: Bool
```

Compares two objects. The expected return values are listed as follows:

| Method  | `L < R` | `L = R` | `L > R` | not comparable |
|---------|:-------:|:-------:|:-------:|:--------------:|
| `<=>()` |  `< 0`  |   `0`   |  `> 0`  |     *error*    |
| `==()`  | `false` | `true`  | `false` |     `false`    |

### Unary operators support: `+#`, `-#`, `~`
```
func Object:\'+#'() :: Any   #=>  + object
func Object:\'-#'() :: Any   #=>  - object
func Object:\'~'() :: Any
```

### Binary operators support: `+`, `-`, `*`, `/`, `%`, `**`, `<<`, `>>`, `&`, `|`, `^`

```
func Object:\'+'(other) :: Any
func Object:\'-'(other) :: Any
func Object:\'*'(other) :: Any
func Object:\'/'(other) :: Any
func Object:\'%'(other) :: Any
func Object:\'**'(other) :: Any
func Object:\'<<'(other) :: Any
func Object:\'>>'(other) :: Any
func Object:\'&'(other) :: Any
func Object:\'|'(other) :: Any
func Object:\'^'(other) :: Any
```

### Element accessing: `[]`, `[]=`

```
func Object:\'[]'(key) :: Any   #=>   object[key]
func Object:\'[]='(key, value)  #=>   object[key] = value
```

### Invocation: `()`

```
func Object:\'()'(*args) :: Any   #=>   object(arg1, arg2, ...)
```