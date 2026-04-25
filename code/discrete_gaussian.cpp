#include "discrete_gaussian.h"
#include <vector>
#include <random>

std::vector<uint32> sample_discrete_gaussian(uint32 n, double sigma = 6.4) {


    const int B = static_cast<int>(std::ceil(12.0 * sigma));

    std::vector<int> values;
    std::vector<double> weights;

    values.reserve(2 * B + 1);
    weights.reserve(2 * B + 1);

    const double denom = 2.0 * sigma * sigma;

    for (int x = -B; x <= B; x++) {
        values.push_back(x);
        weights.push_back(std::exp(-(static_cast<double>(x) * x) / denom));
    }

    static thread_local std::mt19937_64 rng(std::random_device{}());
    std::discrete_distribution<size_t> dist(weights.begin(), weights.end());

    std::vector<uint32> res;
    res.reserve(n);

    for (uint32 i = 0; i < n; i++) {
        int e = values[dist(rng)];

        // For q = 2^32, casting a negative int to uint32_t gives its value modulo 2^32.
        res.push_back(static_cast<uint32>(e));
    }

    return res;
}
/*
std::vector<uint32> sample_discrete_gaussian(uint32 n, double sigma) {
    std::mt19937_64 rng(std::random_device{}());
    std::uniform_int_distribution<int> dist(-1, 1);

    std::vector<uint32> res;
    res.reserve(n);

    for (uint32 i = 0; i < n; i++) {
        int number = dist(rng);
        if (number == -1) {
            //res.push_back(0);
            res.push_back(static_cast<uint32>(indexPIR::CIPHERMOD_q - 1));
        }
        else {
            //res.push_back(0);
            res.push_back(static_cast<uint32>(number)); // 0 或 1
        }
    }

    return res;

    /*

    std:: mt19937_64 rng(std:: random_device{}());

    std:: normal_distribution<double> dist(0.0, sigma);

    std:: vector<uint32> res;
    res.reserve(n);

    for (int i = 0; i < n; i++) {
        uint64 number = llround(dist(rng));
        //if (number < 0) number += indexPIR::CIPHERMOD_q;
        if (number % 3 == 0) number = 0;
        else if (number % 3 == 1) number = -1 + indexPIR::CIPHERMOD_q;
        else number = 1;
        //number = 0;
        res.push_back((uint32) number);
    }
        

    return res;
}*/