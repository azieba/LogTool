# LogTool
Test program 

## How to use

Calling logTool without parameters will print the help.

## Building on Linux

### Prerequisites
- C++17 or newer
- CMake
- libargparse-dev
- libxxhash-dev
- libzstd-dev

### Build Instructions
```bash
git clone https://github.com/azieba/LogTool.git
cd LogTool
cmake -B build
cmake --build build
```

This will produce statically linked logTool executable in build directory.

## Building on Windows
Not supported yet.
