#include <arpa/inet.h>
#include <cerrno>
#include <csignal>
#include <cstring>
#include <iostream>
#include <string>
#include <thread>
#include <unistd.h>
#include <semaphore>
#include <sys/socket.h>

using namespace std;

constexpr int kPort = 8080;
constexpr int kBacklog = 16;
constexpr int kMaxThreads = 10;

std::counting_semaphore<kMaxThreads> thread_limit(kMaxThreads);

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

    std::string msg;

    if (!recv_with_size(client_fd, msg)) {
        cerr << "recv failed\n";
        close(client_fd);
        thread_limit.release();
        return;
    }

    cout << "Received: " << msg << '\n';

    std::string reply = "HELLO";

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