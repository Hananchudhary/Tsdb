#include "Parser.h"
#include <fstream>
#include <filesystem>
#include <charconv>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <string>
#include<cstring>
using namespace std;

ParseResult make_error(string message, string_view line) {
    ParseResult result;
    result.error = ParseError{move(message), string(line)};
    return result;
}

bool is_space(char ch) {
    return ch == ' ' || ch == '\t' || ch == '\r';
}


bool LineProtocolParser::has_pending_data() const {
    cout << buffer_ << endl;
    return !buffer_.empty();
}

size_t LineProtocolParser::pending_bytes() const {
    return buffer_.size();
}
bool is_correct_func(string_view comm) {
    vector<string_view> funcs{"avg", "min", "max", "sum", "count"};

    for (string_view f : funcs) {
        if (f.size() != comm.size()) continue;

        bool match = true;
        for (size_t i = 0; i < f.size(); i++) {
            if (tolower(f[i]) != tolower(comm[i])) {
                match = false;
                break;
            }
        }

        if (match) return true;
    }

    return false;
}
void convert_lower(string& str){
    for(int i = 0;str[i]!='\0';i++){
        if(str[i] >='A' && str[i]<='Z'){
            str[i] = tolower(str[i]);
        }
    }
}
bool isEqual(const char* command, string_view comm) {
    size_t len = strlen(command);
    if (len != comm.size()) return false;

    for (size_t i = 0; i < len; i++) {
        if (tolower(command[i]) != tolower(comm[i])) {
            return false;
        }
    }
    return true;
}
vector<ParseResult> LineProtocolParser::parse_line(string_view raw_line) {
    if (!raw_line.empty()) {
        buffer_.append(raw_line.data(), raw_line.size());
    }
    string line = trim_carriage_return(raw_line);
    vector<string_view> queries = split_queries(line);
    vector<ParseResult> results;

    if (queries.empty()) {
        results.push_back(make_error("empty command", line));
        return results;
    }

    results.reserve(queries.size());
    for (string_view query : queries) {
        const string_view trimmed_query = trim_whitespace(query);
        if (trimmed_query.empty()) {
            results.push_back(make_error("empty command between && separators", line));
            continue;
        }
        results.push_back(parse_single_command(trimmed_query));
    }
    buffer_.clear();
    return results;
}

ParseResult LineProtocolParser::parse_single_command(string_view raw_line) {
    string line(raw_line);
    vector<string_view> tokens = tokenize(line);

    if (tokens.empty()) {
        return make_error("empty command", line);
    }

    const string_view command = tokens[0];

    if (isEqual("PUT", command)) {
        if (tokens.size() != 4) {
            return make_error("PUT expects 3 arguments: PUT <metric_name> <timestamp> <value>", line);
        }

        if (!is_valid_metric_name(tokens[1])) {
            return make_error("invalid metric name", line);
        }

        int64_t timestamp = 0;
        double value = 0.0;
        if (!parse_int64(tokens[2], timestamp)) {
            return make_error("invalid timestamp format", line);
        }
        if (!parse_double(tokens[3], value)) {
            return make_error("invalid value format", line);
        }

        const string metric_name(tokens[1]);

        ParseResult result;
        result.command = Command{
            CommandType::Put,
            PutCommand{metric_name, timestamp, value}
        };
        return result;
    }

    if (isEqual("GET", command)) {
        if (tokens.size() != 4) {
            return make_error("GET expects 3 arguments: GET <metric_name> <from_timestamp> <to_timestamp>", line);
        }

        if (!is_valid_metric_name(tokens[1])) {
            return make_error("invalid metric name", line);
        }

        int64_t from_timestamp = 0;
        int64_t to_timestamp = 0;
        if (!parse_int64(tokens[2], from_timestamp) || !parse_int64(tokens[3], to_timestamp)) {
            return make_error("invalid timestamp format", line);
        }
        if (from_timestamp > to_timestamp) {
            return make_error("GET requires from_timestamp <= to_timestamp", line);
        }

        ParseResult result;
        result.command = Command{
            CommandType::Get,
            GetCommand{string(tokens[1]), from_timestamp, to_timestamp}
        };
        return result;
    }

    if (isEqual("AGG", command)) {
        if (tokens.size() != 6) {
            return make_error("AGG expects 5 arguments: AGG <metric_name> <from> <to> <bucket_seconds> <func>", line);
        }

        if (!is_valid_metric_name(tokens[1])) {
            return make_error("invalid metric name", line);
        }

        int64_t from_timestamp = 0;
        int64_t to_timestamp = 0;
        int64_t bucket_seconds = 0;
        if (!parse_int64(tokens[2], from_timestamp) || !parse_int64(tokens[3], to_timestamp)) {
            return make_error("invalid timestamp format", line);
        }
        if (!parse_int64(tokens[4], bucket_seconds)) {
            return make_error("invalid bucket_seconds format", line);
        }
        if (from_timestamp > to_timestamp) {
            return make_error("AGG requires from <= to", line);
        }
        if (bucket_seconds <= 0) {
            return make_error("bucket_seconds must be positive", line);
        }
        if(!is_correct_func(tokens[5])){
            return make_error("not a valid function", line);
        }
        ParseResult result;
        result.command = Command{
            CommandType::Agg,
            AggCommand{
                string(tokens[1]),
                from_timestamp,
                to_timestamp,
                bucket_seconds,
                string(tokens[5])
            }
        };
        return result;
    }

    if (isEqual("STATS", command)) {
        if (tokens.size() != 2) {
            return make_error("STATS expects 1 argument: STATS <metric_name>", line);
        }

        if (!is_valid_metric_name(tokens[1])) {
            return make_error("invalid metric name", line);
        }

        ParseResult result;
        result.command = Command{
            CommandType::Stats,
            StatsCommand{string(tokens[1])}
        };
        return result;
    }

    if (isEqual("FLUSH", command)) {
        if (tokens.size() != 2) {
            return make_error("FLUSH expects 1 argument: FLUSH <metric_name>", line);
        }

        if (!is_valid_metric_name(tokens[1])) {
            return make_error("invalid metric name", line);
        }

        ParseResult result;
        result.command = Command{
            CommandType::Flush,
            FlushCommand{string(tokens[1])}
        };
        return result;
    }

    if (isEqual("QUIT", command)) {
        if (tokens.size() != 1) {
            return make_error("QUIT expects no arguments", line);
        }

        ParseResult result;
        result.command = Command{
            CommandType::Quit,
            QuitCommand{}
        };
        return result;
    }

    return make_error("unknown command type", line);
}

bool LineProtocolParser::is_valid_metric_name(string_view metric_name) {
    if (metric_name.empty()) {
        return false;
    }

    for (char ch : metric_name) {
        const bool is_alpha = (ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z');
        const bool is_digit = ch >= '0' && ch <= '9';
        const bool is_underscore = ch == '_' || ch == '-' || ch == '.';
        if (!is_alpha && !is_digit && !is_underscore) {
            return false;
        }
    }

    return true;
}

vector<string_view> LineProtocolParser::tokenize(string_view line) {
    vector<string_view> tokens;
    size_t i = 0;

    while (i < line.size()) {
        while (i < line.size() && is_space(line[i])) {
            ++i;
        }

        if (i >= line.size()) {
            break;
        }

        size_t start = i;
        while (i < line.size() && !is_space(line[i])) {
            ++i;
        }
        tokens.push_back(line.substr(start, i - start));
    }

    return tokens;
}

vector<string_view> LineProtocolParser::split_queries(string_view line) {
    vector<string_view> queries;
    size_t start = 0;

    while (start <= line.size()) {
        size_t separator = line.find("&&", start);
        if (separator == string_view::npos) {
            queries.push_back(line.substr(start));
            break;
        }

        queries.push_back(line.substr(start, separator - start));
        start = separator + 2;
    }

    return queries;
}

bool LineProtocolParser::parse_int64(string_view token, int64_t& value) {
    if (token.empty()) {
        return false;
    }

    const char* begin = token.data();
    const char* end = token.data() + token.size();
    auto [ptr, ec] = from_chars(begin, end, value);
    return ec == errc() && ptr == end;
}

bool LineProtocolParser::parse_double(string_view token, double& value) {
    if (token.empty()) {
        return false;
    }

    string owned(token);
    char* parse_end = nullptr;
    errno = 0;
    double parsed = strtod(owned.c_str(), &parse_end);
    if (errno == ERANGE) {
        return false;
    }
    if (parse_end != owned.c_str() + owned.size()) {
        return false;
    }

    value = parsed;
    return true;
}

string LineProtocolParser::trim_carriage_return(string_view line) {
    if (!line.empty() && line.back() == '\r') {
        line.remove_suffix(1);
    }
    return string(line);
}

string_view LineProtocolParser::trim_whitespace(string_view line) {
    size_t start = 0;
    while (start < line.size() && is_space(line[start])) {
        ++start;
    }

    size_t end = line.size();
    while (end > start && is_space(line[end - 1])) {
        --end;
    }

    return line.substr(start, end - start);
}
bool PutCommand::handleRequest(HeadBlock& hb) const {
    if (hb.timestamps.size() == hb.capacity) return false;
    if (!hb.timestamps.empty() && hb.timestamps.back() > this->timestamp) return false;

    hb.timestamps.push_back(this->timestamp);
    hb.values.push_back(this->value);

    string dirPath = "./data/" + this->metric_name;
    string filePath = dirPath + "/wal.log";

    filesystem::create_directories(dirPath);

    bool fileExists = filesystem::exists(filePath);

    ofstream file;

    if (!fileExists) {
        file.open(filePath, ios::out);
        file << this->timestamp << "," << this->value;
    } else {
        file.open(filePath, ios::app);
        file << "\n" << this->timestamp << "," << this->value;
    }

    file.close();
    return true;
}
pair<vector<int64_t>, vector<double>> GetCommand::handleRequest(HeadBlock& hb) const{
    int size = hb.timestamps.size(), i = 9;
    pair<vector<int64_t>, vector<double>> res;
    for(int i = 0;i<size;i++){
        if(hb.timestamps[i] > this->to_timestamp) break;
        if(hb.timestamps[i] >= from_timestamp){
            res.first.push_back(hb.timestamps[i]);
            res.second.push_back(hb.values[i]);
        }
    }
    return res;
}
double mini(const vector<double>& arr){
    double min = INT_FAST32_MAX;
    for(const double a : arr){
        if(a < min) min = a;
    }
    return min;
}
double mixi(const vector<double>& arr){
    double max = INT_FAST32_MIN;
    for(const double a : arr){
        if(a > max) max = a;
    }
    return max;
}
double sum(const vector<double>& arr){
    double sum = 0;
    for(const double a : arr){
        sum = sum + a;
    }
    return sum;
}
pair<vector<int64_t>, vector<double>> AggCommand::handleRequest(HeadBlock& hb) const{
    pair<vector<int64_t>, vector<double>> res;
    int size = hb.timestamps.size(), i = 0;
    for(int i = 0;i<size;i++){
        if(hb.timestamps[i] > this->to_timestamp) break;
        if(hb.timestamps[i] >= this->from_timestamp){
            int last = hb.timestamps[i] + bucket_seconds;
            pair<vector<int64_t>, vector<double>> res1;
            while(hb.timestamps[i] > last && i < size){
                res1.first.push_back(hb.timestamps[i]);
                res1.second.push_back(hb.values[i]);
                i++;
            }
            if(res1.first.empty()) continue;
            if(this->func == "sum"){
                res.first.push_back(res1.first[0]);
                res.second.push_back(sum(res1.second));
            }
            if(this->func == "avg"){
                res.first.push_back(res1.first[0]);
                res.second.push_back((sum(res1.second) / res.first.size()));
            }
            if(this->func == "min"){
                res.first.push_back(res1.first[0]);
                res.second.push_back(mini(res1.second));
            }
            if(this->func == "max"){
                res.first.push_back(res1.first[0]);
                res.second.push_back(mixi(res1.second));
            }
            if(this->func == "count"){
                res.first.push_back(res1.first[0]);
                res.second.push_back(res1.first.size());
            }
        }
    }
    return res;
}
StatsResult StatsCommand::handleRequest(HeadBlock& hb) const{
    StatsResult res;
    res.first_timestamp = hb.timestamps[0];
    res.last_timestamp = hb.timestamps[hb.timestamps.size() - 1];
    res.in_memory = hb.timestamps.size();
    res.metric_name = this->metric_name;
    res.total_points = res.on_disk + res.in_memory;
    return res;
}
bool FlushCommand::handleRequest(HeadBlock& hb) const{
    hb.timestamps.clear();
    hb.values.clear();
    return true;
}