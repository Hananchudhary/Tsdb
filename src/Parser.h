#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string>
#include <variant>
#include <vector>
#include "../include/helpers.h"

using namespace std;

class LineProtocolParser {

    ParseResult parse_single_command(const string& line);

    static bool is_valid_metric_name(const string& metric_name);
    static vector<string> tokenize(const string& line);
    static vector<string> split_queries(const string& line);
    static bool parse_int64(const string& token, uint64_t& value);
    static bool parse_int(const string& token, int& value);
    static bool parse_double(const string& token, double& value);
    static string trim_carriage_return(string& line);
    static string trim_whitespace(const string& line);

    string buffer_;

    public:
    vector<ParseResult> parse_line(string& line);

    bool has_pending_data() const;
    size_t pending_bytes() const;
    LineProtocolParser() = default;

};
