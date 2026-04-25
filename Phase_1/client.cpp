#include <arpa/inet.h>
#include <cerrno>
#include <cstring>
#include <iostream>
#include <string>
#include <unistd.h>
#include <sys/socket.h>

using namespace std;

constexpr int kPort = 8080;
constexpr const char* kServerIp = "127.0.0.1";

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

    string msg = "HI";

    if (!send_with_size(fd, msg.data(), msg.size())) {
        cerr << "send failed\n";
        return 1;
    }

    string response;

    if (!recv_with_size(fd, response)) {
        cerr << "recv failed\n";
        return 1;
    }

    cout << "Server: " << response << '\n';

    close(fd);
    return 0;
}