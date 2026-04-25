#include "ourC3_bfv.h"
#include <exception>
#include <iostream>

int main()
{
    try
    {
        ourc3_bfv::SimulationConfig config;
        config.rows = 2881;
        config.cols = 2912;
        config.poly_modulus_degree = 8192;
        config.plain_modulus = 65537;
        config.query_bytes = 32;
        config.baby_step = 64;
        config.seed = 20260413ULL;
        config.force_positive = true;

        ourc3_bfv::TimeReport time_report;
        ourc3_bfv::CommReport comm_report;

        const bool found = ourc3_bfv::simulate_ourC3_bfv(time_report, comm_report, config);

        std::cout << "found = " << (found ? "true" : "false") << std::endl;
        std::cout << "server_offline_time_ms = " << time_report.server_offline_ms << std::endl;
        std::cout << "client_query_encrypt_time_ms = " << time_report.client_query_encrypt_ms << std::endl;
        std::cout << "client_decrypt_detect_time_ms = " << time_report.client_decrypt_detect_ms << std::endl;
        std::cout << "client_time_ms = " << time_report.client_ms << std::endl;
        std::cout << "server_time_ms = " << time_report.server_ms << std::endl;
        std::cout << "server_to_client_KB = " << comm_report.server_to_client_kb << std::endl;
        std::cout << "client_to_server_KB = " << comm_report.client_to_server_kb << std::endl;
    }
    catch (const std::exception &e)
    {
        std::cerr << "ERROR: " << e.what() << std::endl;
        return 1;
    }
    return 0;
}
