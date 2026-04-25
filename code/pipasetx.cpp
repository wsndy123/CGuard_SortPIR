#include "pipasetx.h"
#include <algorithm>
#include <chrono>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <numeric>
#include <openssl/evp.h>
#include <seal/seal.h>
#include <sstream>
#include <stdexcept>

namespace pipasetx
{
    namespace
    {
        using Clock = std::chrono::high_resolution_clock;
        constexpr std::size_t kDigestBytes = 32;
        constexpr int kPbkdf2Iterations = 100000;
        constexpr char kDatasetMagic[] = "PIPASETX";
        constexpr char kPbkdf2Salt[] = "PIPASETX_FIXED_PBKDF2_SALT";

        template <typename Func>
        double measure_ms(Func &&func)
        {
            const auto start = Clock::now();
            func();
            const auto end = Clock::now();
            return std::chrono::duration<double, std::milli>(end - start).count();
        }

        bool is_zlib_enabled()
        {
#ifdef SEAL_USE_ZLIB
            return true;
#else
            return false;
#endif
        }

        std::size_t ceil_log2(std::size_t x)
        {
            if (x <= 1)
            {
                return 0;
            }
            std::size_t p = 0;
            std::size_t v = 1;
            while (v < x)
            {
                v <<= 1;
                ++p;
            }
            return p;
        }

        std::string hex_encode(const std::array<std::uint8_t, kDigestBytes> &arr)
        {
            std::ostringstream oss;
            oss << std::hex << std::setfill('0');
            for (auto b : arr)
            {
                oss << std::setw(2) << static_cast<unsigned>(b);
            }
            return oss.str();
        }

        std::array<std::uint8_t, kDigestBytes> hex_decode_32(const std::string &s)
        {
            if (s.size() != 64)
            {
                throw std::runtime_error("expected 64 hex chars for 32-byte digest");
            }
            std::array<std::uint8_t, kDigestBytes> out{};
            for (std::size_t i = 0; i < kDigestBytes; ++i)
            {
                out[i] = static_cast<std::uint8_t>(std::stoul(s.substr(2 * i, 2), nullptr, 16));
            }
            return out;
        }

        std::array<std::uint8_t, kDigestBytes> pbkdf2_sha256_256(const std::string &input)
        {
            std::array<std::uint8_t, kDigestBytes> out{};
            const auto *salt_ptr = reinterpret_cast<const unsigned char *>(kPbkdf2Salt);
            const int salt_len = static_cast<int>(sizeof(kPbkdf2Salt) - 1);
            const int ok = PKCS5_PBKDF2_HMAC(
                input.data(), static_cast<int>(input.size()), salt_ptr, salt_len, kPbkdf2Iterations, EVP_sha256(),
                static_cast<int>(out.size()), out.data());
            if (ok != 1)
            {
                throw std::runtime_error("PKCS5_PBKDF2_HMAC failed");
            }
            return out;
        }

        std::array<std::uint8_t, kDigestBytes> hash_username(const std::string &username)
        {
            return pbkdf2_sha256_256(username);
        }

        std::array<std::uint8_t, kDigestBytes> hash_account(const std::string &username, const std::string &password)
        {
            return pbkdf2_sha256_256(username + '\x1f' + password);
        }

        std::array<std::uint8_t, 2> two_byte_prefix_from_username(const std::string &username)
        {
            const auto h = hash_username(username);
            return { h[0], h[1] };
        }

        std::array<std::uint8_t, kDigestBytes> make_near_digest(
            const std::array<std::uint8_t, kDigestBytes> &base, std::uint8_t delta)
        {
            auto out = base;
            if (base[0] > static_cast<std::uint8_t>(255 - delta))
            {
                throw std::runtime_error("first digest byte too large for controlled delta construction");
            }
            out[0] = static_cast<std::uint8_t>(base[0] + delta);
            return out;
        }

        std::vector<std::uint64_t> compared_digits(
            const std::array<std::uint8_t, kDigestBytes> &digest, std::size_t compared_bits, std::size_t base)
        {
            if (base == 0 || (base & (base - 1)) != 0)
            {
                throw std::runtime_error("base must be a power of two");
            }
            std::size_t base_log2 = 0;
            while ((std::size_t(1) << base_log2) < base)
            {
                ++base_log2;
            }
            if (base_log2 == 0 || base_log2 > 16)
            {
                throw std::runtime_error("this implementation supports bases 2^k with 1 <= k <= 16");
            }
            if (compared_bits % base_log2 != 0)
            {
                throw std::runtime_error("compared_bits must be divisible by log2(base)");
            }
            if (compared_bits > digest.size() * 8)
            {
                throw std::runtime_error("compared_bits exceeds digest size");
            }

            const std::size_t digit_count = compared_bits / base_log2;
            std::vector<std::uint64_t> out;
            out.reserve(digit_count);
            for (std::size_t d = 0; d < digit_count; ++d)
            {
                std::uint64_t value = 0;
                for (std::size_t b = 0; b < base_log2; ++b)
                {
                    const std::size_t bit_index = d * base_log2 + b;
                    const std::size_t byte_index = bit_index / 8;
                    const int bit_in_byte = 7 - static_cast<int>(bit_index % 8);
                    const std::uint64_t bit = (digest[byte_index] >> bit_in_byte) & 1U;
                    value = (value << 1) | bit;
                }
                out.push_back(value);
            }
            return out;
        }

        std::uint64_t squared_sum_mod(const std::vector<std::uint64_t> &digits, std::uint64_t plain_modulus)
        {
            std::uint64_t acc = 0;
            for (auto d : digits)
            {
                acc = (acc + (d * d) % plain_modulus) % plain_modulus;
            }
            return acc;
        }

        std::vector<std::uint64_t> repeated_slots(std::size_t slot_count, std::uint64_t value)
        {
            return std::vector<std::uint64_t>(slot_count, value);
        }

        void check_batching_enabled(const std::shared_ptr<seal::SEALContext> &context)
        {
            if (!context || !context->first_context_data())
            {
                throw std::runtime_error("SEALContext is not valid");
            }
            if (!context->first_context_data()->qualifiers().using_batching)
            {
                throw std::runtime_error("Batching is not enabled for the selected plaintext modulus");
            }
        }

        std::size_t serialized_size_bytes(const seal::EncryptionParameters &obj)
        {
            std::stringstream ss(std::ios::in | std::ios::out | std::ios::binary);
            obj.save(ss);
            return ss.str().size();
        }

        template <typename SerializableLike>
        std::size_t serialized_size_bytes(const SerializableLike &obj)
        {
            std::stringstream ss(std::ios::in | std::ios::out | std::ios::binary);
            obj.save(ss);
            return ss.str().size();
        }

        template <typename SerializableLike>
        std::string serialize_to_string(const SerializableLike &obj)
        {
            std::stringstream ss(std::ios::in | std::ios::out | std::ios::binary);
            obj.save(ss);
            return ss.str();
        }

        ExperimentConfig make_cfg(
            const std::string &name, std::size_t db_size, std::size_t poly_degree, std::size_t base,
            std::size_t compared_bits, std::uint64_t plain_modulus, const std::vector<int> &coeff_bits,
            const std::string &note)
        {
            ExperimentConfig cfg;
            cfg.name = name;
            cfg.database_size = db_size;
            cfg.poly_modulus_degree = poly_degree;
            cfg.hash_bits = 256;
            cfg.compared_bits = compared_bits;
            cfg.base = base;
            std::size_t base_log2 = 0;
            while ((std::size_t(1) << base_log2) < base)
            {
                ++base_log2;
            }
            cfg.l_b = compared_bits / base_log2;
            cfg.plain_modulus = plain_modulus;
            cfg.coeff_modulus_bits = coeff_bits;
            cfg.note = note;
            return cfg;
        }

    } // namespace

    std::size_t ExperimentConfig::segment_count() const
    {
        return (database_size + poly_modulus_degree - 1) / poly_modulus_degree;
    }

    std::size_t ExperimentConfig::expected_ct_ct_muls() const
    {
        const auto b = segment_count();
        return b ? (b - 1) : 0;
    }

    std::size_t ExperimentConfig::expected_mul_depth() const
    {
        return ceil_log2(segment_count());
    }

    int ExperimentConfig::coeff_modulus_total_bits() const
    {
        return std::accumulate(coeff_modulus_bits.begin(), coeff_modulus_bits.end(), 0);
    }

    std::size_t CommStats::total_bytes() const
    {
        return bfv_params_bytes + relin_key_bytes + hash_prefix_bytes + account_ciphertexts_bytes +
               result_ciphertext_bytes;
    }

    PipaSetXRunner::PipaSetXRunner(std::string data_dir) : data_dir_(std::move(data_dir))
    {}

    std::vector<ExperimentRequest> PipaSetXRunner::default_requests()
    {
        return {
            { "N = 2^16", (1u << 16) },
            { "N = 2^18", (1u << 18) },
            { "N = 2^20", (1u << 20) },
            { "N = 2^22", (1u << 22) },
        };
    }

    std::vector<ExperimentConfig> PipaSetXRunner::candidate_configs(const ExperimentRequest &request)
    {
        const auto N = request.database_size;
        if (N == (1u << 16))
        {
            return { make_cfg(
                request.name, N, 8192, 256, 256, 2277377ULL, { 60, 60, 60, 38 },
                "fixed best configuration from pipasetx_results.txt for N=2^16") };
        }
        if (N == (1u << 18))
        {
            return { make_cfg(
                request.name, N, 16384, 65536, 256, 68718428161ULL, { 60, 60, 60, 60, 56, 30 },
                "fixed best configuration from pipasetx_results.txt for N=2^18") };
        }
        if (N == (1u << 20))
        {
            return { make_cfg(
                request.name, N, 32768, 65536, 256, 68718428161ULL, { 60, 60, 60, 60, 60, 60, 38 },
                "fixed best configuration from pipasetx_results.txt for N=2^20") };
        }
        if (N == (1u << 22))
        {
            return { make_cfg(
                request.name, N, 32768, 65536, 256, 68718428161ULL, { 60, 60, 60, 60, 60, 60, 60, 60, 60, 44, 30 },
                "fixed best configuration from pipasetx_results.txt for N=2^22") };
        }
        throw std::runtime_error("unsupported database size in candidate_configs");
    }

    std::string PipaSetXRunner::format_bytes(std::size_t bytes)
    {
        std::ostringstream oss;
        oss << std::fixed << std::setprecision(4);
        if (bytes >= (1ull << 20))
        {
            oss << (static_cast<double>(bytes) / (1024.0 * 1024.0)) << " MB";
        }
        else if (bytes >= (1ull << 10))
        {
            oss << (static_cast<double>(bytes) / 1024.0) << " KB";
        }
        else
        {
            oss << bytes << " B";
        }
        return oss.str();
    }

    std::string PipaSetXRunner::coeff_bits_to_string(const std::vector<int> &bits)
    {
        std::ostringstream oss;
        oss << "{";
        for (std::size_t i = 0; i < bits.size(); ++i)
        {
            if (i)
            {
                oss << ",";
            }
            oss << bits[i];
        }
        oss << "}";
        return oss.str();
    }

    std::filesystem::path PipaSetXRunner::dataset_bin_path(std::size_t database_size) const
    {
        return data_dir_ / ("pipasetx_" + std::to_string(database_size) + ".bin");
    }

    std::filesystem::path PipaSetXRunner::dataset_meta_path(std::size_t database_size) const
    {
        return data_dir_ / ("pipasetx_" + std::to_string(database_size) + ".meta.txt");
    }

    void PipaSetXRunner::prepare_all_datasets() const
    {
        for (const auto &request : default_requests())
        {
            prepare_dataset(request.database_size);
        }
    }

    void PipaSetXRunner::prepare_dataset(std::size_t database_size) const
    {
        std::filesystem::create_directories(data_dir_);
        if (std::filesystem::exists(dataset_bin_path(database_size)) &&
            std::filesystem::exists(dataset_meta_path(database_size)))
        {
            return;
        }
        generate_and_save_dataset(database_size);
    }

    void PipaSetXRunner::generate_and_save_dataset(std::size_t database_size) const
    {
        std::filesystem::create_directories(data_dir_);

        DatasetInfo info;
        for (std::size_t nonce = 0;; ++nonce)
        {
            info.username = "pipasetx_user_" + std::to_string(database_size) + "_" + std::to_string(nonce);
            info.password = "pipasetx_pwd_" + std::to_string(database_size) + "_" + std::to_string(nonce);
            info.target_digest = hash_account(info.username, info.password);
            if (info.target_digest[0] <= 252)
            {
                break;
            }
        }
        info.near_digest = make_near_digest(info.target_digest, 1);

        std::ofstream out(dataset_bin_path(database_size), std::ios::binary | std::ios::trunc);
        if (!out)
        {
            throw std::runtime_error("failed to create dataset file");
        }
        out.write(kDatasetMagic, sizeof(kDatasetMagic) - 1);
        const std::uint64_t n = static_cast<std::uint64_t>(database_size);
        out.write(reinterpret_cast<const char *>(&n), sizeof(n));
        for (std::size_t row = 0; row < database_size; ++row)
        {
            const auto &digest = (row == 0) ? info.target_digest : info.near_digest;
            out.write(reinterpret_cast<const char *>(digest.data()), static_cast<std::streamsize>(digest.size()));
        }
        if (!out)
        {
            throw std::runtime_error("failed while writing dataset file");
        }

        std::ofstream meta(dataset_meta_path(database_size), std::ios::trunc);
        if (!meta)
        {
            throw std::runtime_error("failed to create metadata file");
        }
        meta << info.username << '\n';
        meta << info.password << '\n';
        meta << hex_encode(info.target_digest) << '\n';
        meta << hex_encode(info.near_digest) << '\n';
        if (!meta)
        {
            throw std::runtime_error("failed while writing metadata file");
        }
    }

    PipaSetXRunner::DatasetInfo PipaSetXRunner::load_dataset_info(std::size_t database_size) const
    {
        prepare_dataset(database_size);

        DatasetInfo info;
        std::ifstream meta(dataset_meta_path(database_size));
        if (!meta)
        {
            throw std::runtime_error("failed to open metadata file");
        }
        std::string target_hex;
        std::string near_hex;
        std::getline(meta, info.username);
        std::getline(meta, info.password);
        std::getline(meta, target_hex);
        std::getline(meta, near_hex);
        if (info.username.empty() || info.password.empty() || target_hex.empty() || near_hex.empty())
        {
            throw std::runtime_error("invalid metadata file format");
        }
        info.target_digest = hex_decode_32(target_hex);
        info.near_digest = hex_decode_32(near_hex);

        std::ifstream in(dataset_bin_path(database_size), std::ios::binary);
        if (!in)
        {
            throw std::runtime_error("failed to open dataset binary file");
        }
        char magic[sizeof(kDatasetMagic)]{};
        in.read(magic, sizeof(kDatasetMagic) - 1);
        if (std::string(magic) != kDatasetMagic)
        {
            throw std::runtime_error("dataset magic mismatch");
        }
        std::uint64_t n = 0;
        in.read(reinterpret_cast<char *>(&n), sizeof(n));
        if (n != database_size)
        {
            throw std::runtime_error("dataset size mismatch");
        }

        return info;
    }

    RunResult PipaSetXRunner::run_once(const ExperimentConfig &config, bool positive_query) const
    {
        if (config.database_size % config.poly_modulus_degree != 0)
        {
            throw std::runtime_error("database size must be a multiple of poly_modulus_degree in this benchmark");
        }

        const auto data = load_dataset_info(config.database_size);
        const auto target_digits = compared_digits(data.target_digest, config.compared_bits, config.base);
        const auto near_digits = compared_digits(data.near_digest, config.compared_bits, config.base);

        RunResult result;
        result.config = config;
        result.positive_query = positive_query;
        result.zlib_enabled = is_zlib_enabled();
        result.target_username = data.username;
        result.target_password = data.password;
        result.actual_segment_count = config.segment_count();

        seal::EncryptionParameters parms(seal::scheme_type::BFV);
        std::shared_ptr<seal::SEALContext> context;

        result.time.bfv_param_gen_ms = measure_ms([&]() {
            parms.set_poly_modulus_degree(config.poly_modulus_degree);
            parms.set_coeff_modulus(seal::CoeffModulus::Create(config.poly_modulus_degree, config.coeff_modulus_bits));
            parms.set_plain_modulus(config.plain_modulus);
            context = seal::SEALContext::Create(parms);
            check_batching_enabled(context);
        });
        result.comm.bfv_params_bytes = serialized_size_bytes(parms);

        seal::SecretKey secret_key;
        seal::RelinKeys relin_keys_local;
        result.time.kg_ms = measure_ms([&]() {
            seal::KeyGenerator keygen(context);
            secret_key = keygen.secret_key();
            relin_keys_local = keygen.relin_keys_local();
            auto relin_keys_serial = keygen.relin_keys();
            result.comm.relin_key_bytes = serialized_size_bytes(relin_keys_serial);
        });

        seal::BatchEncoder batch_encoder(context);
        const std::size_t slot_count = batch_encoder.slot_count();
        const std::size_t segment_count = config.segment_count();
        if (slot_count != config.poly_modulus_degree)
        {
            throw std::runtime_error("unexpected BFV slot count");
        }

        const std::uint64_t target_sumsq = squared_sum_mod(target_digits, config.plain_modulus);
        const std::uint64_t near_sumsq = squared_sum_mod(near_digits, config.plain_modulus);

        std::vector<std::vector<seal::Plaintext>> twice_z_plain(
            segment_count, std::vector<seal::Plaintext>(config.l_b));
        std::vector<seal::Plaintext> sumsq_plain(segment_count);

        result.time.dbat_encode_ms = measure_ms([&]() {
            for (std::size_t seg = 0; seg < segment_count; ++seg)
            {
                std::vector<std::uint64_t> sumsq_slots(slot_count, near_sumsq);
                if (seg == 0)
                {
                    sumsq_slots[0] = target_sumsq;
                }
                batch_encoder.encode(sumsq_slots, sumsq_plain[seg]);

                for (std::size_t j = 0; j < config.l_b; ++j)
                {
                    const std::uint64_t near_val = (2ULL * near_digits[j]) % config.plain_modulus;
                    const std::uint64_t target_val = (2ULL * target_digits[j]) % config.plain_modulus;

                    std::vector<std::uint64_t> twice_slots(slot_count, near_val);
                    if (seg == 0)
                    {
                        twice_slots[0] = target_val;
                    }
                    batch_encoder.encode(twice_slots, twice_z_plain[seg][j]);
                }
            }
        });

        std::array<std::uint8_t, 2> query_prefix{};
        std::array<std::uint8_t, kDigestBytes> query_digest{};
        result.time.account_hash_ms = measure_ms([&]() {
            query_prefix = two_byte_prefix_from_username(data.username);
            query_digest = hash_account(data.username, data.password);
            if (!positive_query)
            {
                query_digest = make_near_digest(query_digest, 2);
            }
        });
        (void)query_prefix;

        const auto query_digits = compared_digits(query_digest, config.compared_bits, config.base);
        const std::uint64_t x_sum = squared_sum_mod(query_digits, config.plain_modulus);

        seal::Encryptor encryptor(context, secret_key);
        std::vector<seal::Ciphertext> query_cts(config.l_b);
        seal::Ciphertext query_ct_sum;

        result.time.account_encrypt_ms = measure_ms([&]() {
            result.comm.account_ciphertexts_bytes = 0;
            for (std::size_t j = 0; j < config.l_b; ++j)
            {
                seal::Plaintext pt;
                batch_encoder.encode(repeated_slots(slot_count, query_digits[j]), pt);

                auto serializable = encryptor.encrypt_symmetric(pt);
                const std::string blob = serialize_to_string(serializable);
                result.comm.account_ciphertexts_bytes += blob.size();

                std::stringstream ss(blob, std::ios::in | std::ios::out | std::ios::binary);
                query_cts[j].load(context, ss);
            }

            seal::Plaintext sum_pt;
            batch_encoder.encode(repeated_slots(slot_count, x_sum), sum_pt);
            auto serializable_sum = encryptor.encrypt_symmetric(sum_pt);
            const std::string sum_blob = serialize_to_string(serializable_sum);
            result.comm.account_ciphertexts_bytes += sum_blob.size();
            std::stringstream ss(sum_blob, std::ios::in | std::ios::out | std::ios::binary);
            query_ct_sum.load(context, ss);
        });

        seal::Evaluator evaluator(context);
        seal::Ciphertext final_result;

        result.time.homomorphic_eval_ms = measure_ms([&]() {
            std::vector<seal::Ciphertext> layer;
            layer.reserve(segment_count);

            for (std::size_t seg = 0; seg < segment_count; ++seg)
            {
                seal::Ciphertext acc = query_ct_sum;
                for (std::size_t j = 0; j < config.l_b; ++j)
                {
                    seal::Ciphertext tmp;
                    evaluator.multiply_plain(query_cts[j], twice_z_plain[seg][j], tmp);
                    evaluator.sub_inplace(acc, tmp);
                }
                evaluator.add_plain_inplace(acc, sumsq_plain[seg]);
                layer.push_back(std::move(acc));
            }

            std::size_t depth = 0;
            std::size_t muls = 0;
            while (layer.size() > 1)
            {
                std::vector<seal::Ciphertext> next;
                next.reserve((layer.size() + 1) / 2);
                for (std::size_t i = 0; i + 1 < layer.size(); i += 2)
                {
                    seal::Ciphertext prod;
                    evaluator.multiply(layer[i], layer[i + 1], prod);
                    evaluator.relinearize_inplace(prod, relin_keys_local);
                    next.push_back(std::move(prod));
                    ++muls;
                }
                if (layer.size() & 1U)
                {
                    next.push_back(std::move(layer.back()));
                }
                layer = std::move(next);
                ++depth;
            }

            result.actual_ct_ct_muls = muls;
            result.actual_mul_depth = depth;
            final_result = std::move(layer.front());
        });

        result.comm.result_ciphertext_bytes = serialized_size_bytes(final_result);

        seal::Decryptor decryptor(context, secret_key);
        seal::Plaintext plain_result;
        std::vector<std::uint64_t> decoded;
        result.time.decrypt_ms = measure_ms([&]() {
            decryptor.decrypt(final_result, plain_result);
            batch_encoder.decode(plain_result, decoded);
        });

        result.found = std::any_of(decoded.begin(), decoded.end(), [](std::uint64_t x) { return x == 0; });

        const bool expected = positive_query;
        if (result.found != expected)
        {
            throw std::runtime_error(
                "correctness check failed for " + config.name +
                (positive_query ? " (positive query)" : " (negative query)"));
        }
        if (result.actual_segment_count != config.segment_count() ||
            result.actual_ct_ct_muls != config.expected_ct_ct_muls() ||
            result.actual_mul_depth != config.expected_mul_depth())
        {
            throw std::runtime_error("internal experiment-count check failed");
        }

        return result;
    }

    SelectionResult PipaSetXRunner::select_smallest_working(const ExperimentRequest &request) const
    {
        prepare_dataset(request.database_size);

        SelectionResult selection;
        const auto configs = candidate_configs(request);
        if (configs.empty())
        {
            throw std::runtime_error("no fixed parameter set configured for " + request.name);
        }

        AttemptResult attempt;
        attempt.config = configs.front();
        try
        {
            selection.selected_config = configs.front();
            selection.positive_result = run_once(configs.front(), true);
            selection.negative_result = run_once(configs.front(), false);
            attempt.success = true;
            selection.attempts.push_back(attempt);
            return selection;
        }
        catch (const std::exception &e)
        {
            attempt.success = false;
            attempt.error_message = e.what();
            selection.attempts.push_back(std::move(attempt));
            throw std::runtime_error("fixed parameter set failed correctness for " + request.name + ": " + e.what());
        }
    }

    void PipaSetXRunner::print_run_result(const RunResult &r)
    {
        std::cout << "===== " << r.config.name << (r.positive_query ? " / positive" : " / negative") << " =====\n";
        std::cout << "poly_modulus_degree = " << r.config.poly_modulus_degree
                  << ", coeff_mod_bits = " << coeff_bits_to_string(r.config.coeff_modulus_bits) << " (total "
                  << r.config.coeff_modulus_total_bits() << " bits)\n";
        std::cout << "base = " << r.config.base << ", l_B = " << r.config.l_b
                  << ", plain_modulus = " << r.config.plain_modulus << '\n';
        std::cout << "Database size = " << r.config.database_size << ", B = " << r.actual_segment_count << '\n';
        std::cout << "Expected ct-ct muls = " << r.config.expected_ct_ct_muls() << ", actual = " << r.actual_ct_ct_muls
                  << '\n';
        std::cout << "Expected depth = " << r.config.expected_mul_depth() << ", actual = " << r.actual_mul_depth
                  << '\n';
        std::cout << "found = " << (r.found ? "true" : "false") << ", ZLIB = " << (r.zlib_enabled ? "ON" : "OFF")
                  << "\n";
        if (!r.config.note.empty())
        {
            std::cout << "note = " << r.config.note << "\n";
        }
        std::cout << "\n[Communication]\n";
        std::cout << "  BFV params       : " << format_bytes(r.comm.bfv_params_bytes) << '\n';
        std::cout << "  relin key        : " << format_bytes(r.comm.relin_key_bytes) << '\n';
        std::cout << "  hash prefix      : " << format_bytes(r.comm.hash_prefix_bytes) << '\n';
        std::cout << "  account cts      : " << format_bytes(r.comm.account_ciphertexts_bytes)
                  << " (includes ct_sum)\n";
        std::cout << "  result ct        : " << format_bytes(r.comm.result_ciphertext_bytes) << '\n';
        std::cout << "  total            : " << format_bytes(r.comm.total_bytes()) << "\n\n";

        std::cout << "[Timing]\n";
        std::cout << "  BFV param gen    : " << r.time.bfv_param_gen_ms << " ms\n";
        std::cout << "  D_bat encoding   : " << r.time.dbat_encode_ms << " ms\n";
        std::cout << "  HE eval          : " << r.time.homomorphic_eval_ms << " ms\n";
        std::cout << "  KG               : " << r.time.kg_ms << " ms\n";
        std::cout << "  account hash gen : " << r.time.account_hash_ms << " ms\n";
        std::cout << "  account hash enc : " << r.time.account_encrypt_ms << " ms\n";
        std::cout << "  decrypt          : " << r.time.decrypt_ms << " ms\n";
        std::cout << std::endl;
    }

    void PipaSetXRunner::print_selection_result(const SelectionResult &selection)
    {
        std::cout << "Using fixed parameter set from pipasetx_results.txt.\n";
        const auto &cfg = selection.selected_config;
        std::cout << "  n=" << cfg.poly_modulus_degree << ", base=" << cfg.base
                  << ", coeff=" << coeff_bits_to_string(cfg.coeff_modulus_bits) << "\n\n";
        print_run_result(selection.positive_result);
    }

} // namespace pipasetx
