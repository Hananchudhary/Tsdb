#include <arpa/inet.h>
#include <cerrno>
#include <chrono>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <random>
#include <sstream>
#include <string>
#include <sys/stat.h>
#include <unistd.h>
#include <vector>

#include "./include/helpers.h"
#include "./src/compression.h"
#include "server_config.h"

using namespace std;
namespace fs = filesystem;

// ── Network helpers (same as client.cpp) ──────────────────────────────
ssize_t recv_all(int socket_fd, void* data, size_t length) {
    char* buffer = static_cast<char*>(data);
    size_t total_received = 0;
    while (total_received < length) {
        ssize_t received = recv(socket_fd, buffer + total_received,
                                length - total_received, 0);
        if (received < 0) {
            if (errno == EINTR) continue;
            return -1;
        }
        if (received == 0) return 0;
        total_received += received;
    }
    return static_cast<ssize_t>(total_received);
}

bool send_all(int socket_fd, const void* data, size_t length) {
    const char* buffer = static_cast<const char*>(data);
    size_t total_sent = 0;
    while (total_sent < length) {
        ssize_t sent = send(socket_fd, buffer + total_sent,
                            length - total_sent, 0);
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
    if (!send_all(socket_fd, &net_length, sizeof(net_length))) return false;
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

// ── Connect to server ─────────────────────────────────────────────────
int connect_to_server() {
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) return -1;

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

// ── Generate synthetic data ───────────────────────────────────────────
struct DataPoint {
    string metric_name;
    uint64_t timestamp;
    double value;
};

vector<DataPoint> generate_synthetic_data(int total_points, int num_metrics) {
    vector<string> metric_names;
    for (int i = 0; i < num_metrics; ++i) {
        metric_names.push_back("metric_" + to_string(i));
    }

    int points_per_metric = total_points / num_metrics;
    vector<DataPoint> data;
    data.reserve(total_points);

    mt19937_64 rng(42);  // Fixed seed for reproducibility

    for (int m = 0; m < num_metrics; ++m) {
        double current_value = (rng() % 20);

        for (int p = 0; p < points_per_metric; ++p) {
            DataPoint dp;
            dp.metric_name = metric_names[m];
            dp.timestamp = 100 + p;  // 1-second intervals starting at 1000000

            double drift = ((rng() % 21) - 10) / 1000000000000000.0;

            current_value += drift;
            dp.value = current_value;

            data.push_back(dp);
        }
    }

    // Interleave data from different metrics to simulate real-world usage
    vector<DataPoint> interleaved;
    interleaved.reserve(total_points);

    vector<int> indices(num_metrics, 0);
    int total_written = 0;

    while (total_written < total_points) {
        for (int m = 0; m < num_metrics; ++m) {
            if (indices[m] < points_per_metric) {
                int global_idx = m * points_per_metric + indices[m];
                interleaved.push_back(data[global_idx]);
                indices[m]++;
                total_written++;
                if (total_written >= total_points) break;
            }
        }
    }

    return interleaved;
}

// ── Calculate directory size ──────────────────────────────────────────
uint64_t get_directory_size(const string& path) {
    uint64_t total_size = 0;

    if (!fs::exists(path)) return 0;

    for (const auto& entry : fs::recursive_directory_iterator(path)) {
        if (entry.is_regular_file()) {
            total_size += entry.file_size();
        }
    }

    return total_size;
}

// ── Format bytes ──────────────────────────────────────────────────────
string format_bytes(uint64_t bytes) {
    stringstream ss;
    if (bytes < 1024) {
        ss << bytes << " B";
    } else if (bytes < 1024 * 1024) {
        ss << fixed << setprecision(2) << (bytes / 1024.0) << " KB";
    } else if (bytes < 1024 * 1024 * 1024) {
        ss << fixed << setprecision(2) << (bytes / (1024.0 * 1024.0)) << " MB";
    } else {
        ss << fixed << setprecision(2) << (bytes / (1024.0 * 1024.0 * 1024.0)) << " GB";
    }
    return ss.str();
}

// ── Main benchmark ────────────────────────────────────────────────────
int main() {
    const int TOTAL_POINTS = 500000;
    const int NUM_METRICS = 10;
    const string DATA_DIR = "./data";
    const string NAIVE_FILE = "./naive_data.bin";

    cout << "╔══════════════════════════════════════════════════════════╗\n";
    cout << "║           TSDB Benchmark Suite                          ║\n";
    cout << "╚══════════════════════════════════════════════════════════╝\n\n";

    // ── Step 1: Generate synthetic data ───────────────────────────────
    cout << "Generating " << TOTAL_POINTS << " data points across "
         << NUM_METRICS << " metrics...\n";

    auto gen_start = chrono::high_resolution_clock::now();
    vector<DataPoint> data = generate_synthetic_data(TOTAL_POINTS, NUM_METRICS);
    auto gen_end = chrono::high_resolution_clock::now();

    double gen_time = chrono::duration<double>(gen_end - gen_start).count();
    cout << "✓ Data generated in " << fixed << setprecision(3)
         << gen_time << " seconds\n\n";

    // ── Step 2: Clean data directory ──────────────────────────────────
    cout << "Cleaning data directory...\n";
    if (fs::exists(DATA_DIR)) {
        fs::remove_all(DATA_DIR);
    }
    cout << "✓ Data directory cleaned\n\n";

    // ── Step 3: Connect to server ─────────────────────────────────────
    cout << "Connecting to TSDB server...\n";
    int fd = connect_to_server();
    if (fd < 0) {
        cerr << "✗ Failed to connect to server. Is it running?\n";
        return 1;
    }
    cout << "✓ Connected to server\n\n";

    // ── Step 4: Insert all points ─────────────────────────────────────
    cout << "Inserting " << TOTAL_POINTS << " points via PUT...\n";

    auto insert_start = chrono::high_resolution_clock::now();

    // Build batch commands (100 commands per batch for efficiency)
    const int BATCH_SIZE = 100;
    int points_sent = 0;

    for (size_t i = 0; i < data.size(); i += BATCH_SIZE) {
        stringstream ss;
        int batch_count = min(BATCH_SIZE, static_cast<int>(data.size() - i));

        for (int j = 0; j < batch_count; ++j) {
            if (j > 0) ss << " && ";
            ss << "PUT " << data[i + j].metric_name << " "
               << data[i + j].timestamp << " " << data[i + j].value;
        }

        string command = ss.str();
        if (!send_with_size(fd, command.data(), static_cast<uint32_t>(command.size()))) {
            cerr << "✗ Send failed at point " << points_sent << "\n";
            close(fd);
            return 1;
        }

        string response;
        if (!recv_with_size(fd, response)) {
            cerr << "✗ Receive failed at point " << points_sent << "\n";
            close(fd);
            return 1;
        }

        points_sent += batch_count;

        // Progress indicator
        if (points_sent % 50000 == 0) {
            cout << "  Progress: " << points_sent << "/" << TOTAL_POINTS
                 << " points (" << (points_sent * 100 / TOTAL_POINTS) << "%)\n";
        }
    }

    auto insert_end = chrono::high_resolution_clock::now();
    double insert_time = chrono::duration<double>(insert_end - insert_start).count();
    double points_per_second = TOTAL_POINTS / insert_time;

    cout << "✓ All points inserted\n";
    cout << "  Time: " << fixed << setprecision(3) << insert_time << " seconds\n";
    cout << "  Throughput: " << fixed << setprecision(0)
         << points_per_second << " points/second\n\n";

    // ── Step 5: Flush all metrics ─────────────────────────────────────
    cout << "Flushing all metrics...\n";

    auto flush_start = chrono::high_resolution_clock::now();

    for (int m = 0; m < NUM_METRICS; ++m) {
        string command = "FLUSH metric_" + to_string(m);
        if (!send_with_size(fd, command.data(), static_cast<uint32_t>(command.size()))) {
            cerr << "✗ Flush send failed for metric_" << m << "\n";
            close(fd);
            return 1;
        }

        string response;
        if (!recv_with_size(fd, response)) {
            cerr << "✗ Flush receive failed for metric_" << m << "\n";
            close(fd);
            return 1;
        }
    }

    auto flush_end = chrono::high_resolution_clock::now();
    double flush_time = chrono::duration<double>(flush_end - flush_start).count();

    cout << "✓ All metrics flushed in " << fixed << setprecision(3)
         << flush_time << " seconds\n\n";

    // ── Step 6: Calculate disk usage ──────────────────────────────────
    uint64_t disk_bytes = get_directory_size(DATA_DIR);
    cout << "Disk usage (TSDB compressed):\n";
    cout << "  Total: " << format_bytes(disk_bytes) << " ("
         << disk_bytes << " bytes)\n";

    // Per-metric breakdown
    for (int m = 0; m < NUM_METRICS; ++m) {
        string metric_dir = DATA_DIR + "/metric_" + to_string(m);
        uint64_t metric_size = get_directory_size(metric_dir);
        cout << "  metric_" << m << ": " << format_bytes(metric_size)
             << " (" << metric_size << " bytes)\n";
    }
    cout << "\n";

    // ── Step 7: Generate naive format file ────────────────────────────
    cout << "Generating naive format file (16 bytes/point)...\n";

    uint64_t naive_bytes = TOTAL_POINTS * 16;  // 8 bytes timestamp + 8 bytes double

    ofstream naive_file(NAIVE_FILE, ios::binary);
    if (!naive_file.is_open()) {
        cerr << "✗ Failed to create naive file\n";
        close(fd);
        return 1;
    }

    for (const auto& dp : data) {
        naive_file.write(reinterpret_cast<const char*>(&dp.timestamp), sizeof(uint64_t));
        naive_file.write(reinterpret_cast<const char*>(&dp.value), sizeof(double));
    }
    naive_file.close();

    cout << "✓ Naive file created: " << format_bytes(naive_bytes)
         << " (" << naive_bytes << " bytes)\n\n";

    // ── Step 8: Calculate compression ratio ───────────────────────────
    double compression_ratio = static_cast<double>(naive_bytes) / disk_bytes;

    cout << "╔══════════════════════════════════════════════════════════╗\n";
    cout << "║                  BENCHMARK RESULTS                     ║\n";
    cout << "╚══════════════════════════════════════════════════════════╝\n\n";

    cout << "┌─────────────────────────────────────────────────────────┐\n";
    cout << "│ Metric                    │ Value                       │\n";
    cout << "├───────────────────────────┼─────────────────────────────┤\n";
    cout << "│ Total Points              │ " << setw(25) << TOTAL_POINTS << " │\n";
    cout << "│ Number of Metrics         │ " << setw(25) << NUM_METRICS << " │\n";
    cout << "├───────────────────────────┼─────────────────────────────┤\n";
    cout << "│ Insert Throughput         │ " << setw(21) << fixed << setprecision(0)
         << points_per_second << " pts/s │\n";
    cout << "│ Insert Time               │ " << setw(22) << fixed << setprecision(3)
         << insert_time << " sec │\n";
    cout << "│ Flush Time                │ " << setw(22) << fixed << setprecision(3)
         << flush_time << " sec │\n";
    cout << "├───────────────────────────┼─────────────────────────────┤\n";
    cout << "│ Naive Format (16B/pt)     │ " << setw(21) << format_bytes(naive_bytes)
         << setw(4) << " │\n";
    cout << "│ TSDB Compressed           │ " << setw(21) << format_bytes(disk_bytes)
         << setw(4) << " │\n";
    cout << "├───────────────────────────┼─────────────────────────────┤\n";
    cout << "│ Compression Ratio         │ " << setw(21) << fixed << setprecision(2)
         << compression_ratio << "x" << setw(2) << " │\n";
    cout << "│ Space Saved               │ " << setw(21) << format_bytes(naive_bytes - disk_bytes)
         << setw(4) << " │\n";
    cout << "│ Space Reduction           │ " << setw(20) << fixed << setprecision(1)
         << ((1.0 - disk_bytes / static_cast<double>(naive_bytes)) * 100) << "%" << setw(2) << " │\n";
    cout << "└───────────────────────────┴─────────────────────────────┘\n\n";

    // ── Cleanup ───────────────────────────────────────────────────────
    close(fd);

    cout << "Benchmark complete!\n";
    cout << "Note: Data files preserved in ./data/ and ./naive_data.bin\n";

    return 0;
}