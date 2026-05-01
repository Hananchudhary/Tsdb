#pragma once
#include<iostream>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include<vector>
#include<variant>
#include"./config.h"
using namespace std;
enum class CommandType {
    Put,
    Get,
    Agg,
    Stats,
    Flush,
    Quit
};
enum class MessageType : char {
    PUT = '1',
    GET = '2',
    AGG = '3',
    STATS = '4',
    FLUSH = '5',
    QUIT = '6',
    error = '7'
};
struct HeadBlock {
    vector<uint64_t> timestamps = vector<uint64_t>(0, memory_buffer);
    vector<double> values;
    int capacity = memory_buffer;
};

struct PutCommand {
    string metric_name;
    uint64_t timestamp = 0;
    double value = 0.0;
    int handleRequest(HeadBlock& hb) const;
};
struct StatsResult{
    string metric_name;
    int total_points = 0;
    int in_memory = 0;
    int on_disk = 0;
    int disk_chunks = 0;
    uint64_t first_timestamp = 0;
    uint64_t last_timestamp = 0;
};
struct GetCommand {
    string metric_name;
    uint64_t from_timestamp = 0;
    uint64_t to_timestamp = 0;
    pair<vector<uint64_t>, vector<double>> handleRequest(HeadBlock& hb) const;

};

struct AggCommand {
    string metric_name;
    uint64_t from_timestamp = 0;
    uint64_t to_timestamp = 0;
    uint64_t bucket_seconds = 0;
    string func;
    pair<vector<uint64_t>, vector<double>> handleRequest(HeadBlock& hb) const;
};

struct StatsCommand {
    string metric_name;
    StatsResult handleRequest(HeadBlock& hb) const;

};

struct FlushCommand {
    string metric_name;
    bool handleRequest(HeadBlock& hb) const;
};

struct QuitCommand {};

using CommandData = variant<
    PutCommand,
    GetCommand,
    AggCommand,
    StatsCommand,
    FlushCommand,
    QuitCommand>;

struct Command {
    CommandType type;
    CommandData data;
};

struct ParseError {
    string message;
    string line;
};

struct ParseResult {
    optional<Command> command;
    optional<ParseError> error;

    bool ok() const {
        return command.has_value();
    }
};
struct BitWriter {
    vector<uint8_t> buffer;
    uint8_t current_byte = 0;
    int bits_filled = 0;
};
struct BitReader {
    vector<uint8_t> buffer;
    int byte_index = 0;
    int bit_index = 0;
    BitReader(const vector<uint8_t>& b, int bi = 0): buffer{b}, byte_index{bi}{}
};