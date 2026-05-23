#pragma once
#include<iostream>
#include<vector>
#include<memory.h>
#include"../include/helpers.h"
#include<fstream>
#include<algorithm>
#include<filesystem>
using namespace std;
namespace fs = filesystem;
inline void bw_write(BitWriter* bw, uint64_t value, uint16_t n_bits) {
    value = value << (64 - n_bits);
    while (n_bits > 0) {
        uint8_t free = (8 - bw->bits_filled);
        uint8_t can_have = free  > n_bits ? n_bits : free;
        uint8_t x = (value >> (64 - can_have));
        value = value << can_have;
        x = x << (8 - bw->bits_filled - can_have);
        bw->current_byte |= x;
        bw->bits_filled += can_have;
        n_bits -= can_have;
        if(bw->bits_filled > 7){
            bw->buffer.push_back(bw->current_byte);
            bw->current_byte = 0;
            bw->bits_filled = 0;
        }
    }
}
inline uint64_t br_read (BitReader* br , uint16_t n_bits){
    uint64_t res = 0;
    while(n_bits > 0 && br->byte_index < br->buffer.size()){
        uint8_t x = br->buffer[br->byte_index];
        uint8_t to_read = n_bits > (8 - br->bit_index) ? (8 - br->bit_index) : n_bits;
        res = res << to_read;
        x = x << (br->bit_index);
        x = x >> (8 - to_read);
        res |= x;
        n_bits-=to_read;
        br->bit_index+=to_read;
        if(br->bit_index > 7){
            br->byte_index++;
            br->bit_index = 0;
        }
    }
    return res;
}
inline uint64_t encode_signed(int64_t x, int bits) {
    uint64_t mask = (1ULL << bits) - 1;
    return (uint64_t)x & mask;
}
inline int64_t decode_signed(uint64_t x, int bits) {
    uint64_t sign = 1ULL << (bits - 1);
    return (x ^ sign) - sign;
}
inline void bw_flush(BitWriter* bw) {
    if (bw->bits_filled > 0) {
        bw->buffer.push_back(bw->current_byte);
        bw->current_byte = 0;
        bw->bits_filled = 0;
    }
}
inline void print_buffer(const vector<uint8_t>& buf, int by) {
    for (int i = by;i < by + 2;i++) {
        uint8_t b = buf[i];
        for (int i = 7; i >= 0; i--) {
            cout << ((b >> i) & 1);
        }
        cout << " ";
    }
    cout << endl;
}
inline void timestamp_encode(BitWriter* bw, const vector<uint64_t>& timestamps){
    uint16_t size = timestamps.size();
    if(size == 0) return;
    bw_write(bw, timestamps[0], 64);

    if(size == 1) return;
    int64_t x1 = timestamps[1] - timestamps[0];
    bw_write(bw, encode_signed(x1, 14), 14);

    for(uint16_t i = 2;i<size;i++){
        int64_t x2 = timestamps[i] - timestamps[i-1];
        int64_t D = x2 - x1;
        x1 = x2;
        
        if(D == 0){
            bw_write(bw, 0b0, 1);
        }
        else if(D <= 64 && D >= -63){
            bw_write(bw, 0b10, 2);
            bw_write(bw, encode_signed(D, 7), 7);
        }
        else if(-255 <= D && D <= 256){
            bw_write(bw, 0b110, 3);
            bw_write(bw, encode_signed(D, 9), 9);
        }
        else if(-2047 <= D && D <= 2048){
            bw_write(bw, 0b1110, 4);
            bw_write(bw, encode_signed(D, 12), 12);
        }
        else{
            bw_write(bw, 0b1111, 4);
            bw_write(bw, encode_signed(D, 32), 32);
        }
    }
    bw_flush(bw);
}
inline uint8_t to_read(BitReader* br){
    uint64_t x = br_read(br, 1);
    if(x == 0) return 0;
    x = br_read(br, 1);
    if(x == 0) return 7;
    x = br_read(br, 1);
    if(x == 0) return 9;
    x = br_read(br, 1);
    if(x == 0) return 12;
    return 32;
}
inline vector<uint64_t> timestamp_decode(BitReader* br, const uint16_t& N){
    vector<uint64_t> time;
    if (N == 0) return time;

    uint64_t last = br_read(br, 64);
    time.push_back(last);

    if (N == 1) return time;

    uint64_t raw_prev_delta = br_read(br, 14);
    int64_t prev_delta = decode_signed(raw_prev_delta, 14);
    last += prev_delta;
    time.push_back(last);
    for (uint16_t i = 2; i < N; i++) {
        uint8_t read = to_read(br);
        uint64_t raw_D = br_read(br, read);
        int64_t D = decode_signed(raw_D, read);
        int64_t delta = prev_delta + D;
        
        last += delta;
        time.push_back(last);
        prev_delta = delta;
    }
    return time;
}
inline uint8_t find_leading_zeroes(const uint64_t& x){
    if(x == 0) return 0;
    uint8_t ct = 0;
    uint64_t a = 0x8000000000000000;
    while(a != (a & x)){
        a = a >> 1;
        ct++;
    }
    return ct;
}
inline uint8_t find_trailing_zeroes(const uint64_t& x){
    if(x == 0) return 0;
    uint8_t ct = 0;
    uint64_t a = 1;
    while(a != (a & x)){
        a = a << 1;
        ct++;
    }
    return ct;
}
inline void value_encode(BitWriter* bw, const vector<double>& values) {
    int size = values.size();

    if (size == 0) return;

    uint64_t prev_bits = 0;
    memcpy(&prev_bits, &values[0], sizeof(double));
    bw_write(bw, prev_bits, 64);

    uint8_t prev_lead = 0;
    uint8_t prev_trail = 0;

    bool has_prev_block = false;

    for (int i = 1; i < size; i++) {
        uint64_t curr_bits = 0;
        memcpy(&curr_bits, &values[i], sizeof(double));

        uint64_t x = curr_bits ^ prev_bits;
        if (x == 0) {
            bw_write(bw, 0b0, 1);
            prev_bits = curr_bits;
            continue;
        }

        uint8_t lead = find_leading_zeroes(x);
        uint8_t trail = find_trailing_zeroes(x);

        if (has_prev_block &&
            lead >= prev_lead &&
            trail >= prev_trail) {

            bw_write(bw, 0b10, 2);

            uint8_t n =
                64 - prev_lead - prev_trail;

            uint64_t meaningful =
                x >> prev_trail;

            bw_write(bw, meaningful, n);
        }

        else {
            bw_write(bw, 0b11, 2);

            bw_write(bw, lead, 5);

            uint8_t n =
                64 - lead - trail;
            uint8_t stored_n =
                (n == 64) ? 0 : n;

            bw_write(bw, stored_n, 6);

            uint64_t meaningful =
                x >> trail;

            bw_write(bw, meaningful, n);

            prev_lead = lead;
            prev_trail = trail;

            has_prev_block = true;
        }

        prev_bits = curr_bits;
    }

    bw_flush(bw);
}
inline vector<double> value_decode(BitReader* br, const uint16_t& N) {
    vector<double> vals;

    if (N == 0)
        return vals;
    uint64_t prev_bits = br_read(br, 64);

    double v;
    memcpy(&v, &prev_bits, sizeof(double));

    vals.push_back(v);

    if (N == 1)
        return vals;

    uint8_t prev_lead = 0;
    uint8_t prev_trail = 0;

    bool has_prev_block = false;

    for (int i = 1; i < N; i++) {

        uint8_t ctrl = br_read(br, 1);

        // Case 0: identical value
        if (ctrl == 0) {
            vals.push_back(v);
            continue;
        }

        uint64_t x = 0;

        uint8_t second = br_read(br, 1);

        if (second == 0) {

            uint8_t n =
                64 - prev_lead - prev_trail;

            x = br_read(br, n);

            x <<= prev_trail;
        }

        else {

            uint8_t lead = br_read(br, 5);

            uint8_t stored_n =
                br_read(br, 6);

            uint8_t n =
                (stored_n == 0)
                ? 64
                : stored_n;

            uint8_t trail =
                64 - lead - n;

            x = br_read(br, n);

            x <<= trail;

            prev_lead = lead;
            prev_trail = trail;

            has_prev_block = true;
        }

        uint64_t curr_bits =
            prev_bits ^ x;

        memcpy(&v,
               &curr_bits,
               sizeof(double));

        vals.push_back(v);

        prev_bits = curr_bits;
    }

    return vals;
}
inline void crc_update(uint64_t& crc, const char* data, size_t len) {
    for (size_t i = 0; i < len; i++) {
        crc ^= data[i];
        for (int j = 0; j < 8; j++)
            crc = (crc >> 1) ^ (-(crc & 1) & 0xEDB8832002388BDE);
    }
}
inline string chunk_file_writer(HeadBlock* hb, const string& metric_name){
    if(hb->timestamps.size() == 0) return "Empty timestamps.";
    string dirPath = "./data/" + metric_name;
    string filePath = dirPath + "/" + to_string(hb->timestamps[0])  + ".tmp";

    fs::create_directories(dirPath);

    ofstream file(filePath, ios::binary);
    if (!file.is_open()) return "Cannot open the file.";
    string magic("TSDB");
    uint32_t version =  2;
    uint32_t point_count = hb->timestamps.size();
    uint64_t first_timestamp = (hb->timestamps[0]);
    uint64_t last_timestamp = (hb->timestamps[hb->timestamps.size() - 1]);
    BitWriter ts_bitstream, val_bitstream;
    timestamp_encode(&ts_bitstream, hb->timestamps);
    value_encode(&val_bitstream, hb->values);
    uint32_t ts_bitstream_len = (ts_bitstream.buffer.size());
    uint32_t val_bitstream_len = (val_bitstream.buffer.size());
    uint64_t crc = 0xffffffffffffffff;
    file.write(magic.data(),magic.size());
    crc_update(crc, magic.data(),magic.size());
    file.write(reinterpret_cast<const char*>(&version), sizeof(uint32_t));
    crc_update(crc, reinterpret_cast<const char*>(&version),sizeof(uint32_t));
    file.write(reinterpret_cast<const char*>(&point_count),sizeof(uint32_t));
    crc_update(crc, reinterpret_cast<const char*>(&point_count),sizeof(uint32_t));
    file.write(reinterpret_cast<const char*>(&first_timestamp),sizeof(uint64_t));
    crc_update(crc, reinterpret_cast<const char*>(&first_timestamp),sizeof(uint64_t));
    file.write(reinterpret_cast<const char*>(&last_timestamp),sizeof(uint64_t));
    crc_update(crc, reinterpret_cast<const char*>(&last_timestamp),sizeof(uint64_t));
    file.write(reinterpret_cast<const char*>(&ts_bitstream_len), sizeof(uint32_t));
    crc_update(crc, reinterpret_cast<const char*>(&ts_bitstream_len), sizeof(uint32_t));
    file.write(reinterpret_cast<const char*>(&val_bitstream_len), sizeof(uint32_t));
    crc_update(crc, reinterpret_cast<const char*>(&val_bitstream_len), sizeof(uint32_t));
    for(uint32_t i = 0;i < ts_bitstream.buffer.size();i++){
        file.write(reinterpret_cast<const char*>(&ts_bitstream.buffer[i]), 1);
        crc_update(crc, reinterpret_cast<const char*>(&ts_bitstream.buffer[i]),1);
    }
    for(uint32_t i = 0;i < val_bitstream.buffer.size();i++){
        file.write(reinterpret_cast<const char*>(&val_bitstream.buffer[i]), 1);
        crc_update(crc, reinterpret_cast<const char*>(&val_bitstream.buffer[i]),1);
    }
    crc ^= 0xFFFFFFFFFFFFFFFF;
    file.write(reinterpret_cast<const char*>(&crc), sizeof(uint64_t));
    file.close();
    string newfilePath = dirPath + "/" + to_string(hb->timestamps[0])  + ".chunk";
    try {
        fs::rename(filePath, newfilePath);
        return "";
    } catch (const fs::filesystem_error& e) {
        return "Rename Error.";
    }
    return "";
}
inline vector<string> get_chunk_files(const string& dirPath) {
    vector<string> files;
    if (!fs::exists(dirPath) || fs::is_empty(dirPath)) {
        return files;
    }
    for (const auto& entry : fs::directory_iterator(dirPath)) {
        if (entry.is_regular_file()) {
            string path = entry.path().string();

            if (entry.path().extension() == ".tmp" ||
                entry.path().extension() == ".chunk") {
                files.push_back(path);
            }
        }
    }
    sort(files.begin(), files.end());

    return files;
}
inline uint64_t get_last_timestamp_from_chunk(const string& path) {
    ifstream file(path, ios::binary);
    if (!file.is_open()) return 0;

    file.seekg(20);

    uint64_t last_ts = 0;
    file.read(reinterpret_cast<char*>(&last_ts), sizeof(uint64_t));
    return last_ts;
}
inline pair<vector<uint64_t>, vector<double>> chunk_file_reader(const string& metric_name) {

    pair<vector<uint64_t>, vector<double>> res;

    string dirPath = "./data/" + metric_name;
    vector<string> filePaths = get_chunk_files(dirPath);

    for (const auto& path : filePaths) {
        try{
            ifstream file(path, ios::binary);
            if (!file.is_open()){
                cout << "cannot open file" << path << endl;
            }

            uint64_t crc = 0xFFFFFFFFFFFFFFFF;

            string magic(4, '\0');
            file.read(magic.data(),4);
            if (magic != "TSDB"){
                cout << "Wrong magic\n";
                continue;
            }
            crc_update(crc, magic.data(),4);

            uint32_t ver= 0;
            file.read(reinterpret_cast<char*>(&ver),sizeof(uint32_t));
            if (ver != 2){
                cout << "Wrong version\n";
                continue;
            }
            crc_update(crc, reinterpret_cast<char*>(&ver), sizeof(uint32_t));
            
            uint32_t count = 0;
            file.read(reinterpret_cast<char*>(&count),sizeof(uint32_t));
            crc_update(crc, reinterpret_cast<char*>(&count),sizeof(uint32_t));
            

            uint64_t first_ts = 0;
            uint64_t last_ts  = 0;
            file.read(reinterpret_cast<char*>(&first_ts),sizeof(uint64_t));
            crc_update(crc, reinterpret_cast<char*>(&first_ts),sizeof(uint64_t));

            file.read(reinterpret_cast<char*>(&last_ts),sizeof(uint64_t));
            crc_update(crc, reinterpret_cast<char*>(&last_ts),sizeof(uint64_t));

            uint32_t ts_len  = 0;
            uint32_t val_len = 0;
            file.read(reinterpret_cast<char*>(&ts_len),sizeof(uint32_t));
            crc_update(crc, reinterpret_cast<char*>(&ts_len),sizeof(uint32_t));
            file.read(reinterpret_cast<char*>(&val_len),sizeof(uint32_t));
            crc_update(crc, reinterpret_cast<char*>(&val_len),sizeof(uint32_t));

            vector<uint8_t> ts_buffer(ts_len);
            for(uint32_t i =0;i<ts_len;i++){
                file.read(reinterpret_cast<char*>(&ts_buffer[i]),sizeof(uint8_t));
                crc_update(crc, reinterpret_cast<char*>(&ts_buffer[i]),sizeof(uint8_t));
            }

            BitReader ts_br(ts_buffer, 0);
            vector<uint64_t> timestamps =
                timestamp_decode(&ts_br, count);

            vector<uint8_t> val_buffer(val_len);
            for(uint32_t i =0;i<val_len;i++){
                file.read(reinterpret_cast<char*>(&val_buffer[i]),sizeof(uint8_t));
                crc_update(crc, reinterpret_cast<char*>(&val_buffer[i]),sizeof(uint8_t));
            }

            BitReader val_br(val_buffer, 0);
            vector<double> values =
                value_decode(&val_br, count);

            uint64_t stored_crc;
            file.read(reinterpret_cast<char*>(&stored_crc), 8);

            crc ^= 0xFFFFFFFFFFFFFFFF;

            if (stored_crc != crc) {
                cout << "❌ CRC mismatch: " << path << endl;
                continue;
            }

            res.first.insert(res.first.end(),
                             timestamps.begin(),
                             timestamps.end());

            res.second.insert(res.second.end(),
                              values.begin(),
                              values.end());

            file.close();
        }
        catch (const exception& e) {
            cout << "Error: " << e.what() << "\n";
        }
        catch (...) {
            cout << "Unknown fatal error\n";
        }
    }

    return res;
}
inline void zstd_compress(const string& last_chunk){
    ifstream file(last_chunk, ios::binary | ios::ate);

    if (!file){
        cout << "Failed to open file\n";
        return;
    }

    streamsize size = file.tellg();
    file.seekg(0, ios::beg);

    vector<uint8_t> buffer(size);

    if (!file.read(reinterpret_cast<char*>(buffer.data()), size)){
        cout << "Failed to read file\n";
        return;
    }
    BitWriter bw;
    const int window = 16;
    uint32_t i = 0;
    bw_write(&bw, size, 64);
    while(i < window && i < size){
        bw_write(&bw, buffer[i], 8);
        i++;
    }
    while(i < size){
        uint32_t max_offset = 0, max_len = 0;
        for(uint32_t j = i - 1; j > i - window;j--){
            uint32_t k = j, h = i;
            uint32_t matched = 0;
            while(buffer[h] == buffer[k] && k < i){
                matched++;
                h++;
                k++;
            }
            if(matched > max_len){
                max_len = matched;
                max_offset = i - j;
            }
        }
        if(max_len > 2){
            bw_write(&bw, 0b1, 1);
            bw_write(&bw, max_len, 6);
            
            bw_write(&bw, max_offset, 6);
            i+=max_len;
        }
        else{
            bw_write(&bw, 0b0, 1);
            bw_write(&bw, buffer[i], 8);
            i++;
        }
    }
    bw_flush(&bw);

    fs::path p(last_chunk);
    string coarser_path = (p.parent_path() / "coarser" / p.filename()).string();
    ofstream fw(coarser_path, ios::binary);
    fw.write(reinterpret_cast<const char*>(bw.buffer.data()), bw.buffer.size());
}
inline std::vector<uint8_t> zstd_decompress(const std::string& input_path) {
    std::ifstream file(input_path, std::ios::binary | std::ios::ate);
    std::vector<uint8_t> res;

    if (!file) {
        std::cout << "Failed to open file\n";
        return res;
    }

    std::streamsize size = file.tellg();
    file.seekg(0, std::ios::beg);

    std::vector<uint8_t> buffer(size);

    if (!file.read(reinterpret_cast<char*>(buffer.data()), size)) {
        std::cout << "Failed to read file\n";
        return res;
    }

    BitReader br(buffer);

    uint32_t original_size = br_read(&br, 64);

    const int window = 16;

    for (int i = 0; i < window && res.size() < original_size; i++) {
        res.push_back(br_read(&br, 8));
    }

    while (res.size() < original_size) {
        uint8_t flag = br_read(&br, 1);
        if (flag == 0) {
            uint8_t lit = br_read(&br, 8);
            res.push_back(lit);
        } 
        else {
            uint32_t len = br_read(&br, 6);
            uint32_t off = br_read(&br, 6);

            uint32_t start = res.size() - off;

            for (uint32_t j = 0; j < len && res.size() < original_size; j++) {
                res.push_back(res[start + j]);
            }
        }
    }

    return res;
}
