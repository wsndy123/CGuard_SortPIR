#include "pipasetx.h"
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

int mainpppp(int argc, char *argv[])
{
    try
    {
        const std::string data_dir = (argc >= 2) ? argv[1] : R"(D:\KWPIR\test_data)";

        pipasetx::PipaSetXRunner runner(data_dir);
        const auto requests = pipasetx::PipaSetXRunner::default_requests();
        runner.prepare_all_datasets();

        std::filesystem::create_directories(data_dir);
        const auto report_path = std::filesystem::path(data_dir) / "pipasetx_results.txt";
        std::ofstream report(report_path, std::ios::trunc);
        if (!report)
        {
            throw std::runtime_error("failed to open result report file: " + report_path.string());
        }

#ifdef SEAL_USE_ZLIB
        std::cout << "SEAL_USE_ZLIB = ON\n";
#else
        std::cout << "SEAL_USE_ZLIB = OFF\n";
#endif
        std::cout << "Dataset directory: " << data_dir << "\n\n";

        std::vector<pipasetx::SelectionResult> selections;
        selections.reserve(requests.size());

        std::cout << "================ Fixed-parameter positive runs ================\n";
        for (const auto &req : requests)
        {
            const auto sel = runner.select_smallest_working(req);
            selections.push_back(sel);
            pipasetx::PipaSetXRunner::print_selection_result(sel);
        }

        report << "Pipa fixed-parameter experiments\n";
        report << "Data dir: " << data_dir << "\n\n";

        report << std::left << std::setw(12) << "N" << std::setw(10) << "n" << std::setw(10) << "base" << std::setw(12)
               << "plain_t" << std::setw(14) << "coeff_bits" << std::setw(8) << "B" << std::setw(8) << "l_B"
               << std::setw(12) << "ctct_muls" << std::setw(10) << "depth" << std::setw(16) << "total_comm"
               << std::setw(16) << "he_eval_ms" << '\n';

        for (const auto &sel : selections)
        {
            const auto &res = sel.positive_result;
            report << std::left << std::setw(12) << res.config.database_size << std::setw(10)
                   << res.config.poly_modulus_degree << std::setw(10) << res.config.base << std::setw(12)
                   << res.config.plain_modulus << std::setw(14) << res.config.coeff_modulus_total_bits() << std::setw(8)
                   << res.actual_segment_count << std::setw(8) << res.config.l_b << std::setw(12)
                   << res.actual_ct_ct_muls << std::setw(10) << res.actual_mul_depth << std::setw(16)
                   << pipasetx::PipaSetXRunner::format_bytes(res.comm.total_bytes()) << std::setw(16)
                   << res.time.homomorphic_eval_ms << '\n';
        }

        report << "\nDetailed per-request dump\n\n";
        for (const auto &sel : selections)
        {
            const auto &res = sel.positive_result;
            report << "===== " << res.config.name << " =====\n";
            report << "selected_poly_modulus_degree=" << res.config.poly_modulus_degree << '\n';
            report << "selected_base=" << res.config.base << '\n';
            report << "selected_plain_modulus=" << res.config.plain_modulus << '\n';
            report << "selected_coeff_modulus_bits="
                   << pipasetx::PipaSetXRunner::coeff_bits_to_string(res.config.coeff_modulus_bits) << '\n';
            report << "selected_coeff_modulus_total_bits=" << res.config.coeff_modulus_total_bits() << '\n';
            report << "note=" << res.config.note << '\n';
            report << "database_size=" << res.config.database_size << '\n';
            report << "B=" << res.actual_segment_count << '\n';
            report << "l_B=" << res.config.l_b << '\n';
            report << "ct_ct_muls=" << res.actual_ct_ct_muls << '\n';
            report << "mul_depth=" << res.actual_mul_depth << '\n';
            report << "bfv_params_bytes=" << res.comm.bfv_params_bytes << '\n';
            report << "relin_key_bytes=" << res.comm.relin_key_bytes << '\n';
            report << "hash_prefix_bytes=" << res.comm.hash_prefix_bytes << '\n';
            report << "account_ciphertexts_bytes=" << res.comm.account_ciphertexts_bytes << '\n';
            report << "result_ciphertext_bytes=" << res.comm.result_ciphertext_bytes << '\n';
            report << "total_comm_bytes=" << res.comm.total_bytes() << '\n';
            report << "bfv_param_gen_ms=" << res.time.bfv_param_gen_ms << '\n';
            report << "dbat_encode_ms=" << res.time.dbat_encode_ms << '\n';
            report << "homomorphic_eval_ms=" << res.time.homomorphic_eval_ms << '\n';
            report << "kg_ms=" << res.time.kg_ms << '\n';
            report << "account_hash_ms=" << res.time.account_hash_ms << '\n';
            report << "account_hash_enc_ms=" << res.time.account_encrypt_ms << '\n';
            report << "decrypt_ms=" << res.time.decrypt_ms << '\n';
            report << "positive_found=" << (sel.positive_result.found ? "true" : "false") << '\n';
            report << "negative_found=" << (sel.negative_result.found ? "true" : "false") << '\n';
            report << "attempt_count=" << sel.attempts.size() << '\n';
            for (std::size_t i = 0; i < sel.attempts.size(); ++i)
            {
                const auto &a = sel.attempts[i];
                report << "attempt[" << i << "]="
                       << "n=" << a.config.poly_modulus_degree << ",base=" << a.config.base
                       << ",coeff=" << pipasetx::PipaSetXRunner::coeff_bits_to_string(a.config.coeff_modulus_bits)
                       << ",success=" << (a.success ? "true" : "false");
                if (!a.error_message.empty())
                {
                    report << ",error=" << a.error_message;
                }
                report << '\n';
            }
            report << '\n';
        }

        std::cout << "Detailed report written to: " << report_path.string() << "\n";
        return 0;
    }
    catch (const std::exception &e)
    {
        std::cerr << "ERROR: " << e.what() << std::endl;
        return 1;
    }
}
