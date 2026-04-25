#pragma once
#pragma once

#include <vector>
#include <cstdint>

std::vector<std::vector<uint64_t>> matmul_mod(
    const std::vector<std::vector<uint32_t>>& A,
    const std::vector<std::vector<uint32_t>>& B,
    uint64_t mod
);