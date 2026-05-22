#include "Parser.h"
#include <fstream>
#include <filesystem>
#include <charconv>
#include <cstdlib>
#include <iostream>
#include<algorithm>
#include <limits>
#include <string>
#include<cstring>
#include"compression.h"
using namespace std;

ParseResult make_error(string message,const string& line) {
    ParseResult result;
    if(message[message.size()-1] != '.'){
        message += '.';
    }
    result.error = ParseError{move(message), line};
    return result;
}

bool is_space(char ch) {
    return ch == ' ' || ch == '\t' || ch == '\r';
}


bool LineProtocolParser::has_pending_data() const {
    return !buffer_.empty();
}

size_t LineProtocolParser::pending_bytes() const {
    return buffer_.size();
}
bool is_correct_func(const string& comm) {
    vector<string> funcs{
        "avg", "min", "max", "sum", "count"
    };

    for (const string& f : funcs) {
        if (f.size() != comm.size()) continue;
        if(comm == f) return true;
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
vector<ParseResult> LineProtocolParser::parse_line(string& raw_line) {
    if (!raw_line.empty()) {
        buffer_.append(raw_line.data(), raw_line.size());
    }
    string line = trim_carriage_return(raw_line);
    vector<string> queries = split_queries(line);
    vector<ParseResult> results;

    if (queries.empty()) {
        results.push_back(make_error("empty command.", line));
        return results;
    }

    results.reserve(queries.size());
    for (string query : queries) {
        const string trimmed_query = trim_whitespace(query);
        if (trimmed_query.empty()) {
            results.push_back(make_error("empty command between && separators.", line));
            continue;
        }
        results.push_back(parse_single_command(trimmed_query));
    }
    buffer_.clear();
    return results;
}

ParseResult LineProtocolParser::parse_single_command(const string& raw_line) {
    string line(raw_line);
    vector<string> tokens = tokenize(line);

    if (tokens.empty()) {
        return make_error("empty command.", line);
    }

    string command = tokens[0];
    convert_lower(command);
    if (command == "put") {
        if (tokens.size() != 4) {
            return make_error("PUT expects 3 arguments: PUT <metric_name> <timestamp> <value>.", line);
        }

        if (!is_valid_metric_name(tokens[1])) {
            return make_error("invalid metric name.", line);
        }

        uint64_t timestamp = 0;
        double value = 0.0;
        if (!parse_int64(tokens[2], timestamp)) {
            return make_error("invalid timestamp format.", line);
        }
        if (!parse_double(tokens[3], value)) {
            return make_error("invalid value format.", line);
        }

        const string metric_name(tokens[1]);

        ParseResult result;
        result.command = Command{
            CommandType::Put,
            PutCommand{metric_name, timestamp, value}
        };
        return result;
    }

    if (command == "get") {
        if (tokens.size() != 4) {
            return make_error("GET expects 3 arguments: GET <metric_name> <from_timestamp> <to_timestamp>.", line);
        }

        if (!is_valid_metric_name(tokens[1])) {
            return make_error("invalid metric name.", line);
        }

        uint64_t from_timestamp = 0;
        uint64_t to_timestamp = 0;
        if (!parse_int64(tokens[2], from_timestamp) || !parse_int64(tokens[3], to_timestamp)) {
            return make_error("invalid timestamp format.", line);
        }
        if (from_timestamp > to_timestamp) {
            return make_error("GET requires from_timestamp <= to_timestamp.", line);
        }

        ParseResult result;
        result.command = Command{
            CommandType::Get,
            GetCommand{string(tokens[1]), from_timestamp, to_timestamp}
        };
        return result;
    }

    if (command == "agg") {
        if (tokens.size() != 6) {
            return make_error("AGG expects 5 arguments: AGG <metric_name> <from> <to> <bucket_seconds> <func>.", line);
        }

        if (!is_valid_metric_name(tokens[1])) {
            return make_error("invalid metric name.", line);
        }

        uint64_t from_timestamp = 0;
        uint64_t to_timestamp = 0;
        uint64_t bucket_seconds = 0;
        convert_lower(tokens[5]);

        if (!parse_int64(tokens[2], from_timestamp) || !parse_int64(tokens[3], to_timestamp)) {
            return make_error("invalid timestamp format.", line);
        }
        if (!parse_int64(tokens[4], bucket_seconds)) {
            return make_error("invalid bucket_seconds format.", line);
        }
        if (from_timestamp > to_timestamp) {
            return make_error("AGG requires from <= to.", line);
        }
        if (bucket_seconds <= 0) {
            return make_error("bucket_seconds must be positive.", line);
        }
        if(!is_correct_func(tokens[5])){
            return make_error("not a valid function.", line);
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

    if (command == "stats") {
        if (tokens.size() != 2) {
            return make_error("STATS expects 1 argument: STATS <metric_name>.", line);
        }

        if (!is_valid_metric_name(tokens[1])) {
            return make_error("invalid metric name.", line);
        }

        ParseResult result;
        result.command = Command{
            CommandType::Stats,
            StatsCommand{string(tokens[1])}
        };
        return result;
    }

    if (command == "flush") {
        if (tokens.size() != 2) {
            return make_error("FLUSH expects 1 argument: FLUSH <metric_name>.", line);
        }

        if (!is_valid_metric_name(tokens[1])) {
            return make_error("invalid metric name.", line);
        }

        ParseResult result;
        result.command = Command{
            CommandType::Flush,
            FlushCommand{string(tokens[1])}
        };
        return result;
    }

    if (command == "quit") {
        if (tokens.size() != 1) {
            return make_error("QUIT expects no arguments.", line);
        }

        ParseResult result;
        result.command = Command{
            CommandType::Quit,
            QuitCommand{}
        };
        return result;
    }

    return make_error("unknown command type.", line);
}

bool LineProtocolParser::is_valid_metric_name(const string& metric_name) {
    if (metric_name.empty()) {
        return false;
    }

    for (char ch : metric_name) {
        const bool is_alpha = (ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z');
        const bool is_digit = ch >= '0' && ch <= '9';
        const bool is_underscore = ch == '_' || ch == '-';
        if (!is_alpha && !is_digit && !is_underscore) {
            return false;
        }
    }

    return true;
}

vector<string> LineProtocolParser::tokenize(const string& line) {
    vector<string> tokens;
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

vector<string> LineProtocolParser::split_queries(const string& line) {
    vector<string> queries;
    size_t start = 0;

    while (start <= line.size()) {
        size_t separator = line.find("&&", start);
        if (separator == string::npos) {
            queries.push_back(line.substr(start));
            break;
        }

        queries.push_back(line.substr(start, separator - start));
        start = separator + 2;
    }

    return queries;
}

bool LineProtocolParser::parse_int64(const string& token, uint64_t& value) {
    if (token.empty()) {
        return false;
    }

    const char* begin = token.data();
    const char* end = token.data() + token.size();
    auto [ptr, ec] = from_chars(begin, end, value);
    return ec == errc() && ptr == end;
}

bool LineProtocolParser::parse_double(const string& token, double& value) {
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

string LineProtocolParser::trim_carriage_return(string& line) {
    if (!line.empty() && line.back() == '\r') {
        line.pop_back();
    }
    return line;
}

string LineProtocolParser::trim_whitespace(const string& line) {
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
int PutCommand::handleRequest(HeadBlock& hb) const {
    if (hb.timestamps.size() == hb.capacity){
        FlushCommand fs;
        fs.metric_name = this->metric_name;
        if(!fs.handleRequest(hb)){
            return -1;
        }
    }
    string dirPath = "./data/" + this->metric_name;
    string filePath = dirPath + "/wal.bin";
    if(hb.timestamps.empty()){

        vector<string> files = get_chunk_files(dirPath);
        if(!files.empty()){
            string latest = files.back();
            if(get_last_timestamp_from_chunk(latest) > this->timestamp){
                return -2;
            }
        }
    }
    if (!hb.timestamps.empty() && hb.timestamps.back() > this->timestamp)
        return -2;

    filesystem::create_directories(dirPath);

    ofstream file(filePath, ios::binary | ios::app);
    if (!file.is_open()) return -3;

    file.write(reinterpret_cast<const char*>(&this->timestamp), sizeof(this->timestamp));
    file.write(reinterpret_cast<const char*>(&this->value), sizeof(this->value));

    file.close();

    hb.timestamps.push_back(this->timestamp);
    hb.values.push_back(this->value);

    return 0;
}
pair<vector<uint64_t>, vector<double>> GetCommand::handleRequest(HeadBlock& hb) const{
    pair<vector<uint64_t>, vector<double>> res;
    pair<vector<uint64_t>, vector<double>> chunks = chunk_file_reader(this->metric_name);
    chunks.first.insert(chunks.first.end(),hb.timestamps.begin(),hb.timestamps.end());
    chunks.second.insert(chunks.second.end(),hb.values.begin(),hb.values.end());
    uint64_t size = chunks.first.size();
    for(uint64_t i = 0;i<size;i++){
        if((chunks.first[i] >= from_timestamp) &&
            (chunks.first[i] <= this->to_timestamp)){
            res.first.push_back(chunks.first[i]);
            res.second.push_back(chunks.second[i]);
        }
    }
    return res;
}
pair<vector<uint64_t>, vector<double>> AggCommand::handleRequest(HeadBlock& hb) const{
    pair<vector<uint64_t>, vector<double>> res;
    
    pair<vector<uint64_t>, vector<double>> chunks = chunk_file_reader(this->metric_name);
    chunks.first.insert(chunks.first.end(),hb.timestamps.begin(),hb.timestamps.end());
    chunks.second.insert(chunks.second.end(),hb.values.begin(),hb.values.end());
    uint64_t size = chunks.first.size();
    
    for(uint64_t i = 0;i<size;){
        if((chunks.first[i] >= this->from_timestamp) &&
            (chunks.first[i] <= this->to_timestamp)){
            uint64_t last = chunks.first[i] + bucket_seconds;
            pair<vector<uint64_t>, vector<double>> res1;
            while(chunks.first[i] < last && i < size){
                res1.first.push_back(chunks.first[i]);
                res1.second.push_back(chunks.second[i]);
                i++;
            }
            if(res1.first.empty()) continue;
            res.first.push_back(res1.first[0]);
            if(this->func == "sum"){
                res.second.push_back(sum(res1.second));
            }
            else if(this->func == "avg"){
                res.second.push_back((sum(res1.second) / res1.first.size()));
            }
            else if(this->func == "min"){
                res.second.push_back(mini(res1.second));
            }
            else if(this->func == "max"){
                res.second.push_back(mixi(res1.second));
            }
            else if(this->func == "count"){
                res.second.push_back(res1.first.size());
            }
            else{
                res.first.pop_back();
            }
        }
    }
    return res;
}
StatsResult StatsCommand::handleRequest(HeadBlock& hb) const{
    StatsResult res;
    pair<vector<uint64_t>, vector<double>> chunks = chunk_file_reader(this->metric_name);
    res.on_disk = chunks.first.size();
    chunks.first.insert(chunks.first.end(),hb.timestamps.begin(),hb.timestamps.end());
    chunks.second.insert(chunks.second.end(),hb.values.begin(),hb.values.end());

    res.first_timestamp = chunks.first[0];
    res.last_timestamp = chunks.first[chunks.first.size() - 1];
    res.in_memory = hb.timestamps.size();
    res.metric_name = this->metric_name;
    string dirPath = "./data/" + this->metric_name;
    vector<string> files = get_chunk_files(dirPath);
    res.disk_chunks = files.size();
    res.total_points = res.on_disk + res.in_memory;
    return res;
}
bool FlushCommand::handleRequest(HeadBlock& hb) const{
    try{
        string res = chunk_file_writer(&hb, this->metric_name);
        if(res != ""){
            cout << res << endl;
            return false;
        }
        hb.timestamps.clear();
        hb.values.clear();
        string dirPath = "./data/" + this->metric_name;
        string filePath = dirPath + "/wal.bin";
        if(!filesystem::remove(filePath)){
            cout << "filecould not deleted.\n";
        }
        return true;
    }
    catch(const exception e){
        cout << e.what() << endl;
        return false;
    }
    return true;
}