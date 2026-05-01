#include <arpa/inet.h>
#include <cerrno>
#include <cstring>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>
#include <unistd.h>
#include <bits/stdc++.h>
#include <sys/socket.h>

#include "./include/helpers.h"
#include "server_config.h"
#include"./src/compression.h"

using namespace std;

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

void show_pair_result(string& response, int& i){
    int64_t size;
    memcpy(&size, response.data() + i, sizeof(int64_t));
    i += sizeof(int64_t);
    for(int64_t j = 0;j < size && i < response.size();j++){
        int64_t value;
        memcpy(&value, response.data() + i, sizeof(int64_t));
        i += sizeof(int64_t);
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

    string result = str.substr(i, pos - i);
    i = pos + 1;
    return result;
}
void show_stats_result(const string& str, int& i){
    cout << deserialize(str, i, '=');
    cout << deserialize(str, i, ' ') << endl;
    cout << deserialize(str, i, '=');
    cout << stoi(deserialize(str, i, ' ')) << endl;
    cout << deserialize(str, i, '=');
    cout << stoi(deserialize(str, i, ' ')) << endl;
    cout << deserialize(str, i, '=');
    cout << stoi(deserialize(str, i, ' ')) << endl;
    cout << deserialize(str, i, '=');
    cout << stoi(deserialize(str, i, ' ')) << endl;
    cout << deserialize(str, i, '=');
    cout << stoi(deserialize(str, i, ' ')) << endl;
    cout << deserialize(str, i, '=');
    cout << stoi(deserialize(str, i, ' ')) << endl;
    cout << deserialize(str, i, '=');
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

int main1() {
    const vector<string> tests = {
        "PUT cpu_usage 1000 45.2 && PUT cpu_usage 1001 45.3 && PUT temperature 2000 36.6 && GET cpu_usage 1000 2000 && AGG cpu_usage 1000 2000 10 avg && AGG cpu_usage 1000 2000 10 min && AGG cpu_usage 1000 2000 10 max && AGG cpu_usage 1000 2000 10 sum && AGG cpu_usage 1000 2000 10 count && STATS cpu_usage && FLUSH cpu_usage && QUIT",
        "PUT cpu 1 10.0 && PUT cpu 2 20.0 && PUT cpu 3 30.0 && GET cpu 1 3",
        "PUT   cpu_usage    1000    45.2 && GET     cpu_usage   1000    2000 && AGG   cpu_usage   1000   2000   10   avg",
        "POT cpu_usage 1000 45.2 && GEET cpu_usage 1000 2000 && AGGG cpu_usage 1000 2000 10 avg && STAT cpu_usage && FLUS cpu_usage",
        "PUT cpu_usage 1000 && PUT cpu_usage && GET cpu_usage 1000 && GET cpu_usage && AGG cpu_usage 1000 2000 10 && AGG cpu_usage 1000 2000 && STATS && FLUSH",
        "PUT cpu_usage 1000 && PUT cpu_usage && GET cpu_usage 1000 && GET cpu_usage && AGG cpu_usage 1000 2000 10 && AGG cpu_usage 1000 2000 && STATS && FLUSH",
        "PUT cpu_usage 1000 0 && PUT cpu_usage 1000 -45.2 && PUT cpu_usage 1000 3.402823e38 && PUT cpu_usage 1000 -3.402823e38 && PUT cpu_usage 1000 NaN && PUT cpu_usage 1000 inf && PUT cpu_usage 1000 -inf && PUT cpu_usage 1000 abc",
        "GET cpu_usage 2000 1000 && GET cpu_usage 1000 1000 && GET cpu_usage 0 0 && GET cpu_usage -100 1000",
        "AGG cpu_usage 1000 2000 10 average && AGG cpu_usage 1000 2000 10 AVG && AGG cpu_usage 1000 2000 10 median && AGG cpu_usage 1000 2000 10 mode && AGG cpu_usage 1000 2000 0 avg && AGG cpu_usage 1000 2000 -10 avg",
        "HELLO WORLD && PUT && GET && AGG && RANDOM TEXT HERE && 12345 && !@#$%^&* && PUT cpu_usage 1000 45.2 extra_token",
        "PUT cpu 100 10 && PUT temp 100 36.5 && PUT cpu 101 11 && PUT temp 101 36.6 && GET cpu 100 102 && GET temp 100 102 && STATS cpu && STATS temp",
        "PUT cpu 100 10 && PUT cpu 101 11 && PUT cpu 100 9 && PUT cpu 102 12",
        "PUT cpu 100 10 && PUT cpu 100 20 && PUT cpu 100 30 && GET cpu 100 101",
        "GET unknown 0 1000 && STATS unknown",
        "PUT cpu 100 abc && PUT cpu 100 NaN && PUT cpu 100 inf && PUT cpu 100 -inf",
        "PUT cpu 100 10 && PUT cpu 200 20 && PUT cpu 150 15 && GET cpu 0 300",
        "PUT cpu 100 10 && QUIT && PUT cpu 101 11"
    };

    for (size_t i = 0; i < tests.size(); ++i) {
        int fd = connect_to_server();
        if (fd < 0) {
            cerr << "Test " << (i + 1) << ": connect failed\n";
            return 1;
        }

        const string& request = tests[i];
        cout << "========== Test " << (i + 1) << " ==========\n";
        cout << "Input: " << request << "\n\n";

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
