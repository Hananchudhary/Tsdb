#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <variant>
#include <vector>

using namespace std;
enum class CommandType {
    Put,
    Get,
    Agg,
    Stats,
    Flush,
    Quit
};

struct PutCommand {
    string metric_name;
    int64_t timestamp = 0;
    double value = 0.0;
};

struct GetCommand {
    string metric_name;
    int64_t from_timestamp = 0;
    int64_t to_timestamp = 0;
};

struct AggCommand {
    string metric_name;
    int64_t from_timestamp = 0;
    int64_t to_timestamp = 0;
    int64_t bucket_seconds = 0;
    string func;
};

struct StatsCommand {
    string metric_name;
};

struct FlushCommand {
    string metric_name;
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

class LineProtocolParser {

    ParseResult parse_single_command(string_view line);

    static bool is_valid_metric_name(string_view metric_name);
    static vector<string_view> tokenize(string_view line);
    static vector<string_view> split_queries(string_view line);
    static bool parse_int64(string_view token, int64_t& value);
    static bool parse_int(string_view token, int& value);
    static bool parse_double(string_view token, double& value);
    static string trim_carriage_return(string_view line);
    static string_view trim_whitespace(string_view line);

    string buffer_;
    unordered_map<string, int64_t> last_put_timestamp_;
    size_t max_buffered_bytes_;

    public:
    vector<ParseResult> parse_line(string_view line);

    bool has_pending_data() const;
    size_t pending_bytes() const;
    LineProtocolParser() = default;

};
