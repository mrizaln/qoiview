# qoiview

A simple Quite OK Image ([QOI](https://qoiformat.org/)) viewer written in C++20.

This project is initially part of my [qoipp](https://github.com/mrizaln/qoipp) example projects. But I realized that this may be useful as a standalone project, so here it is.

## Dependencies

Build dependencies

- C++20 capable compiler
- CMake
- Conan

Library dependencies

- fmt
- glfw
- glad
- cli11
- qoipp (FetchContent)

> all libraries are managed by Conan except if otherwise specified

## Building

```sh
conan install . --build missing -s build_type=Release
cmake --preset conan-release                          # conan-default if on Windows
cmake --build --preset conan-release
```

The built binary should be in the `build/Release/` directory with the name `qoiview`. You can move this binary anywhere you like.

## Input

Key navigation

|  key  | action                    |
| :---: | :------------------------ |
|   Q   | close                     |
|   H   | move left                 |
|   J   | move down                 |
|   K   | move up                   |
|   L   | move right                |
|   I   | zoom in                   |
|   O   | zoom out                  |
|   F   | toggle fullscreen         |
|   N   | toggle filtering          |
|   M   | toggle mipmap             |
|   R   | reset zoom and position   |
|   P   | print filename to console |
| RIGHT | next file                 |
| LEFT  | previous file             |
|  UP   | zoom in                   |
| DOWN  | zoom out                  |

Mouse navigation

|             | action            |
| :---------- | :---------------- |
| drag        | move image around |
| scroll up   | zoom out          |
| scroll down | zoom in           |

## Preview

https://github.com/user-attachments/assets/19c51592-34a2-4b39-875b-0a63a63498fd
