#ifndef OURC3_BFV_H
#define OURC3_BFV_H

#include "seal/seal.h"
#include <cstddef>
#include <cstdint>
#include <vector>

namespace ourc3_bfv
{
    struct TimeReport
    {
        double server_offline_ms = 0.0;
        double client_query_encrypt_ms = 0.0;
        double client_decrypt_detect_ms = 0.0;
        double client_ms = 0.0;
        double server_ms = 0.0;
    };

    struct CommReport
    {
        double server_to_client_kb = 0.0;
        double client_to_server_kb = 0.0;
    };

    struct SimulationConfig
    {
        std::size_t rows = 33333;
        std::size_t cols = 32221;
        std::size_t poly_modulus_degree = 8192;
        std::uint64_t plain_modulus = 65537;
        std::size_t query_bytes = 32;
        std::size_t baby_step = 64;
        std::uint64_t seed = 20260413ULL;
        bool force_positive = true;
    };

    void getvectorA(
        std::vector<std::vector<std::uint8_t>> &A, std::size_t rows = 33333, std::size_t cols = 32221,
        std::uint64_t seed = 20260413ULL);

    bool simulate_ourC3_bfv(
        TimeReport &time_report, CommReport &comm_report, const SimulationConfig &config = SimulationConfig());

} // namespace ourc3_bfv

#endif // OURC3_BFV_H
