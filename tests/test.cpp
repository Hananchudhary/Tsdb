#include <arpa/inet.h>
#include <cerrno>
#include <cstring>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>
#include <chrono>
#include <filesystem>
#include <thread>
#include <unistd.h>
#include <bits/stdc++.h>
#include <sys/socket.h>

#include "../include/helpers.h"
#include "../server_config.h"
#include"../src/compression.h"

using namespace std;
namespace fs = std::filesystem;

ssize_t recv_all(int socket_fd, void* data, size_t length) {
    char* buffer = static_cast<char*>(data);
    size_t total_received = 0;

    while (total_received < length) {
        ssize_t received = recv(socket_fd, buffer + total_received, length - total_received, 0);

        if (received < 0) {
            if (errno == EINTR) {
                continue;
            }
            return -1;
        }

        if (received == 0) {
            return 0;
        }

        total_received += received;
    }

    return static_cast<ssize_t>(total_received);
}

bool send_all(int socket_fd, const void* data, size_t length) {
    const char* buffer = static_cast<const char*>(data);
    size_t total_sent = 0;

    while (total_sent < length) {
        ssize_t sent = send(socket_fd, buffer + total_sent, length - total_sent, 0);

        if (sent < 0) {
            if (errno == EINTR) {
                continue;
            }
            return false;
        }

        if (sent == 0) {
            return false;
        }

        total_sent += sent;
    }

    return true;
}

bool send_with_size(int socket_fd, const void* data, uint32_t length) {
    uint32_t net_length = htonl(length);

    if (!send_all(socket_fd, &net_length, sizeof(net_length))) {
        return false;
    }

    return send_all(socket_fd, data, length);
}

bool recv_with_size(int socket_fd, string& out) {
    uint32_t net_length = 0;

    if (recv_all(socket_fd, &net_length, sizeof(net_length)) <= 0) {
        return false;
    }

    uint32_t length = ntohl(net_length);
    out.resize(length);

    return recv_all(socket_fd, out.data(), length) > 0;
}

uint64_t parse_point_count_from_get_response(const string& response) {
    if (response.empty() ||
        response[0] != static_cast<char>(MessageType::GET) ||
        response.size() < 1 + sizeof(uint64_t)) {
        return numeric_limits<uint64_t>::max();
    }

    uint64_t count = 0;
    memcpy(&count, response.data() + 1, sizeof(uint64_t));
    return count;
}

void show_pair_result(string& response, int& i){
    uint64_t size;
    memcpy(&size, response.data() + i, sizeof(uint64_t));
    i += sizeof(uint64_t);
    for(uint64_t j = 0;j < size && i < response.size();j++){
        uint64_t value;
        memcpy(&value, response.data() + i, sizeof(uint64_t));
        i += sizeof(uint64_t);
        double val;
        memcpy(&val, response.data() + i, sizeof(double));
        i += sizeof(double);
        cout << "(" << value << " , " << val << ")\n";
    }
}
string deserialize(const string& str, int& i, const char del){
    size_t pos = str.find(del, i);

    if(pos == string::npos){
        cout << "Error in deserialization.\n";
        return "";
    }

    string result = str.substr(i, pos - i + 1);
    i = pos + 1;
    return result;
}
void show_stats_result(const string& str, int& i){
    cout << deserialize(str, i, '=');
    cout << deserialize(str, i, ' ') << endl;
    cout << deserialize(str, i, '=');
    cout << stoull(deserialize(str, i, ' ')) << endl;
    cout << deserialize(str, i, '=');
    cout << stoull(deserialize(str, i, ' ')) << endl;
    cout << deserialize(str, i, '=');
    cout << stoull(deserialize(str, i, ' ')) << endl;
    cout << deserialize(str, i, '=');
    cout << stoull(deserialize(str, i, ' ')) << endl;
    cout << deserialize(str, i, '=');
    cout << stoull(deserialize(str, i, ' ')) << endl;
    cout << deserialize(str, i, '=');
    cout << stoull(deserialize(str, i, ' ')) << endl;
}

void show_result(string& response) {
    int i = 0;
    
    while (i < response.size()) {
        char type = response[i++];
        if (type == static_cast<char>(MessageType::PUT)) {
            cout << deserialize(response, i, '.') << endl;
        }
        else if (type == static_cast<char>(MessageType::GET)) { 
            show_pair_result(response, i);
        }
        else if (type == static_cast<char>(MessageType::AGG)) {
            show_pair_result(response, i);
        }
        else if (type == static_cast<char>(MessageType::FLUSH)) {
            cout << deserialize(response, i, '.') << endl;

        }
        else if (type == static_cast<char>(MessageType::QUIT)) {
            cout << deserialize(response, i, '.') << endl;
        }
        else if (type == static_cast<char>(MessageType::STATS)) {
            show_stats_result(response, i);
        }
        else if(type == static_cast<char>(MessageType::error)){
            cout << deserialize(response, i, '.') << endl;
        }
    }
}

int connect_to_server() {
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) {
        return -1;
    }

    sockaddr_in server{};
    server.sin_family = AF_INET;
    server.sin_port = htons(kPort);

    if (inet_pton(AF_INET, kServerIp, &server.sin_addr) <= 0) {
        close(fd);
        return -1;
    }

    if (connect(fd, reinterpret_cast<sockaddr*>(&server), sizeof(server)) < 0) {
        close(fd);
        return -1;
    }

    return fd;
}

void print_buffer(const vector<uint8_t>& buf, uint32_t cur) {
    for (auto b : buf) {
        for (int i = 7; i >= 0; i--) {
            cout << ((b >> i) & 1);
        }
        cout << " ";
    }
    for (int i = 7; i >= 0; i--) {
        cout << ((cur >> i) & 1);
    }
    cout << " ";
    cout << endl;
}

void test_1000_random_roundtrip() {
    cout << "=== 1000 VALUE BITSTREAM ROUNDTRIP TEST ===\n";

    BitWriter bw;

    vector<pair<uint64_t, int>> inputs;
    long long total_bits = 0;

    for (int i = 0; i < 1000; i++) {
        int bits = 1 + (rand() % 64);

        uint64_t value =
            ((uint64_t)rand() << 48) ^
            ((uint64_t)rand() << 32) ^
            ((uint64_t)rand() << 16) ^
            (uint64_t)rand();

        inputs.push_back({value, bits});

        bw_write(&bw, value, bits);
        total_bits += bits;
    }
    uint64_t val = 0;
    int bits = total_bits % 8;
    bw_write(&bw, val, bits);
    inputs.push_back({val, bits});
    total_bits += bits;

    if (bw.bits_filled != 0) {
        bw.buffer.push_back(bw.current_byte);
    }


    cout << "Total bits written: " << total_bits << "\n";
    cout << "Total bytes: " << bw.buffer.size() << "\n";

    BitReader br(bw.buffer);

    for (int i = 0; i < 1000; i++) {
        uint64_t expected = inputs[i].first;
        int bits = inputs[i].second;

        uint64_t got = br_read(&br, bits);

        uint64_t mask = (bits == 64) ? ~0ULL : ((1ULL << bits) - 1);

        if ((got & mask) != (expected & mask)) {
            cout << "❌ MISMATCH at index " << i << "\n";
            cout << "Expected: " << (expected & mask)
                 << " Got: " << (got & mask) << "\n";
            exit(1);
        }
    }

    cout << "✔ ALL 1000 VALUES MATCH\n";
}
void test_timestamp_random_monotonic() {
    cout << "=== Timestamp Encode/Decode Test (Random Monotonic) ===\n";

    BitWriter bw;
    vector<uint64_t> input;

    // 1. Generate random but non-decreasing timestamps
    uint64_t current = 1;

    for (int i = 0; i < 1000; i++) {
        input.push_back(current);

        // random increment (0 to 20)
        uint64_t step = rand() % 21;  
        current += step;
    }

    // 2. Encode
    timestamp_encode(&bw, input);

    // 3. Decode
    BitReader br(bw.buffer, 0);
    vector<uint64_t> output = timestamp_decode(&br, input.size());

    // 4. Verify size
    if (output.size() != input.size()) {
        cout << "❌ Size mismatch: "
             << output.size() << " vs " << input.size() << "\n";
        return;
    }

    // 5. Verify exact values
    for (size_t i = 0; i < input.size(); i++) {
        if (input[i] != output[i]) {
            cout << "❌ Mismatch at index " << i << "\n";
            cout << "Expected: " << input[i]
                 << " Got: " << output[i] << "\n";

            // print context (VERY useful)
            int start = max(0, (int)i - 5);
            int end = min((int)input.size(), (int)i + 5);

            cout << "\nContext around failure:\n";
            for (int j = start; j < end; j++) {
                cout << j << ": expected=" << input[j]
                     << " got=" << output[j] << endl;
            }

            return;
        }
    }

    cout << "✔ Test passed: perfect round-trip encoding/decoding\n";
}
void test_value_random_walk() {
    cout << "=== Value Encode/Decode Test (Random Walk 1000 doubles) ===\n";

    BitWriter bw;
    vector<double> input;

    // 1. Generate slowly drifting random walk
    double current = 1000.0;

    for (int i = 0; i < 1000; i++) {
        input.push_back(current);

        // small drift: -1.0 to +1.0
        double step = ((rand() % 2001) - 1000) / 1000.0;
        if(i % 100 == 0) {
            input.push_back(-1 * current);
        }
        current += step;
    }
    char* parsed = nullptr;
    input.push_back(strtod("NaN", &parsed));
    input.push_back(strtod("inf", &parsed));
    input.push_back(strtod("-inf", &parsed));

    // 2. Encode
    value_encode(&bw, input);

    // 3. Decode
    BitReader br(bw.buffer, 0);
    vector<double> output = value_decode(&br, input.size());

    // 4. Check size
    if (output.size() != input.size()) {
        cout << "❌ Size mismatch: "
             << output.size() << " vs " << input.size() << "\n";
        return;
    }

    // 5. Compare exact bit patterns so NaN/Inf and sign bits are validated too.
    for (size_t i = 0; i < input.size(); i++) {
        uint64_t expected_bits = 0;
        uint64_t got_bits = 0;
        memcpy(&expected_bits, &input[i], sizeof(double));
        memcpy(&got_bits, &output[i], sizeof(double));
        if (expected_bits != got_bits) {
            cout << "❌ Mismatch at index " << i << "\n";
            cout << "Expected: " << input[i]
                 << " Got: " << output[i] << "\n";
            cout << "Expected bits: " << expected_bits
                 << " Got bits: " << got_bits << "\n";

            // context window for debugging
            int start = max(0, (int)i - 5);
            int end = min((int)input.size(), (int)i + 5);

            cout << "\nContext:\n";
            for (int j = start; j < end; j++) {
                cout << j << ": expected=" << input[j]
                     << " got=" << output[j] << "\n";
            }

            return;
        }
    }

    cout << "✔ Test passed: random walk encoding/decoding successful\n";
}
pair<vector<uint64_t>, vector<double>> generate_test_data(int N) {
    vector<uint64_t> timestamps;
    vector<double> values;

    std::mt19937_64 rng(42);

    uint64_t t = 1000000;

    for (int i = 0; i < N; i++) {
        t += (rng() % 10 + 1);  // monotonic increase
        timestamps.push_back(t);

        double drift = (rng() % 1000) / 1000.0;
        values.push_back(1000.0 + sin(i * 0.01) + drift);
    }

    return {timestamps, values};
}
void test_chunk_roundtrip() {

    cout << "=== CHUNK ROUNDTRIP TEST ===\n";

    string metric = "test_metric";
    auto [input_ts, input_val] = generate_test_data(120);
    cout << "Data populated\n";

    HeadBlock hb;
    hb.timestamps = input_ts;
    hb.values = input_val;

    string err = chunk_file_writer(&hb, metric);
    if (!err.empty()) {
        cout << "WRITE FAILED: " << err << "\n";
        return;
    }
    cout << "Data written\n";
    auto [out_ts, out_val] = chunk_file_reader(metric);
    cout << "Data Read\n";
    // 4. Validate size
    if (out_ts.size() != input_ts.size() ||
        out_val.size() != input_val.size()) {
        cout << "❌ SIZE MISMATCH\n";
        cout << "ts: " << out_ts.size() << " vs " << input_ts.size() << "\n";
        cout << "val: " << out_val.size() << " vs " << input_val.size() << "\n";
        return;
    }

    // 5. Validate content
    for (size_t i = 0; i < input_ts.size(); i++) {

        if (input_ts[i] != out_ts[i]) {
            cout << "❌ TIMESTAMP MISMATCH at " << i << "\n";
            cout << "expected: " << input_ts[i]
                 << " got: " << out_ts[i] << "\n";
            return;
        }

        if (input_val[i] != out_val[i]) {
            cout << "❌ VALUE MISMATCH at " << i << "\n";
            cout << "expected: " << input_val[i]
                 << " got: " << out_val[i] << "\n";
            return;
        }
    }

    cout << "✔ CHUNK ROUNDTRIP SUCCESS\n";
}
void test_cleaner_thread() {
    cout << "=== CLEANER THREAD TEST ===\n";

    const string metric = "temperature";
    const string metric_dir = "./data/" + metric;

    if (fs::exists(metric_dir)) {
        fs::remove_all(metric_dir);
    }

    int fd = connect_to_server();
    if (fd < 0) {
        cout << "⚠ Cleaner test skipped: server is not running\n";
        return;
    }

    const uint64_t now = static_cast<uint64_t>(time(nullptr));
    const uint64_t from_ts = now - 30;
    const uint64_t to_ts = now - 21;

    stringstream ss;
    for (uint64_t ts = from_ts; ts <= to_ts; ++ts) {
        if (ts != from_ts) ss << " && ";
        ss << "PUT " << metric << " " << ts << " " << (36.5 + (ts - from_ts) * 0.1);
    }
    ss << " && FLUSH " << metric;

    const string write_request = ss.str();
    if (!send_with_size(fd, write_request.data(),
                        static_cast<uint32_t>(write_request.size()))) {
        cout << "❌ Cleaner test failed: could not send expired test data\n";
        close(fd);
        return;
    }

    string response;
    if (!recv_with_size(fd, response)) {
        cout << "❌ Cleaner test failed: no response for write request\n";
        close(fd);
        return;
    }

    vector<string> chunks = get_chunk_files(metric_dir);
    if (chunks.empty()) {
        cout << "❌ Cleaner test failed: flush did not create any chunk files\n";
        close(fd);
        return;
    }

    cout << "Created " << chunks.size()
         << " expired chunk(s); waiting for cleaner cycle...\n";

    this_thread::sleep_for(chrono::seconds(70));

    vector<string> remaining_chunks = get_chunk_files(metric_dir);
    if (!remaining_chunks.empty()) {
        cout << "❌ Cleaner test failed: expired chunks still exist after cleaner cycle\n";
        close(fd);
        return;
    }

    const string get_request =
        "AGG " + metric + " " + to_string(from_ts) + " " + to_string(to_ts) + " 80 avg";
    if (!send_with_size(fd, get_request.data(),
                        static_cast<uint32_t>(get_request.size()))) {
        cout << "❌ Cleaner test failed: could not send verification GET\n";
        close(fd);
        return;
    }

    string get_response;
    if (!recv_with_size(fd, get_response)) {
        cout << "❌ Cleaner test failed: no response for verification GET\n";
        close(fd);
        return;
    }

    show_result(get_response);

    cout << "✔ Cleaner removed expired chunks and AGG returned no points\n";
    close(fd);
}
void test_zstd_roundtrip()
{
    namespace fs = std::filesystem;

    const string dir = "../data/cpu_usage";
    const string chunk_path = dir + "/1000.chunk";
    const string coarser_path = dir + "/coarser/1000.chunk";

    fs::create_directories(dir);
    fs::create_directories(dir + "/coarser");

    // generate random test data
    vector<uint8_t> original(1024 * 1024);

    std::mt19937 rng(12345);
    std::uniform_int_distribution<int> dist(0, 255);

    for (auto& b : original)
        b = static_cast<uint8_t>(dist(rng));

    // write original chunk
    {
        ofstream out(chunk_path, ios::binary);

        if (!out)
            throw runtime_error("failed to create input chunk");

        out.write(
            reinterpret_cast<const char*>(original.data()),
            original.size()
        );
    }
    cout << "Compressing\n";
    // compress
    zstd_compress(chunk_path);
    cout << "Decompressing\n";
    // decompress
    vector<uint8_t> decompressed = zstd_decompress(coarser_path);
    cout << "Done\n";
    // validate
    assert(original.size() == decompressed.size());

    bool equal = std::equal(
        original.begin(),
        original.end(),
        decompressed.begin()
    );

    assert(equal);

    cout << "zstd roundtrip test passed\n";
}
int main(){
    // test_1000_random_roundtrip();
    // test_value_random_walk();
    // test_chunk_roundtrip();
    test_cleaner_thread();
    // test_zstd_roundtrip();
    return 0;
}
int main1() {
    const vector<string> tests = {
        "PUT cpu_usage 1000 45.2 && PUT cpu_usage 1001 45.3 && PUT temperature 2000 36.6 && GET cpu_usage 1000 2000 && AGG cpu_usage 1000 2000 10 min && AGG cpu_usage 1000 2000 10 max && AGG cpu_usage 1000 2000 10 sum && AGG cpu_usage 1000 2000 10 count && FLUSH cpu_usage && STATS cpu_usage && QUIT",
        "PUT cpu 1 10.0 && PUT cpu 2 20.0 && PUT cpu 3 30.0 && GET cpu 1 3",
        "PUT   cpu_usage    1000    45.2 && PUT cpu_usage 1001 45.3 && GET     cpu_usage   1000    2000 && AGG   cpu_usage   1000   2000   10   avg",
        "POT cpu_usage 1000 45.2 && GEET cpu_usage 1000 2000 && AGGG cpu_usage 1000 2000 10 avg && STAT cpu_usage && FLUS cpu_usage",
        "PUT cpu_usage 1000 && PUT cpu_usage && GET cpu_usage 1000 && GET cpu_usage && AGG cpu_usage 1000 2000 10 && AGG cpu_usage 1000 2000 && STATS && FLUSH",
        "PUT cpu_usage 1000 && PUT cpu_usage && GET cpu_usage 1000 && GET cpu_usage && AGG cpu_usage 1000 2000 10 && AGG cpu_usage 1000 2000 && STATS && FLUSH",
        "PUT cpu_usage 1000 0 && PUT cpu_usage 1000 -45.2 && PUT cpu_usage 1000 3.402823e38 && PUT cpu_usage 1000 -3.402823e38 && PUT cpu_usage 1000 NaN && PUT cpu_usage 1000 inf && PUT cpu_usage 1000 -inf && PUT cpu_usage 1000 abc",
        "GET cpu_usage 2000 1000 && GET cpu_usage 1000 1000 && GET cpu_usage 0 0 && GET cpu_usage -100 1000",
        "AGG cpu_usage 1000 2000 10 avg && AGG cpu_usage 1000 2000 10 AVG && AGG cpu_usage 1000 2000 10 median && AGG cpu_usage 1000 2000 10 mode && AGG cpu_usage 1000 2000 0 avg && AGG cpu_usage 1000 2000 -10 avg",
        "HELLO WORLD && PUT && GET && AGG && RANDOM TEXT HERE && 12345 && !@#$%^&* && PUT cpu_usage 1000 45.2 extra_token",
        "PUT cpu 100 10 && PUT temp 100 36.5 && PUT cpu 101 11 && PUT temp 101 36.6 && GET cpu 100 102 && GET temp 100 102 && STATS cpu && STATS temp",
        "PUT cpu 100 10 && PUT cpu 101 11 && PUT cpu 100 9 && PUT cpu 102 12",
        "PUT cpu 100 10 && PUT cpu 100 20 && PUT cpu 100 30 && GET cpu 100 101",
        "GET unknown 0 1000 && STATS unknown",
        "PUT cpu 100 abc && PUT cpu 100 NaN && PUT cpu 100 inf && PUT cpu 100 -inf",
        "PUT cpu 100 10 && PUT cpu 200 20 && PUT cpu 150 15 && GET cpu 0 300",
        "PUT cpu 100 10 && PUT cpu 101 11 && flush cpu_usage"
    };
    for (size_t i = 0; i < tests.size(); ++i) {
        int fd = connect_to_server();
        if (fd < 0) {
            cerr << "Test " << (i + 1) << ": connect failed\n";
            return 1;
        }

        const string& request = tests[i];
        cout << "========== Test " << (i + 1) << " ==========\n";
        // cout << "Input: " << request << "\n\n";

        if (!send_with_size(fd, request.data(), static_cast<uint32_t>(request.size()))) {
            cerr << "Test " << (i + 1) << ": send failed\n";
            close(fd);
            return 1;
        }

        string response;
        if (!recv_with_size(fd, response)) {
            cerr << "Test " << (i + 1) << ": recv failed\n";
            close(fd);
            return 1;
        }

        cout << "Server:\n";
        show_result(response);
        cout << '\n';

        close(fd);
    }

    return 0;
}
