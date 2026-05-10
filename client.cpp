#include <arpa/inet.h>
#include <cerrno>
#include <cstring>
#include <iostream>
#include <string>
#include<sstream>
#include <unistd.h>
#include "./include/helpers.h"
#include <sys/socket.h>
#include"server_config.h"
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
        close(fd);
        return 1;
    }

    if (connect(fd, (sockaddr*)&server, sizeof(server)) < 0) {
        cerr << "connect failed\n";
        close(fd);
        return 1;
    }

    cout << "Connected to TSDB server\n";
    cout << "Type commands. Use QUIT to exit.\n";

    while (true) {
        cout << "> ";
        cout.flush();

        string msg;
        getline(cin, msg);

        if (msg.empty())
            continue;

        if (!send_with_size(fd, msg.data(),(msg.size()))) {
            cerr << "send failed\n";
            break;
        }

        string response;
        if (!recv_with_size(fd, response)) {
            cerr << "recv failed\n";
            break;
        }

        cout << "Server:\n";
        show_result(response);

        if (msg == "QUIT" || msg == "quit")
            break;
    }
    close(fd);
    return 0;
}
/* 
PUT cpu 1 10 && PUT temp 1 36.5 && PUT cpu 2 20 && GET cpu 0 10 && PUT cpu 1 5 && STATS cpu && GET temp 0 10 && INVALID && PUT temp 2 36.6 && GET temp 0 10 && QUIT
*/