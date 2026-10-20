cmake_minimum_required(VERSION 3.29)
project(screenrecord LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Writes build/compile_commands.json, which clangd and clang-tidy read to
# learn each file's include paths and flags. Ninja generator only.
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

add_subdirectory(src)
