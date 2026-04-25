#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <variant>
#include <vector>
#include "../include/helpers.h"

using namespace std;

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

    public:
    vector<ParseResult> parse_line(string_view line);

    bool has_pending_data() const;
    size_t pending_bytes() const;
    LineProtocolParser() = default;

};
