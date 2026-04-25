#pragma once
#include "macros.h"
#include "config.h"
#include <vector>

struct Segment {
    uint64 key;
    double slope;
    double intercept;

    uint64 predict(uint64 x) const {
        return (uint64)(slope * x + intercept);
    }
};

std::vector<Segment> build_PLA_model(const std::vector<uint64_t>& keys);

std::vector<std::vector<Segment>> buildPLA(std:: vector<uint64> keys);


uint64 askPLA(uint64 key, std::vector<std::vector<Segment>> levels);
uint64 askPLA0(const std::vector<Segment>& segs, uint64_t k);