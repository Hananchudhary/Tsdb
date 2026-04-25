#include <arpa/inet.h>
#include <cerrno>
#include <cstring>
#include <iostream>
#include <string>
#include<sstream>
#include <unistd.h>
#include "./include/helpers.h"
#include <sys/socket.h>
#include "server_config.h"
using namespace std;

ssize_t recv_all(int socket_fd, void* data, size_t length) {
    char* buffer = static_cast<char*>(data);
    size_t total = 0;

    while (total < length) {
        ssize_t r = recv(socket_fd, buffer + total, length - total, 0);
        
        if (r < 0) {
            if (errno == EINTR) continue;
            return -1;
        }

        if (r == 0) return 0;

        total += r;
    }

    return total;
}

bool send_all(int socket_fd, const void* data, size_t length) {
    const char* buf = (const char*)data;
    size_t sent_total = 0;

    while (sent_total < length) {
        ssize_t s = send(socket_fd, buf + sent_total, length - sent_total, 0);

        if (s < 0) {
            if (errno == EINTR) continue;
            return false;
        }

        if (s == 0) return false;

        sent_total += s;
    }

    return true;
}

bool send_with_size(int socket_fd, const void* data, uint32_t length) {
    uint32_t net = htonl(length);

    if (!send_all(socket_fd, &net, sizeof(net)))
        return false;

    return send_all(socket_fd, data, length);
}

bool recv_with_size(int socket_fd, std::string& out) {
    uint32_t net = 0;

    if (recv_all(socket_fd, &net, sizeof(net)) <= 0)
        return false;

    uint32_t len = ntohl(net);
    out.resize(len);

    return recv_all(socket_fd, out.data(), len) > 0;
}
void show_pair_result(string& response){
    uint32_t offset = 0;
    int64_t value;
    while(response[offset] != '\0'){
        int64_t value;
        memcpy(&value, response.data() + offset, sizeof(int64_t));
        offset += sizeof(int64_t);
        double val;
        memcpy(&val, response.data() + offset, sizeof(double));
        offset += sizeof(int64_t);
        cout << "(" << value << " , " << val << ")\n";
    }
}
void show_stats_result(string& str){
    
    std::istringstream iss(str);
    std::string token;

    while (iss >> token) {
        size_t pos = token.find('=');
        if (pos == std::string::npos) continue;
    
        std::string key = token.substr(0, pos);
        std::string value = token.substr(pos + 1);
        cout << key << " = ";
        if (key == "metric_name") {
            cout << value;
        }
        else if (key == "total_points") {
            cout << stoi(value);
        }
        else if (key == "in_memory") {
            cout << stoi(value);
        }
        else if (key == "on_disk") {
            cout << stoi(value);
        }
        else if (key == "disk_chunks") {
            cout << stoi(value);
        }
        else if (key == "first_timestamp") {
            cout << stoll(value);
        }
        else if (key == "last_timestamp") {
            cout << stoll(value);
        }
        cout << endl;
    }

}

void show_result(std::string& response) {
    std::istringstream iss(response);
    std::string line;

    while (std::getline(iss, line, '&')) {
        if (line.empty()) continue;
        char type = line[0];
        string res = line.substr(1);
        if (type == static_cast<char>(MessageType::PUT)) {
            cout << res << endl;
        }
        else if (type == static_cast<char>(MessageType::GET)) { 
            show_pair_result(res);
        }
        else if (type == static_cast<char>(MessageType::AGG)) {
            show_pair_result(res);
        }
        else if (type == static_cast<char>(MessageType::FLUSH)) {
            cout << res << endl;
        }
        else if (type == static_cast<char>(MessageType::QUIT)) {
            cout << res << endl;
        }
        else if (type == static_cast<char>(MessageType::STATS)) {
            show_stats_result(res);
        }
        else if(type == static_cast<char>(MessageType::error)){
            cout << res << endl;
        }
    }
}
int main() {
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) {
        cerr << "socket failed\n";
        return 1;
    }

    sockaddr_in server{};
    server.sin_family = AF_INET;
    server.sin_port = htons(kPort);

    if (inet_pton(AF_INET, kServerIp, &server.sin_addr) <= 0) {
        cerr << "inet_pton failed\n";
        return 1;
    }

    if (connect(fd, (sockaddr*)&server, sizeof(server)) < 0) {
        cerr << "connect failed\n";
        return 1;
    }

    string msg = "PUT cpu 1 10 && PUT temp 1 36.5 && PUT cpu 2 20 && GET cpu 0 10 && PUT cpu 1 5 && STATS cpu && GET temp 0 10 && INVALID && PUT temp 2 36.6 && GET temp 0 10 && QUIT";

    if (!send_with_size(fd, msg.data(), msg.size())) {
        cerr << "send failed\n";
        return 1;
    }

    string response;

    if (!recv_with_size(fd, response)) {
        cerr << "recv failed\n";
        return 1;
    }
    cout << "Server: \n";
    show_result(response);
    close(fd);
    return 0;
}
/* 
PUT cpu 1 10 && PUT temp 1 36.5 && PUT cpu 2 20 && GET cpu 0 10 && PUT cpu 1 5 && STATS cpu && GET temp 0 10 && INVALID && PUT temp 2 36.6 && GET temp 0 10 && QUIT
*/