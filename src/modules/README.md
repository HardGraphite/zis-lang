# Modules

## Module conf

The *module conf* is key-value pairs in Unix conf / Windows INI format
that provides information so that CMake can know how to define targets for the modules.
Note that the lists in the config are double-bars (`"||"`) separated.
The available keys and default values are listed in [modules/module.conf](module.conf).

For a module defined in a directory,
the module conf shall be written in a file called "`module.conf`",
or be generated with CMake function `zis_make_module_conf()`
(see [modules/ModuleConf.cmake](ModuleConf.cmake)) in a "`module.conf.cmake`" file.
For a module defined in a single file,
the module conf shall be written as comment lines with prefix "`//%%`";
for example:

```c
//%% [module]
//%% name = hello
//%% default-enabled = NO
```

Modules whose `force-embedded` option is true
will have the project root directory added to the include path list.
That means, you can use "`#include <core/xxx.h>`"
to include core headers in such module source files.
