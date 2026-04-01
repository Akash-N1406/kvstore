cmake_minimum_required(VERSION 3.16)
project(kvstore VERSION 0.1.0 LANGUAGES CXX)

# ── C++17 everywhere ──────────────────────────────────────────────────────────
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)          # no GNU extensions, strict standard

# ── Compiler warnings ─────────────────────────────────────────────────────────
add_compile_options(-Wall -Wextra -Wpedantic)

# ── Collect all .cpp files under src/ ─────────────────────────────────────────
file(GLOB_RECURSE SOURCES "src/*.cpp")

add_executable(kvstore ${SOURCES})

# ── Include path: lets us write #include "server/Server.hpp" anywhere ─────────
target_include_directories(kvstore PRIVATE src)

# ── pthreads (needed for std::thread on Linux) ────────────────────────────────
find_package(Threads REQUIRED)
target_link_libraries(kvstore PRIVATE Threads::Threads)