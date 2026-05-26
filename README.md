# TSDB

A small time-series database engine written in C++ for timestamped numeric
measurements. The project accepts data over TCP, stores recent points in
memory, flushes older points into compressed chunk files on disk, and supports
range queries, aggregations, statistics, and manual flushing.

This project is based on Gorilla-style compression ideas:

- delta-of-delta encoding for timestamps
- XOR encoding for double values
- bit-level packing into binary chunk files

## Features

- TCP server with one thread per client
- Interactive CLI client
- In-memory per-metric head blocks
- Write-ahead style `wal.bin` for unflushed points
- Monotonic timestamp validation per metric
- Compressed on-disk chunk files
- `PUT`, `GET`, `AGG`, `STATS`, `FLUSH`, and `QUIT` commands
- CRC-protected chunk reads
- Benchmark for ingestion and compression ratio
- Unit and round-trip compression tests
- Extra retention/older-chunk handling experiments in the codebase

## Project Layout

```text
Tsdb/
├── benchmarks/
│   ├── benchmark.cpp
│   └── benchmark.log
├── include/
│   ├── config.h
│   └── helpers.h
├── src/
│   ├── compression.h
│   ├── Parser.cpp
│   └── Parser.h
├── tests/
│   ├── test.cpp
│   └── testing.log
├── client.cpp
├── design.txt
├── Makefile
├── manual.pdf
├── retention_config.h
├── server.cpp
└── server_config.h
```

## Architecture

The system is organized into three main parts.

### 1. Network and command layer

Files:

- `server.cpp`
- `client.cpp`
- `src/Parser.cpp`
- `src/Parser.h`

Responsibilities:

- accept TCP connections
- receive commands from clients
- parse and validate commands
- dispatch them to the storage logic
- send responses back to the client

### 2. Storage layer

Files:

- `include/helpers.h`
- `src/Parser.cpp`
- `server.cpp`

Responsibilities:

- maintain one in-memory head block per metric
- append recent writes to `wal.bin`
- flush full head blocks to chunk files
- read from both memory and disk
- execute `GET`, `AGG`, `STATS`, and `FLUSH`

### 3. Compression layer

Files:

- `src/compression.h`

Responsibilities:

- bit writer and bit reader
- timestamp compression
- value compression
- chunk file write/read
- CRC verification

## Data Model

Each metric is identified by a flat string name such as:

- `cpu`
- `cpu_usage`
- `temperature`

Each metric keeps a `HeadBlock` in memory with:

- `timestamps: vector<uint64_t>`
- `values: vector<double>`
- `capacity`

Points are appended in timestamp order. When a head block reaches capacity, it
is flushed to disk as one compressed chunk file.

## Storage Format

Each chunk is stored under:

```text
./data/<metric_name>/<first_timestamp>.chunk
```

The current implementation writes a temporary file first and then renames it.

Chunk format:

```text
magic             4 bytes   "TSDB"
version           4 bytes   uint32
point_count       4 bytes   uint32
first_timestamp   8 bytes   uint64
last_timestamp    8 bytes   uint64
ts_len            4 bytes   uint32
val_len           4 bytes   uint32
ts_bitstream      variable
val_bitstream     variable
crc64             8 bytes
```

## Compression

### Timestamp compression

Timestamps use delta-of-delta encoding:

1. store the first timestamp in 64 bits
2. store the first delta in 14 bits
3. encode later delta-of-delta values with variable-length prefixes

This works well when samples arrive at regular or nearly regular intervals.

### Value compression

Values use Gorilla-style XOR encoding:

1. store the first double as raw 64-bit IEEE-754 bits
2. XOR each later value with the previous value
3. encode zero-runs and meaningful bits efficiently

This works well when consecutive values change only a little.

## Build

Requirements:

- `g++` with C++20 support
- POSIX sockets
- `make`
- Linux or another Unix-like environment

Build everything:

```bash
make
```

Build individual targets:

```bash
make server
make client
make test
make benchmark
```

Clean build artifacts:

```bash
make clean
```

Remove build artifacts and all generated data:

```bash
make distclean
```

## Running the Server

Start the server:

```bash
./server
```

Default configuration is defined in:

- `include/config.h`
- `server_config.h`

Current defaults:

- port: `8080`
- max worker threads: `10`
- head block capacity: `100000`
- server IP for client: `127.0.0.1`

## Running the Client

Start the client in another terminal:

```bash
./client
```

You can then enter commands interactively.

## Command Reference

### PUT

Insert one point into a metric.

```text
PUT <metric_name> <timestamp> <value>
```

Example:

```text
PUT cpu 1000 45.2
```

Rules:

- timestamps for each metric must be non-decreasing
- points are first appended to `wal.bin`
- then appended to the metric's in-memory head block

### GET

Return points in a time range.

```text
GET <metric_name> <from_timestamp> <to_timestamp>
```

Example:

```text
GET cpu 1000 2000
```

### AGG

Aggregate points into buckets.

```text
AGG <metric_name> <from_timestamp> <to_timestamp> <bucket_seconds> <func>
```

Supported functions:

- `avg`
- `min`
- `max`
- `sum`
- `count`

Example:

```text
AGG cpu 1000 2000 60 avg
```

### STATS

Show metadata for one metric.

```text
STATS <metric_name>
```

Example:

```text
STATS cpu
```

### FLUSH

Force the metric's head block to be written to disk.

```text
FLUSH <metric_name>
```

Example:

```text
FLUSH cpu
```

### QUIT

Close the client connection.

```text
QUIT
```

## Example Session

```text
PUT cpu 1000 45.2
PUT cpu 1010 45.3
PUT cpu 1020 45.5
GET cpu 1000 1030
AGG cpu 1000 1030 20 avg
STATS cpu
FLUSH cpu
QUIT
```

## Testing

Build the test target:

```bash
make test
```

Run the tests:

```bash
./test
```

The test suite includes:

- bit writer / bit reader round-trip checks
- timestamp encode / decode checks
- value encode / decode checks
- chunk write / read round-trip checks
- some end-to-end network behavior checks

Reference log:

- `tests/testing.log`

## Benchmark

Build the benchmark:

```bash
make benchmark
```

Make sure the server is already running, then run:

```bash
./benchmark
```

Or:

```bash
make run-benchmark
```

The benchmark:

- generates 500,000 synthetic points
- spreads them across 10 metrics
- uses regular timestamps
- uses slowly drifting values
- inserts all points through the server
- flushes all metrics
- compares compressed disk usage against a naive 16-byte-per-point format

Reference log:

- `benchmarks/benchmark.log`

### Recorded benchmark result

From the current benchmark log:

- total points: `500000`
- metrics: `10`
- insert time: `417.593 sec`
- throughput: `1197 pts/s`
- flush time: `0.823 sec`
- naive size: `8000000 bytes`
- compressed size: `893240 bytes`
- compression ratio: `8.96x`

Interpretation:

- the compression ratio is strong and meets the project target of at least 8x
- ingestion throughput is currently much lower than the manual's ideal target

## Recovery and Persistence

### WAL behavior

Each metric stores unflushed writes in:

```text
./data/<metric_name>/wal.bin
```

On server startup:

- metric directories are scanned
- existing `wal.bin` files are replayed into memory

After a successful flush:

- the head block is cleared
- the corresponding `wal.bin` file is removed

### Startup metric discovery

The server registers metrics by scanning the `./data/` directory at startup.

## Current Limitations

This README reflects the current implementation, not an idealized target.
Some known limitations or differences from the manual/spec include:

- the server uses a length-prefixed socket protocol rather than plain
  line-by-line socket responses
- client/server response formatting is custom and not human-readable plain text
- benchmark throughput is currently much lower than the target
- parts of retention/downsampling behavior are experimental
- some edge cases in query and aggregation logic still need cleanup

## Configuration

### `include/config.h`

Contains:

- `kPort`
- `kBacklog`
- `kMaxThreads`
- `memory_buffer`

### `server_config.h`

Contains:

- `kServerIp`

### `retention_config.h`

Contains retention constants for selected metric names.

## Suggested Workflow

1. Build the project with `make`
2. Start the server with `./server`
3. Start the client with `./client`
4. Insert data using `PUT`
5. Retrieve it using `GET`
6. Test bucketed summaries with `AGG`
7. Force persistence using `FLUSH`
8. Run `./test`
9. Run `./benchmark`

## Related Files

- Project design notes: [design.txt](design.txt)
- Assignment manual: [manual.pdf](manual.pdf)
- Compression code: [src/compression.h](src/compression.h)
- Parser and command handling: [src/Parser.cpp](src/Parser.cpp)
- Server entry point: [server.cpp](server.cpp)

## License

This project was created as a course project. Add a license here if you want
to distribute it publicly.
