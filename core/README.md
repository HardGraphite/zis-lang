# Core

Source code for the runtime core.

+ runtime-independent utilities (`*util.*`)
  - [`algorithm.h`](algorithm.h)
  - [`attributes.h`](attributes.h)
  - [`bits.h`](bits.h)
  - [`compat.h`](compat.h)
  - [`ndefutil.h`](ndefutil.h)
  - [`platform.h`](platform.h)
+ runtime context
  - [`api.c`](api.c)
  - [`context.h`](context.h), [`context.c`](context.c)
  - [`debug.h`](debug.h), [`debug.c`](debug.c)
  - [`globals.h`](globals.h), [`globals.c`](globals.c)
  - [`memory.h`](memory.h), [`memory.c`](memory.c)
  - [`stack.h`](stack.h), [`stack.c`](stack.c)
+ object system support (`obj*.*`)
  - [`object.h`](object.h)
  - [`objmem.h`](objmem.h), [`objmem.c`](objmem.c)
  - [`smallint.h`](smallint.h)
+ built-in types (`*obj.*`)
  - [`boolobj.h`](boolobj.h), [`boolobj.c`](boolobj.c)
  - [`floatobj.h`](floatobj.h), [`floatobj.c`](floatobj.c)
  - [`intobj.h`](intobj.h), [`intobj.c`](intobj.c)
  - [`nilobj.h`](nilobj.h), [`nilobj.c`](nilobj.c)
  - [`typeobj.h`](typeobj.h), [`typeobj.c`](typeobj.c)