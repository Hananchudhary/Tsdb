CXX = g++
CXXFLAGS = -std=c++20 -Wall -Wextra -O2
LDFLAGS = -pthread

# Main targets
all: server client test benchmark

server: server.cpp src/Parser.cpp
	$(CXX) $(CXXFLAGS) -o $@ server.cpp src/Parser.cpp $(LDFLAGS)

client: client.cpp
	$(CXX) $(CXXFLAGS) -o $@ client.cpp

test: tests/test.cpp src/Parser.cpp
	$(CXX) $(CXXFLAGS) -o $@ tests/test.cpp src/Parser.cpp $(LDFLAGS)

benchmark: benchmarks/benchmark.cpp src/Parser.cpp
	$(CXX) $(CXXFLAGS) -o $@ benchmarks/benchmark.cpp src/Parser.cpp $(LDFLAGS)

# Clean build artifacts
clean:
	rm -f server client test benchmark naive_data.bin

# Clean everything including data
distclean: clean
	rm -rf data/

# Run benchmark (requires server to be running)
run-benchmark: benchmark
	@echo "Make sure the server is running on port 8080!"
	@echo "Start server: ./server"
	@echo "Then run: make run-benchmark"
	./benchmark

# Help target
help:
	@echo "Available targets:"
	@echo "  all        - Build server, client, test, and benchmark"
	@echo "  server     - Build the TSDB server"
	@echo "  client     - Build the interactive client"
	@echo "  test       - Build the test suite"
	@echo "  benchmark  - Build the benchmark program"
	@echo "  clean      - Remove build artifacts"
	@echo "  distclean  - Remove build artifacts and data directory"
	@echo "  run-benchmark - Run the benchmark (server must be running)"
	@echo "  help       - Show this help message"

.PHONY: all clean distclean run-benchmark help