# src

Source code directory.

## File Organization

| Directory | Description                                       |
|-----------|---------------------------------------------------|
| `core`    | source for the core runtime [...](core/README.md) |
| `start`   | source for the executable entry                   |
| `modules` | source for the modules [...](modules/README.md)   |

## CMake targets and variables

See [CMakeLists.txt](CMakeLists.txt).

| Target          | Description                                 |
|-----------------|---------------------------------------------|
| `zis_core_tgt`  | the core runtime, may be shared or static   |
| `zis_start_tgt` | the executable entry                        |

| Variable                      | Description                                  |
|-------------------------------|----------------------------------------------|
| `zis_src_generated_code_dir`  | directory where generated files should be    |
