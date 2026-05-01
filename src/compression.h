#pragma once
#include<iostream>
#include<vector>
#include"../include/helpers.h"
using namespace std;
void bw_write(BitWriter* bw, uint64_t value, int n_bits ) {
    value = value << (64 - n_bits);
    while (n_bits > 0) {
        int free = (8 - bw->bits_filled);
        int can_have = free  > n_bits ? n_bits : free;
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
uint64_t br_read (BitReader* br , int n_bits){
    uint64_t res = 0;
    while(n_bits > 0){
        uint8_t x = br->buffer[br->byte_index];
        int to_read = n_bits > (8 - br->bit_index) ? (8 - br->bit_index) : n_bits;
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