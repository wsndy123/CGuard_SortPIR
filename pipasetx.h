#pragma once
#include "seal/util/config.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace pipasetx
{
    struct ExperimentRequest
    {
        std::string name;
        std::size_t database_size = 0;
    };

    struct ExperimentConfig
    {
        std::string name;
        std::size_t database_size = 0;
        std::size_t poly_modulus_degree = 8192;
        std::size_t hash_bits = 256;
        std::size_t compared_bits = 256;
        std::size_t base = 256;
        std::size_t l_b = 32;
        std::uint64_t plain_modulus = 2277377;
        std::vector<int> coeff_modulus_bits;
        std::string note;

        std::size_t segment_count() const;
        std::size_t expected_ct_ct_muls() const;
        std::size_t expected_mul_depth() const;
        int coeff_modulus_total_bits() const;
    };

    struct CommStats
    {
        std::size_t bfv_params_bytes = 0;
        std::size_t relin_key_bytes = 0;
        std::size_t hash_prefix_bytes = 2;
        std::size_t account_ciphertexts_bytes = 0; // includes ct_sum
        std::size_t result_ciphertext_bytes = 0;

        std::size_t total_bytes() const;
    };

    struct TimeStats
    {
        double bfv_param_gen_ms = 0.0;
        double dbat_encode_ms = 0.0;
        double homomorphic_eval_ms = 0.0;
        double kg_ms = 0.0;
        double account_hash_ms = 0.0;
        double account_encrypt_ms = 0.0;
        double decrypt_ms = 0.0;
    };

    struct RunResult
    {
        ExperimentConfig config;
        CommStats comm;
        TimeStats time;

        bool positive_query = true;
        bool found = false;
        bool zlib_enabled = false;

        std::string target_username;
        std::string target_password;

        std::size_t actual_segment_count = 0;
        std::size_t actual_ct_ct_muls = 0;
        std::size_t actual_mul_depth = 0;
    };

    struct AttemptResult
    {
        ExperimentConfig config;
        bool success = false;
        std::string error_message;
    };

    struct SelectionResult
    {
        ExperimentConfig selected_config;
        RunResult positive_result;
        RunResult negative_result;
        std::vector<AttemptResult> attempts;
    };

    class PipaSetXRunner
    {
    public:
        explicit PipaSetXRunner(std::string data_dir);

        void prepare_all_datasets() const;
        void prepare_dataset(std::size_t database_size) const;
        RunResult run_once(const ExperimentConfig &config, bool positive_query) const;
        SelectionResult select_smallest_working(const ExperimentRequest &request) const;

        static std::vector<ExperimentRequest> default_requests();
        static std::vector<ExperimentConfig> candidate_configs(const ExperimentRequest &request);
        static std::string format_bytes(std::size_t bytes);
        static std::string coeff_bits_to_string(const std::vector<int> &bits);
        static void print_run_result(const RunResult &result);
        static void print_selection_result(const SelectionResult &selection);

    private:
        struct DatasetInfo
        {
            std::string username;
            std::string password;
            std::array<std::uint8_t, 32> target_digest{};
            std::array<std::uint8_t, 32> near_digest{};
        };

        std::filesystem::path dataset_bin_path(std::size_t database_size) const;
        std::filesystem::path dataset_meta_path(std::size_t database_size) const;

        void generate_and_save_dataset(std::size_t database_size) const;
        DatasetInfo load_dataset_info(std::size_t database_size) const;

        std::filesystem::path data_dir_;
    };

} // namespace pipasetx
