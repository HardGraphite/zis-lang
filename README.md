# The ZiS Programming Language

<!-- ![](misc/logo.svg) -->

<div style="text-align: center;">
<img src="misc/logo.svg" width="30%"/>
</div>

This is the **ZiS** language,
a simple dynamic programming language.

See [VERSION.txt](VERSION.txt) for the current version number.

## Building

[CMake](https://cmake.org/) is used as the build system.
A C/C++ compiler (GCC/Clang/MSVC) is also required.

You can build this project with default configuration using `cmake` like this:

```sh
mkdir build && cd build
cmake -DCMAKE_BUILD_TYPE=Release ..
cmake --build .
```

For multi-config generators (like Visual Studio),
variable `CMAKE_BUILD_TYPE` does not control the build type.
The command-line option `--config` should be used instead
to specify the build configuration (build type) when building:

```sh
cmake --build . --config Release
```

To build with debug code, enable the options `ZIS_DEBUG` and `ZIS_DEBUG_LOGGING`.
To build documentations, enable the `ZIS_DOC*` options.
To run tests, enable the option `ZIS_TEST` and use command `ctest`.
To install the binaries, use command `cmake` with command-line argument `--install`.
To generate a package, enable the option `ZIS_PACK` and use command `cpack`.

Use commands `ccmake` or `cmake-gui` to view and customize the options.

## File Organization

The project files are organized as follows:

| Directory | Description                                       |
|-----------|---------------------------------------------------|
| `cmake`   | CMake scripts                                     |
| `include` | header files for the core runtime                 |
| `core`    | source for the core runtime [...](core/README.md) |
| `start`   | source for the executable entry                   |
| `modules` | source for the modules [...](modules/README.md)   |
| `test`    | test code                                         |
| `tools`   | programs that help develop                        |
| `doc`     | documentations                                    |
| `misc`    | miscellaneous files                               |
