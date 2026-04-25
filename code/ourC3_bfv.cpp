#include "ourC3_bfv.h"
#include <algorithm>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <random>
#include <sstream>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>
#include <fstream>

struct unitP
{
    std::vector<uint8_t> data;
};

namespace ourc3_bfv
{
    namespace
    {
        using Clock = std::chrono::high_resolution_clock;

        inline double elapsed_ms(const Clock::time_point &start, const Clock::time_point &end)
        {
            return std::chrono::duration<double, std::milli>(end - start).count();
        }

        class Simulator
        {
        public:
            explicit Simulator(const SimulationConfig &config) : cfg_(config)
            {
                if (cfg_.poly_modulus_degree != 8192)
                {
                    throw std::invalid_argument("This implementation is fixed to N = 8192.");
                }
                if (cfg_.plain_modulus != 65537ULL)
                {
                    throw std::invalid_argument("This implementation is fixed to t = 65537.");
                }
                if (cfg_.query_bytes != 32)
                {
                    throw std::invalid_argument("This implementation is fixed to 32 query bytes.");
                }
                initialize_he();
            }

            bool run(TimeReport &time_report, CommReport &comm_report)
            {
                auto offline_start = Clock::now();
                //cfg_.cols = 31121;
                fill_matrix();
                auto offline_end = Clock::now();
                time_report.server_offline_ms += elapsed_ms(offline_start, offline_end);

                auto client_start = Clock::now();
                const std::size_t target_row = sample_target_row();
                std::size_t aligned_start = 0;
                const std::vector<std::uint8_t> needle = choose_needle(target_row, aligned_start);
                QueryPack query = build_query_pack(target_row, needle);
                auto client_mid = Clock::now();
                time_report.client_query_encrypt_ms += elapsed_ms(client_start, client_mid);
                time_report.client_ms += time_report.client_query_encrypt_ms;

                comm_report.client_to_server_kb = static_cast<double>(
                                                      serialized_size_bytes(query.selector) +
                                                      serialized_size_bytes(query.compact_E) + sizeof(std::size_t)) /
                                                  1024.0;

                auto server_start = Clock::now();

                const std::size_t block_cols = ceil_div(cfg_.cols, row_size_);
                const std::size_t active_block_row = query.target_block_row;

                std::vector<seal::Ciphertext> selector_baby_rotations =
                    precompute_selector_baby_rotations(query.selector, row_size_ / 2);

                std::vector<seal::Ciphertext> ans0(block_cols);
                for (std::size_t bc = 0; bc < block_cols; ++bc)
                {
                    BlockPlan plan = build_block_plan(active_block_row * row_size_, bc * row_size_);
                    ans0[bc] = select_row_from_block_bsgs(selector_baby_rotations, plan);
                }

                verify_row_extraction(ans0, target_row, query.local_row);

                std::vector<seal::Ciphertext> compacted_ans0 = compact_ciphertexts(ans0);

                const std::vector<std::uint64_t> random_mask_slots = build_random_mask_slots();
                seal::Plaintext random_mask_plain;
                batch_encoder_->encode(random_mask_slots, random_mask_plain);

                std::vector<seal::Ciphertext> ans(compacted_ans0.size());
                for (std::size_t j = 0; j < compacted_ans0.size(); ++j)
                {
                    ans[j] = compacted_ans0[j];
                    evaluator_->sub_inplace(ans[j], query.compact_E);
                    evaluator_->multiply_plain_inplace(ans[j], random_mask_plain);
                }

                auto server_end = Clock::now();
                time_report.server_ms += elapsed_ms(server_start, server_end);

                std::size_t total_ans_bytes = 0;
                for (std::size_t j = 0; j < ans.size(); ++j)
                {
                    total_ans_bytes += serialized_size_bytes(ans[j]);
                }
                comm_report.server_to_client_kb = (static_cast<double>(total_ans_bytes) + cfg_.rows * 64 / 8) / 1024.0;

                auto client_finish_start = Clock::now();
                const std::vector<std::uint64_t> recovered_diff = reconstruct_difference_row(ans, target_row);
                const bool found = detect_aligned_zero_window(recovered_diff);
                auto client_finish_end = Clock::now();
                time_report.client_decrypt_detect_ms += elapsed_ms(client_finish_start, client_finish_end);
                time_report.client_ms += time_report.client_decrypt_detect_ms;

                const bool expected = detect_aligned_match_plain(flat_row_view(target_row), needle);
                if (found != expected)
                {
                    throw std::runtime_error(
                        "Correctness check failed: encrypted result does not match plaintext detection.");
                }

                return found;
            }

        private:
            struct QueryPack
            {
                std::size_t target_row = 0;
                std::size_t target_block_row = 0;
                std::size_t local_row = 0;
                std::vector<std::uint8_t> needle;
                seal::Ciphertext selector;
                seal::Ciphertext compact_E;
            };

            struct BlockPlan
            {
                std::size_t block_row_start = 0;
                std::size_t block_col_start = 0;
                std::size_t block_height = 0;
                std::size_t block_width = 0;
                std::size_t pair_count = 0;
                std::size_t used_baby_step = 0;
                std::size_t giant_count = 0;
                std::vector<seal::Plaintext> pair_plaintexts;
            };

            SimulationConfig cfg_;
            std::size_t slot_count_ = 0;
            std::size_t row_size_ = 0;
            std::vector<std::uint8_t> A_;

            std::shared_ptr<seal::SEALContext> context_;
            seal::PublicKey public_key_;
            seal::SecretKey secret_key_;
            seal::GaloisKeys galois_keys_;
            std::unique_ptr<seal::Encryptor> encryptor_;
            std::unique_ptr<seal::Evaluator> evaluator_;
            std::unique_ptr<seal::Decryptor> decryptor_;
            std::unique_ptr<seal::BatchEncoder> batch_encoder_;
            seal::Ciphertext zero_cipher_;

            static std::size_t ceil_div(std::size_t a, std::size_t b)
            {
                return (a + b - 1) / b;
            }

            std::size_t at(std::size_t r, std::size_t c) const
            {
                return static_cast<std::size_t>(A_[r * cfg_.cols + c]);
            }

            std::vector<std::uint8_t> flat_row_view(std::size_t row_index) const
            {
                std::vector<std::uint8_t> row(cfg_.cols, 0);
                const std::size_t base = row_index * cfg_.cols;
                std::copy(
                    A_.begin() + static_cast<std::ptrdiff_t>(base),
                    A_.begin() + static_cast<std::ptrdiff_t>(base + cfg_.cols), row.begin());
                return row;
            }

            void initialize_he()
            {
                seal::EncryptionParameters parms(seal::scheme_type::BFV);
                parms.set_poly_modulus_degree(cfg_.poly_modulus_degree);
                parms.set_coeff_modulus(seal::CoeffModulus::BFVDefault(cfg_.poly_modulus_degree));
                parms.set_plain_modulus(cfg_.plain_modulus);

                context_ = seal::SEALContext::Create(parms);
                if (!context_->parameters_set())
                {
                    throw std::runtime_error("SEALContext rejected the parameter set.");
                }

                seal::KeyGenerator keygen(context_);
                secret_key_ = keygen.secret_key();
                public_key_ = keygen.public_key();
                galois_keys_ = keygen.galois_keys_local();

                encryptor_.reset(new seal::Encryptor(context_, public_key_));
                evaluator_.reset(new seal::Evaluator(context_));
                decryptor_.reset(new seal::Decryptor(context_, secret_key_));
                batch_encoder_.reset(new seal::BatchEncoder(context_));

                slot_count_ = batch_encoder_->slot_count();
                row_size_ = slot_count_ / 2;
                if (row_size_ != 4096)
                {
                    throw std::runtime_error("Unexpected row size; expected 4096.");
                }

                std::vector<std::uint64_t> zero_slots(slot_count_, 0ULL);
                seal::Plaintext zero_plain;
                batch_encoder_->encode(zero_slots, zero_plain);
                encryptor_->encrypt(zero_plain, zero_cipher_);
            }

            void fill_matrix()
            {
                const std::size_t total = cfg_.rows * cfg_.cols;
                A_.assign(total, 0);
                std::mt19937_64 rng(cfg_.seed);
                std::uniform_int_distribution<int> dist(0, 255);
                for (std::size_t i = 0; i < total; ++i)
                {
                    A_[i] = static_cast<std::uint8_t>(dist(rng));
                }
            }

            std::size_t sample_target_row() const
            {
                int mn = 1.0 * log10f(cfg_.rows) / log10f(2);
                for (int i = 0; i < mn; i++)
                {
                    int l = 0xfffffff + 1;
                } // moni erfen chazhao
                std::mt19937_64 rng(cfg_.seed ^ 0xA5A5A5A5A5A5A5A5ULL);
                std::uniform_int_distribution<std::size_t> dist(0, cfg_.rows - 1);
                return dist(rng);
            }

            std::vector<std::uint8_t> choose_needle(std::size_t row_index, std::size_t &aligned_start) const
            {
                if (cfg_.cols < cfg_.query_bytes)
                {
                    throw std::runtime_error("The matrix has fewer than 32 columns.");
                }

                std::mt19937_64 rng(cfg_.seed ^ 0x9E3779B97F4A7C15ULL ^ static_cast<std::uint64_t>(row_index));
                const std::size_t aligned_choices = (cfg_.cols - cfg_.query_bytes) / cfg_.query_bytes + 1;
                std::uniform_int_distribution<std::size_t> dist(0, aligned_choices - 1);
                aligned_start = dist(rng) * cfg_.query_bytes;

                std::vector<std::uint8_t> needle(cfg_.query_bytes, 0);
                if (cfg_.force_positive)
                {
                    for (std::size_t k = 0; k < cfg_.query_bytes; ++k)
                    {
                        needle[k] = A_[row_index * cfg_.cols + aligned_start + k];
                    }
                    return needle;
                }

                std::uniform_int_distribution<int> byte_dist(0, 255);
                for (;;)
                {
                    for (std::size_t k = 0; k < cfg_.query_bytes; ++k)
                    {
                        needle[k] = static_cast<std::uint8_t>(byte_dist(rng));
                    }
                    if (!detect_aligned_match_plain(flat_row_view(row_index), needle))
                    {
                        return needle;
                    }
                }
            }

            QueryPack build_query_pack(std::size_t target_row, const std::vector<std::uint8_t> &needle) const
            {
                QueryPack pack;
                pack.target_row = target_row;
                pack.target_block_row = target_row / row_size_;
                pack.local_row = target_row % row_size_;
                pack.needle = needle;

                std::vector<std::uint64_t> selector_slots(slot_count_, 0ULL);
                selector_slots[pack.local_row] = 1ULL;
                selector_slots[row_size_ + pack.local_row] = 1ULL;
                seal::Plaintext selector_plain;
                batch_encoder_->encode(selector_slots, selector_plain);
                encryptor_->encrypt(selector_plain, pack.selector);

                // Compact-aware query ciphertext.
                // It only populates the useful slots that survive the r2 -> r2/2 compression.
                std::vector<std::uint64_t> e_slots(slot_count_, 0ULL);
                const std::size_t pair_capacity = row_size_ / 2;
                for (std::size_t p = 0; p < pair_capacity; ++p)
                {
                    const std::size_t pos0 = (pack.local_row + row_size_ - p) % row_size_;
                    e_slots[pos0] = static_cast<std::uint64_t>(needle[(2 * p) % needle.size()]);
                    e_slots[row_size_ + pos0] = static_cast<std::uint64_t>(needle[(2 * p + 1) % needle.size()]);

                    const std::size_t pos1 = (pack.local_row + row_size_ / 2 + row_size_ - p) % row_size_;
                    e_slots[pos1] = static_cast<std::uint64_t>(needle[(2 * p) % needle.size()]);
                    e_slots[row_size_ + pos1] = static_cast<std::uint64_t>(needle[(2 * p + 1) % needle.size()]);
                }
                seal::Plaintext e_plain;
                batch_encoder_->encode(e_slots, e_plain);
                encryptor_->encrypt(e_plain, pack.compact_E);

                return pack;
            }

            std::vector<seal::Ciphertext> precompute_selector_baby_rotations(
                const seal::Ciphertext &selector_ct, std::size_t max_pair_count) const
            {
                const std::size_t used_baby = std::max<std::size_t>(1, std::min(cfg_.baby_step, max_pair_count));
                std::vector<seal::Ciphertext> baby(used_baby);
                baby[0] = selector_ct;
                for (std::size_t j = 1; j < used_baby; ++j)
                {
                    evaluator_->rotate_rows(selector_ct, static_cast<int>(j), galois_keys_, baby[j]);
                }
                return baby;
            }

            BlockPlan build_block_plan(std::size_t block_row_start, std::size_t block_col_start) const
            {
                BlockPlan plan;
                plan.block_row_start = block_row_start;
                plan.block_col_start = block_col_start;
                plan.block_height = std::min(row_size_, cfg_.rows - block_row_start);
                plan.block_width = std::min(row_size_, cfg_.cols - block_col_start);
                plan.pair_count = ceil_div(plan.block_width, static_cast<std::size_t>(2));
                plan.used_baby_step = std::max<std::size_t>(1, std::min(cfg_.baby_step, plan.pair_count));
                plan.giant_count = (plan.pair_count + plan.used_baby_step - 1) / plan.used_baby_step;
                plan.pair_plaintexts.resize(plan.pair_count);

                std::vector<std::uint64_t> slots(slot_count_, 0ULL);
                for (std::size_t p = 0; p < plan.pair_count; ++p)
                {
                    std::fill(slots.begin(), slots.end(), 0ULL);
                    const std::size_t j = p % plan.used_baby_step;
                    const std::size_t even_local_col = 2 * p;
                    const std::size_t odd_local_col = even_local_col + 1;

                    for (std::size_t pos = 0; pos < row_size_; ++pos)
                    {
                        const std::size_t local_src_row = (pos + j) % row_size_;
                        if (local_src_row >= plan.block_height)
                        {
                            continue;
                        }
                        const std::size_t global_row = block_row_start + local_src_row;
                        if (even_local_col < plan.block_width)
                        {
                            slots[pos] = static_cast<std::uint64_t>(at(global_row, block_col_start + even_local_col));
                        }
                        if (odd_local_col < plan.block_width)
                        {
                            slots[row_size_ + pos] =
                                static_cast<std::uint64_t>(at(global_row, block_col_start + odd_local_col));
                        }
                    }

                    batch_encoder_->encode(slots, plan.pair_plaintexts[p]);
                }

                return plan;
            }

            seal::Ciphertext select_row_from_block_bsgs(
                const std::vector<seal::Ciphertext> &selector_baby_rotations, const BlockPlan &plan) const
            {
                if (plan.pair_count == 0)
                {
                    return zero_cipher_;
                }

                bool total_init = false;
                seal::Ciphertext total;

                for (std::size_t giant = 0; giant < plan.giant_count; ++giant)
                {
                    bool giant_init = false;
                    seal::Ciphertext giant_sum;

                    for (std::size_t j = 0; j < plan.used_baby_step; ++j)
                    {
                        const std::size_t p = giant * plan.used_baby_step + j;
                        if (p >= plan.pair_count)
                        {
                            break;
                        }

                        seal::Ciphertext term;
                        evaluator_->multiply_plain(selector_baby_rotations[j], plan.pair_plaintexts[p], term);
                        if (!giant_init)
                        {
                            giant_sum = term;
                            giant_init = true;
                        }
                        else
                        {
                            evaluator_->add_inplace(giant_sum, term);
                        }
                    }

                    if (!giant_init)
                    {
                        continue;
                    }

                    const std::size_t giant_shift = giant * plan.used_baby_step;
                    if (giant_shift != 0)
                    {
                        evaluator_->rotate_rows_inplace(giant_sum, static_cast<int>(giant_shift), galois_keys_);
                    }

                    if (!total_init)
                    {
                        total = giant_sum;
                        total_init = true;
                    }
                    else
                    {
                        evaluator_->add_inplace(total, giant_sum);
                    }
                }

                return total_init ? total : zero_cipher_;
            }

            std::vector<seal::Ciphertext> compact_ciphertexts(const std::vector<seal::Ciphertext> &ans0) const
            {
                std::vector<seal::Ciphertext> compacted;
                compacted.reserve((ans0.size() + 1) / 2);

                const int half_row = static_cast<int>(row_size_ / 2);
                for (std::size_t idx = 0; idx < ans0.size(); idx += 2)
                {
                    seal::Ciphertext merged = ans0[idx];
                    if (idx + 1 < ans0.size())
                    {
                        seal::Ciphertext shifted = ans0[idx + 1];
                        evaluator_->rotate_rows_inplace(shifted, half_row, galois_keys_);
                        evaluator_->add_inplace(merged, shifted);
                    }
                    compacted.push_back(merged);
                }
                return compacted;
            }

            std::vector<std::uint64_t> decode_ciphertext(const seal::Ciphertext &ct) const
            {
                seal::Plaintext plain;
                decryptor_->decrypt(ct, plain);
                std::vector<std::uint64_t> slots;
                batch_encoder_->decode(plain, slots);
                return slots;
            }

            std::vector<std::uint64_t> reconstruct_row_from_ans0(
                const std::vector<seal::Ciphertext> &ans0, std::size_t target_row, std::size_t local_row) const
            {
                std::vector<std::uint64_t> recovered(cfg_.cols, 0ULL);
                const std::size_t block_cols = ans0.size();

                for (std::size_t bc = 0; bc < block_cols; ++bc)
                {
                    const std::vector<std::uint64_t> slots = decode_ciphertext(ans0[bc]);
                    const std::size_t block_col_start = bc * row_size_;
                    const std::size_t width = std::min(row_size_, cfg_.cols - block_col_start);
                    const std::size_t pair_count = ceil_div(width, static_cast<std::size_t>(2));

                    for (std::size_t p = 0; p < pair_count; ++p)
                    {
                        const std::size_t pos = (local_row + row_size_ - p) % row_size_;
                        const std::size_t even_col = block_col_start + 2 * p;
                        const std::size_t odd_col = even_col + 1;
                        recovered[even_col] = slots[pos] % cfg_.plain_modulus;
                        if (odd_col < cfg_.cols)
                        {
                            recovered[odd_col] = slots[row_size_ + pos] % cfg_.plain_modulus;
                        }
                    }
                }

                (void)target_row;
                return recovered;
            }

            void verify_row_extraction(
                const std::vector<seal::Ciphertext> &ans0, std::size_t target_row, std::size_t local_row) const
            {
                const std::vector<std::uint64_t> recovered = reconstruct_row_from_ans0(ans0, target_row, local_row);
                for (std::size_t c = 0; c < cfg_.cols; ++c)
                {
                    if (recovered[c] != static_cast<std::uint64_t>(at(target_row, c)))
                    {
                        throw std::runtime_error("Row extraction correctness check failed.");
                    }
                }
            }

            std::vector<std::uint64_t> reconstruct_difference_row(
                const std::vector<seal::Ciphertext> &compacted, std::size_t target_row) const
            {
                std::vector<std::uint64_t> recovered(cfg_.cols, 0ULL);
                const std::size_t local_row = target_row % row_size_;
                const std::size_t half_row = row_size_ / 2;
                const std::size_t block_cols = ceil_div(cfg_.cols, row_size_);

                for (std::size_t compact_idx = 0; compact_idx < compacted.size(); ++compact_idx)
                {
                    const std::vector<std::uint64_t> slots = decode_ciphertext(compacted[compact_idx]);

                    const std::size_t block0 = 2 * compact_idx;
                    const std::size_t block1 = block0 + 1;

                    if (block0 < block_cols)
                    {
                        const std::size_t block_col_start = block0 * row_size_;
                        const std::size_t width = std::min(row_size_, cfg_.cols - block_col_start);
                        const std::size_t pair_count = ceil_div(width, static_cast<std::size_t>(2));
                        for (std::size_t p = 0; p < pair_count; ++p)
                        {
                            const std::size_t pos = (local_row + row_size_ - p) % row_size_;
                            const std::size_t even_col = block_col_start + 2 * p;
                            const std::size_t odd_col = even_col + 1;
                            recovered[even_col] = slots[pos] % cfg_.plain_modulus;
                            if (odd_col < cfg_.cols)
                            {
                                recovered[odd_col] = slots[row_size_ + pos] % cfg_.plain_modulus;
                            }
                        }
                    }

                    if (block1 < block_cols)
                    {
                        const std::size_t block_col_start = block1 * row_size_;
                        const std::size_t width = std::min(row_size_, cfg_.cols - block_col_start);
                        const std::size_t pair_count = ceil_div(width, static_cast<std::size_t>(2));
                        for (std::size_t p = 0; p < pair_count; ++p)
                        {
                            const std::size_t pos = (local_row + half_row + row_size_ - p) % row_size_;
                            const std::size_t even_col = block_col_start + 2 * p;
                            const std::size_t odd_col = even_col + 1;
                            recovered[even_col] = slots[pos] % cfg_.plain_modulus;
                            if (odd_col < cfg_.cols)
                            {
                                recovered[odd_col] = slots[row_size_ + pos] % cfg_.plain_modulus;
                            }
                        }
                    }
                }

                return recovered;
            }

            std::vector<std::uint64_t> build_random_mask_slots() const
            {
                std::mt19937_64 rng(cfg_.seed ^ 0xD1B54A32D192ED03ULL);
                std::uniform_int_distribution<std::uint64_t> dist(1ULL, cfg_.plain_modulus - 1ULL);
                std::vector<std::uint64_t> slots(slot_count_, 1ULL);
                for (std::size_t i = 0; i < slot_count_; ++i)
                {
                    slots[i] = dist(rng);
                }
                return slots;
            }

            bool detect_aligned_zero_window(const std::vector<std::uint64_t> &values) const
            {
                const std::size_t q = cfg_.query_bytes;
                if (values.size() < q)
                {
                    return false;
                }
                for (std::size_t start = 0; start + q <= values.size(); start += q)
                {
                    bool ok = true;
                    for (std::size_t k = 0; k < q; ++k)
                    {
                        if ((values[start + k] % cfg_.plain_modulus) != 0ULL)
                        {
                            ok = false;
                            break;
                        }
                    }
                    if (ok)
                    {
                        return true;
                    }
                }
                return false;
            }

            bool detect_aligned_match_plain(
                const std::vector<std::uint8_t> &row, const std::vector<std::uint8_t> &needle) const
            {
                const std::size_t q = needle.size();
                if (row.size() < q)
                {
                    return false;
                }
                for (std::size_t start = 0; start + q <= row.size(); start += q)
                {
                    bool ok = true;
                    for (std::size_t k = 0; k < q; ++k)
                    {
                        if (row[start + k] != needle[k])
                        {
                            ok = false;
                            break;
                        }
                    }
                    if (ok)
                    {
                        return true;
                    }
                }
                return false;
            }

            std::size_t serialized_size_bytes(const seal::Ciphertext &ct) const
            {
                std::stringstream ss(std::ios::binary | std::ios::in | std::ios::out);
                ct.save(ss);
                return ss.str().size();
            }
        };

    } // namespace

    void getvectorA(std::vector<std::vector<std::uint8_t>> &A, std::size_t rows, std::size_t cols, std::uint64_t seed)
    {
        std::mt19937_64 rng(seed);
        std::uniform_int_distribution<int> dist(0, 255);
        A.assign(rows, std::vector<std::uint8_t>(cols, 0));
        for (std::size_t r = 0; r < rows; ++r)
        {
            for (std::size_t c = 0; c < cols; ++c)
            {
                A[r][c] = static_cast<std::uint8_t>(dist(rng));
            }
        }
    }

    bool simulate_ourC3_bfv(TimeReport &time_report, CommReport &comm_report, const SimulationConfig &config)
    {
        Simulator simulator(config);
        return simulator.run(time_report, comm_report);
    }

} // namespace ourc3_bfv
