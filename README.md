# kvstore

A high-performance, in-memory key-value store built from scratch in modern C++17.  
Inspired by Redis — built to understand what Redis actually does under the hood.

## Features

- TCP server with concurrent client handling
- Fixed-size thread pool (no unbounded thread spawning)
- Commands: `SET`, `GET`, `DEL`, `EXPIRE`
- LRU eviction when memory limit is reached
- TTL-based key expiry with background cleanup
- Periodic snapshotting for persistence across restarts

## Build

**Requirements:** GCC 10+, CMake 3.16+, Linux / WSL2
```bash
git clone https://github.com//kvstore.git
cd kvstore
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j$(nproc)
```

## Run
```bash
./build/kvstore
```

Listens on port `6379` by default.

## Usage

Connect with netcat or any TCP client:
```bash
nc localhost 6379
```
```
SET name Akash
+OK
GET name
Akash
DEL name
+OK
EXPIRE name 10
+OK
```

## Architecture
```
Client → TCP Server → Thread Pool → Command Parser → Data Store → Persistence
```

| Module | Description |
|---|---|
| TCP Server | POSIX sockets, `SO_REUSEADDR`, accept loop |
| Thread Pool | Fixed workers, `std::condition_variable` task queue |
| Command Parser | Tokenises raw input into structured commands |
| Data Store | `unordered_map` + LRU eviction + TTL expiry |
| Persistence | Binary snapshots, restore on restart |

## Build Steps (Development Log)

| Step | What was built |
|---|---|
| 1 | TCP server + project scaffold |
| 2 | Thread pool |
| 3 | Command parser |
| 4 | Data store + TTL |
| 5 | LRU eviction |
| 6 | Persistence |