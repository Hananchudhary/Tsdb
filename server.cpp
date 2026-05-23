#include <arpa/inet.h>
#include <cerrno>
#include <csignal>
#include<variant>
#include <cstring>
#include"./src/Parser.h"
#include"./src/compression.h"
#include<sstream>
#include <iostream>
#include <string>
#include<algorithm>
#include<fstream>
#include<filesystem>
#include <thread>
#include <unistd.h>
#include <semaphore>
#include <sys/socket.h>
#include<mutex>

#include<unordered_map>
using namespace std;
namespace fs = filesystem;
mutex registry_mutex;
counting_semaphore<kMaxThreads> thread_limit(kMaxThreads);
const string registeration_path = "./registerations.bin";
unordered_map<string, HeadBlock> metric_registery;
unordered_map<string, uint64_t> registery_time;
void write_registeration_times(const string& path){
    ofstream file(path, ios::binary);
    if(!file.is_open()) return;
    size_t size = registery_time.size();
    file.write(reinterpret_cast<const char*>(&size), sizeof(size));
    for(auto a : registery_time){
        size = a.first.size();
        file.write(reinterpret_cast<const char*>(&size),sizeof(size));
        file.write(a.first.data(),a.first.size());
        file.write(reinterpret_cast<const char*>(&a.second), sizeof(uint64_t));
    }
    file.close();
}
void read_registeration_times(const string& path){
    ifstream file(path, ios::binary);
    if(!file.is_open()) return;
    size_t size = 0;
    file.read(reinterpret_cast<char*>(&size), sizeof(size));
    for(int i = 0;i<size;i++){
        size_t s = 0;
        file.read(reinterpret_cast<char*>(&size),sizeof(s));
        string metric_name(s, '\0');
        file.read(metric_name.data(),s);
        uint64_t time = 0;
        file.read(reinterpret_cast<char*>(&time), sizeof(uint64_t));
        registery_time[metric_name] = time;
    }
    file.close();
}
bool initialize() {
    try {
        string basePath = "./data/";

        if (!fs::exists(basePath)) return true;

        for (const auto& entry : fs::directory_iterator(basePath)) {
            if (!entry.is_directory()) continue;

            string metric_name = entry.path().filename().string();
            string walPath = entry.path().string() + "/wal.bin";

            HeadBlock hb;

            if (fs::exists(walPath)) {
                ifstream file(walPath, ios::binary);

                if (!file.is_open()) continue;

                while (true) {
                    uint64_t ts;
                    double val;

                    file.read(reinterpret_cast<char*>(&ts), sizeof(ts));
                    if (file.gcount() != sizeof(ts)) break;

                    file.read(reinterpret_cast<char*>(&val), sizeof(val));
                    if (file.gcount() != sizeof(val)) break;

                    if (hb.timestamps.size() < hb.capacity) {
                        hb.timestamps.push_back(ts);
                        hb.values.push_back(val);
                    }
                }

                file.close();
            }
            lock_guard<mutex> lock(registry_mutex);
            metric_registery[metric_name] = move(hb);
        }
        read_registeration_times(registeration_path);
        cout << "System initialized\n";
        return true;
    }
    catch (const exception& e) {
        cout << "Initialization failed: " << e.what() << "\n";
        return false;
    }
}
string serialize(const vector<uint64_t>& ts, const vector<double>& values)
{
    string buffer{};

    uint32_t n = ts.size();
    buffer.resize(n * (sizeof(uint64_t) +  sizeof(double)) + 1 + sizeof(uint64_t));
    memset(buffer.data(),0,n * (sizeof(uint64_t) +  sizeof(double)) + 1 +sizeof(uint64_t));
    uint32_t offset = 0;
    uint64_t s = ts.size();
    memcpy(buffer.data() + offset, &s, sizeof(uint64_t));
    offset += sizeof(uint64_t);
    for (uint32_t i = 0; i < n;i++) {
        memcpy(buffer.data() + offset, &ts[i], sizeof(uint64_t));
        offset += sizeof(uint64_t);
        memcpy(buffer.data() + offset, &values[i], sizeof(double));
        offset += sizeof(double);
    }

    return buffer;
}
string serializeStats(const StatsResult& s) {
    
    return
        "metric_name=" + s.metric_name +
        " total_points=" + to_string(s.total_points) +
        " in_memory=" + to_string(s.in_memory) +
        " on_disk=" + to_string(s.on_disk) +
        " disk_chunks=" + to_string(s.disk_chunks) +
        " first_timestamp=" + to_string(s.first_timestamp) +
        " last_timestamp=" + to_string(s.last_timestamp) + ' ';
}
string get_result(const CommandData& command) {
    string res{};
    lock_guard<mutex> lock(registry_mutex);
    if (holds_alternative<PutCommand>(command)) {
        const PutCommand& put = get<PutCommand>(command);
        if(metric_registery.count(put.metric_name) == 0){
            uint64_t timestamp = chrono::duration_cast<chrono::milliseconds>(
                chrono::system_clock::now().time_since_epoch()
            ).count();
            registery_time[put.metric_name] = timestamp;
            metric_registery[put.metric_name] = HeadBlock{};
        }
        HeadBlock& hb = metric_registery[put.metric_name];
        int res = put.handleRequest(hb);
        string response(1, static_cast<char>(MessageType::PUT));
        if(res == -1){
            response = response + "Overflow flush error.";   
        }
        else if (res == -2){
            response = response + "Timestamp must be in non-decreasing.";   
        }
        else if(res == -3){
            cout << "Data could not write to log\n";
        }
        else{
            response = response + "Inserted Successfully.";
        }
        return response;
    }
    if (holds_alternative<GetCommand>(command)) {
        const GetCommand& gets = get<GetCommand>(command);
        string response(1, static_cast<char>(MessageType::GET));
        if(metric_registery.count(gets.metric_name) == 0){
            return string(1, static_cast<char>(MessageType::error)) + "No such metric exists.";
        }
        HeadBlock& hb = metric_registery[gets.metric_name];
        pair<vector<uint64_t>, vector<double>> res = gets.handleRequest(hb);
        return response + serialize(res.first, res.second);
    }
    if (holds_alternative<AggCommand>(command)) {
        const AggCommand& agg = get<AggCommand>(command);
        if(metric_registery.count(agg.metric_name) == 0){
            return string(1, static_cast<char>(MessageType::error)) + "No such metric exists.";
        }
        string response(1, static_cast<char>(MessageType::AGG));
        HeadBlock& hb = metric_registery[agg.metric_name];
        pair<vector<uint64_t>, vector<double>> res = agg.handleRequest(hb);
        return response + serialize(res.first, res.second);
    }
    if (holds_alternative<StatsCommand>(command)) {
        const StatsCommand& stats = get<StatsCommand>(command);
        string response(1, static_cast<char>(MessageType::STATS));

        if(metric_registery.count(stats.metric_name) == 0){
            return string(1, static_cast<char>(MessageType::error)) + "No such metric exists.";
        }
        HeadBlock& hb = metric_registery[stats.metric_name];
        StatsResult res = stats.handleRequest(hb);
        return response + serializeStats(res);
    }
    if (holds_alternative<FlushCommand>(command)) {
        const FlushCommand& flush = get<FlushCommand>(command);
        string response(1, static_cast<char>(MessageType::FLUSH));
        
        if(metric_registery.count(flush.metric_name) == 0){
            return string(1, static_cast<char>(MessageType::error)) + "No such metric exists.";
        }
        HeadBlock& hb = metric_registery[flush.metric_name];
        bool res = flush.handleRequest(hb);
        return res ? response + "Flushed Successfully." : response + "Unknown error occured.";
    }
    return ".";
}
ssize_t recv_all(int socket_fd, void* data, size_t length) {
    char* buffer = static_cast<char*>(data);
    size_t total_received = 0;

    while (total_received < length) {
        ssize_t received = recv(socket_fd,
                                buffer + total_received,
                                length - total_received,
                                0);

        if (received < 0) {
            if (errno == EINTR) continue;
            return -1;
        }

        if (received == 0) return 0;

        total_received += received;
    }

    return total_received;
}

bool send_all(int socket_fd, const void* data, size_t length) {
    const char* buffer = static_cast<const char*>(data);
    size_t total_sent = 0;

    while (total_sent < length) {
        ssize_t sent = send(socket_fd,
                            buffer + total_sent,
                            length - total_sent,
                            0);

        if (sent < 0) {
            if (errno == EINTR) continue;
            return false;
        }

        if (sent == 0) return false;

        total_sent += sent;
    }

    return true;
}

bool send_with_size(int socket_fd, const void* data, uint32_t length) {
    uint32_t net_length = htonl(length);
    if (!send_all(socket_fd, &net_length, sizeof(net_length)))
        return false;
    return send_all(socket_fd, data, length);
}

bool recv_with_size(int socket_fd, string& out) {
    uint32_t net_length = 0;

    if (recv_all(socket_fd, &net_length, sizeof(net_length)) <= 0)
        return false;
    uint32_t length = ntohl(net_length);

    out.resize(length);

    return recv_all(socket_fd, out.data(), length) > 0;
}

void handle_client(int client_fd, sockaddr_in client_addr) {
    char ip[INET_ADDRSTRLEN]{};
    inet_ntop(AF_INET, &client_addr.sin_addr, ip, sizeof(ip));

    cout << "Client: " << ip << ":" << ntohs(client_addr.sin_port) << '\n';

    while (true) {
        string msg;

        if (!recv_with_size(client_fd, msg)) {
            cerr << "Client disconnected\n";
            break;
        }

        LineProtocolParser parser;
        vector<ParseResult> results = parser.parse_line(msg);

        if (parser.has_pending_data()) {
            string temp("\n");
            vector<ParseResult> flushed_results = parser.parse_line(temp);
            results.insert(results.end(),
                           flushed_results.begin(),
                           flushed_results.end());
        }

        string reply{};
        bool has_error = false;

        for (const ParseResult& result : results) {
            if (!result.ok()) {
                const ParseError& error = *result.error;

                reply += static_cast<char>(MessageType::error);
                reply += error.message;

                has_error = true;
            }
            else {
                if (result.command &&
                    holds_alternative<QuitCommand>(result.command->data)) {

                    reply += static_cast<char>(MessageType::QUIT);
                    reply += "BYEE.";

                    send_with_size(client_fd,
                                   reply.data(),
                                   reply.size());

                    close(client_fd);
                    thread_limit.release();
                    return;
                }

                reply += get_result(result.command->data);
            }

            reply += '&';
        }

        if (!results.empty())
            reply.back() = '\0';

        if (!send_with_size(client_fd,
                            reply.data(),
                            reply.size())) {
            cerr << "send failed\n";
            break;
        }
    }

    close(client_fd);
    thread_limit.release();
}
void retention_cleaner_thread() {

    while (true) {

        this_thread::sleep_for(chrono::minutes(1));

        uint64_t now = static_cast<uint64_t>(time(nullptr));
        lock_guard<mutex> lock(registry_mutex);
        

        for (const auto& [metric_name, retention_seconds]
             : registery_time) {

            string dirPath = "./data/" + metric_name;

            if (!fs::exists(dirPath))
                continue;

            uint64_t horizon =
                now - retention_seconds;

            vector<string> chunk_files =
                get_chunk_files(dirPath);

            for (const auto& path : chunk_files) {

                try {

                    uint64_t last_ts = get_last_timestamp_from_chunk(path);

                    if (last_ts < horizon) {
                        zstd_compress(path);
                        fs::remove(path);

                        cout
                            << "[Cleaner] Deleted expired chunk: "
                            << path << endl;
                    }

                }
                catch (...) {

                    cout
                        << "[Cleaner] Failed to process: "
                        << path << endl;
                }
            }
        }
    }
}
int main() {
    signal(SIGPIPE, SIG_IGN);

    int server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd < 0) {
        cerr << "socket failed\n";
        return 1;
    }

    int opt = 1;
    setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    addr.sin_port = htons(kPort);

    if (bind(server_fd, (sockaddr*)&addr, sizeof(addr)) < 0) {
        cerr << "bind failed\n";
        return 1;
    }

    if (listen(server_fd, kBacklog) < 0) {
        cerr << "listen failed\n";
        return 1;
    }

    cout << "Server running...\n";
    if(!initialize()){
        return 1;
    }
    thread cleaner(retention_cleaner_thread);
    cleaner.detach();

    while (true) {
        sockaddr_in client_addr{};
        socklen_t len = sizeof(client_addr);

        int client_fd = accept(server_fd,
                                (sockaddr*)&client_addr,
                                &len);

        if (client_fd < 0) {
            if (errno == EINTR) continue;
            cerr << "accept failed\n";
            continue;
        }

        thread_limit.acquire();

        thread([=]() mutable {
            handle_client(client_fd, client_addr);
        }).detach();
    }
    cout << "BYEEE\n";
    close(server_fd);
    write_registeration_times(registeration_path);
    return 0;
}