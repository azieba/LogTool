cmake_minimum_required(VERSION 3.10)
project(logTool LANGUAGES CXX)

# Set C++ standard (e.g. C++17)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Prefer static libraries
set(BUILD_SHARED_LIBS OFF)
set(CMAKE_FIND_LIBRARY_SUFFIXES ".a")

# Ensure we link libc++ statically too (optional)
set(CMAKE_EXE_LINKER_FLAGS "-static")

# Collect all .cpp files in src/
file(GLOB SRC_FILES CONFIGURE_DEPENDS "${CMAKE_SOURCE_DIR}/src/*.cpp")

# Define the executable
add_executable(${PROJECT_NAME} ${SRC_FILES})

target_link_libraries(logTool PRIVATE xxhash zstd)
