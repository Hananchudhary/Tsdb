#include <arpa/inet.h>
#include <cerrno>
#include <csignal>
#include<variant>
#include <cstring>
#include"./src/Parser.h"
#include<sstream>
#include <iostream>
#include <string>
#include <thread>
#include <unistd.h>
#include <semaphore>
#include <sys/socket.h>
#include"./include/config.h"
#include<unordered_map>
using namespace std;
unordered_map<string, HeadBlock> metric_registery;

std::counting_semaphore<kMaxThreads> thread_limit(kMaxThreads);

string serialize(const vector<int64_t>& ts, const vector<double>& values)
{
    string buffer{};

    uint32_t n = ts.size();
    buffer.resize(n * (sizeof(int64_t) +  sizeof(double)) + 1);
    memset(buffer.data(),0,n * (sizeof(int64_t) +  sizeof(double)) + 1);
    uint32_t offset = 0;
    for (uint32_t i = 0; i < n;i++) {
        memcpy(buffer.data() + offset, &ts[i], sizeof(int64_t));
        offset += sizeof(int64_t);
        memcpy(buffer.data() + offset, &values[i], sizeof(double));
        offset += sizeof(double);
    }

    return buffer;
}
string serializeStats(const StatsResult& s) {
    
    return
        "metric_name=" + s.metric_name +
        " total_points=" + std::to_string(s.total_points) +
        " in_memory=" + std::to_string(s.in_memory) +
        " on_disk=" + std::to_string(s.on_disk) +
        " disk_chunks=" + std::to_string(s.disk_chunks) +
        " first_timestamp=" + std::to_string(s.first_timestamp) +
        " last_timestamp=" + std::to_string(s.last_timestamp);
}
string get_result(const CommandData& command) {
    string res{};
    if (holds_alternative<PutCommand>(command)) {
        const PutCommand& put = get<PutCommand>(command);
        if(metric_registery.count(put.metric_name) == 0){
            metric_registery[put.metric_name] = HeadBlock{};
        }
        HeadBlock& hb = metric_registery[put.metric_name];
        bool res = put.handleRequest(hb);
        string response(1, static_cast<char>(MessageType::PUT));
        return res ? response + "Inserted Successfully" : response + "Timestamp must be in non-decreasing";
    }
    if (holds_alternative<GetCommand>(command)) {
        const GetCommand& gets = get<GetCommand>(command);
        string response(1, static_cast<char>(MessageType::GET));
        if(metric_registery.count(gets.metric_name) == 0){
            return string(1, static_cast<char>(MessageType::error)) + "No such metric exists";
        }
        HeadBlock& hb = metric_registery[gets.metric_name];
        pair<vector<int64_t>, vector<double>> res = gets.handleRequest(hb);
        return response + serialize(res.first, res.second);
    }
    if (holds_alternative<AggCommand>(command)) {
        const AggCommand& agg = get<AggCommand>(command);
        if(metric_registery.count(agg.metric_name) == 0){
            return string(1, static_cast<char>(MessageType::error)) + "No such metric exists";
        }
        string response(1, static_cast<char>(MessageType::AGG));
        HeadBlock& hb = metric_registery[agg.metric_name];
        pair<vector<int64_t>, vector<double>> res = agg.handleRequest(hb);
        return response + serialize(res.first, res.second);
    }
    if (holds_alternative<StatsCommand>(command)) {
        const StatsCommand& stats = get<StatsCommand>(command);
        string response(1, static_cast<char>(MessageType::STATS));

        if(metric_registery.count(stats.metric_name) == 0){
            return string(1, static_cast<char>(MessageType::error)) + "No such metric exists";
        }
        HeadBlock& hb = metric_registery[stats.metric_name];
        StatsResult res = stats.handleRequest(hb);
        return response + serializeStats(res);
    }
    if (holds_alternative<FlushCommand>(command)) {
        const FlushCommand& flush = get<FlushCommand>(command);
        string response(1, static_cast<char>(MessageType::FLUSH));
        
        if(metric_registery.count(flush.metric_name) == 0){
            return string(1, static_cast<char>(MessageType::error)) + "No such metric exists";
        }
        HeadBlock& hb = metric_registery[flush.metric_name];
        bool res = flush.handleRequest(hb);
        return res ? response + "Flushed Successfully" : response + "Unknown error occured";
    }
    return "";
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

bool recv_with_size(int socket_fd, std::string& out) {
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

    string msg;

    if (!recv_with_size(client_fd, msg)) {
        cerr << "recv failed\n";
        close(client_fd);
        thread_limit.release();
        return;
    }

    LineProtocolParser parser;
    vector<ParseResult> results = parser.parse_line(msg);
    if (parser.has_pending_data()) {
        vector<ParseResult> flushed_results = parser.parse_line("\n");
        results.insert(results.end(), flushed_results.begin(), flushed_results.end());
    }

    string reply{};
    bool has_error = false;
    for (const ParseResult& result : results) {
        if (!result.ok()) {
            const ParseError& error = *result.error;
            cerr << "Parse error: " << error.message << " | input: " << error.line << '\n';
            reply = reply + static_cast<char>(MessageType::error) + error.message;
            has_error = true;
        }
        else{
            if (holds_alternative<QuitCommand>(result.command->data)) {
                reply = reply  + static_cast<char>(MessageType::QUIT);
                break;
            }
            else{
                reply = reply + get_result(result.command->data);
            }
        }
        reply += '&';

    }
    if (results.empty() && !has_error) {
        reply = "ERROR: empty request";
    }
    reply[reply.size() - 1] = '\0';
    if (!send_with_size(client_fd, reply.data(), reply.size())) {
        cerr << "send failed\n";
    }

    close(client_fd);
    cout << "Client disconnected\n";

    thread_limit.release();
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

    close(server_fd);
    return 0;
}