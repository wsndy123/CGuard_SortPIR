#include "IDB.h"

#include <openssl/bn.h>
#include <openssl/crypto.h>
#include <openssl/rand.h>

#include <algorithm>
#include <array>
#include <cerrno>
#include <chrono>
#include <cstring>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <memory>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

#ifdef _WIN32
#include <direct.h>
#else
#include <sys/stat.h>
#include <sys/types.h>
#endif

namespace idb {
    namespace {

        typedef std::unique_ptr<BIGNUM, decltype(&BN_free)> BN_ptr;
        typedef std::unique_ptr<BN_CTX, decltype(&BN_CTX_free)> BN_CTX_ptr;
        typedef std::unique_ptr<BN_MONT_CTX, decltype(&BN_MONT_CTX_free)> BN_MONT_CTX_ptr;

        const char* kModulusHex =
            "010000000000000000000000000000000000000000000000000000000000000129";
        // p = 2^256 + 297, a 257-bit prime. All encoded 256-bit inputs v + 1 fit strictly below p.

        void ThrowOpenSSLError(const std::string& msg)
        {
            throw std::runtime_error(msg);
        }

        void CheckOpenSSL(int ok, const std::string& msg)
        {
            if (ok != 1) {
                ThrowOpenSSLError(msg);
            }
        }

        BN_ptr MakeBN()
        {
            BN_ptr x(BN_new(), &BN_free);
            if (!x) {
                throw std::bad_alloc();
            }
            return x;
        }

        BN_CTX_ptr MakeCtx()
        {
            BN_CTX_ptr ctx(BN_CTX_new(), &BN_CTX_free);
            if (!ctx) {
                throw std::bad_alloc();
            }
            return ctx;
        }

        BN_MONT_CTX_ptr MakeMontCtx(const BIGNUM* modulus)
        {
            BN_MONT_CTX_ptr mont(BN_MONT_CTX_new(), &BN_MONT_CTX_free);
            if (!mont) {
                throw std::bad_alloc();
            }
            BN_CTX_ptr ctx = MakeCtx();
            CheckOpenSSL(BN_MONT_CTX_set(mont.get(), modulus, ctx.get()),
                "BN_MONT_CTX_set failed");
            return mont;
        }

        BN_ptr BNFromHex(const std::string& hex)
        {
            BN_ptr bn(NULL, &BN_free);
            BIGNUM* raw = NULL;
            CheckOpenSSL(BN_hex2bn(&raw, hex.c_str()) > 0 ? 1 : 0, "BN_hex2bn failed");
            bn.reset(raw);
            return bn;
        }

        std::string BytesToHex(const unsigned char* data, std::size_t len)
        {
            std::ostringstream oss;
            oss << std::hex << std::setfill('0');
            for (std::size_t i = 0; i < len; ++i) {
                oss << std::setw(2) << static_cast<unsigned int>(data[i]);
            }
            return oss.str();
        }

        std::vector<unsigned char> HexToBytes(const std::string& hex)
        {
            if (hex.size() != 2 * IDB::kInputBytes) {
                throw std::runtime_error("Expected exactly 64 hex characters per line");
            }

            std::vector<unsigned char> bytes(IDB::kInputBytes, 0);
            for (std::size_t i = 0; i < IDB::kInputBytes; ++i) {
                const char hi = hex[2 * i];
                const char lo = hex[2 * i + 1];
                int h = -1;
                int l = -1;

                if (hi >= '0' && hi <= '9') h = hi - '0';
                else if (hi >= 'a' && hi <= 'f') h = 10 + (hi - 'a');
                else if (hi >= 'A' && hi <= 'F') h = 10 + (hi - 'A');

                if (lo >= '0' && lo <= '9') l = lo - '0';
                else if (lo >= 'a' && lo <= 'f') l = 10 + (lo - 'a');
                else if (lo >= 'A' && lo <= 'F') l = 10 + (lo - 'A');

                if (h < 0 || l < 0) {
                    throw std::runtime_error("Dataset line is not valid hex");
                }
                bytes[i] = static_cast<unsigned char>((h << 4) | l);
            }
            return bytes;
        }

        BN_ptr EncodeInputToGroupElement(const std::string& input_hex)
        {
            const std::vector<unsigned char> bytes = HexToBytes(input_hex);
            BN_ptr x(BN_bin2bn(bytes.data(), static_cast<int>(bytes.size()), NULL), &BN_free);
            if (!x) {
                throw std::bad_alloc();
            }
            // Encode 256-bit value v as v + 1, so every encoded value lies in Z_p^* and mapping is injective.
            CheckOpenSSL(BN_add_word(x.get(), 1), "BN_add_word failed");
            return x;
        }

        void BNToFixedBytes(const BIGNUM* x, std::vector<unsigned char>& out)
        {
            if (BN_bn2binpad(x, out.data(), static_cast<int>(out.size())) <= 0) {
                ThrowOpenSSLError("BN_bn2binpad failed");
            }
        }

        void BNToFixedBytes(const BIGNUM* x, unsigned char* out, std::size_t len)
        {
            if (BN_bn2binpad(x, out, static_cast<int>(len)) <= 0) {
                ThrowOpenSSLError("BN_bn2binpad failed");
            }
        }

        BN_ptr RandomCoprimeExponent(const BIGNUM* phi, BN_CTX* ctx)
        {
            BN_ptr candidate = MakeBN();
            BN_ptr gcd = MakeBN();
            BN_ptr one = MakeBN();
            CheckOpenSSL(BN_one(one.get()), "BN_one failed");

            for (;;) {
                CheckOpenSSL(BN_priv_rand_range(candidate.get(), phi), "BN_priv_rand_range failed");
                if (BN_is_zero(candidate.get())) {
                    continue;
                }
                BN_set_flags(candidate.get(), BN_FLG_CONSTTIME);
                CheckOpenSSL(BN_gcd(gcd.get(), candidate.get(), phi, ctx), "BN_gcd failed");
                if (BN_cmp(gcd.get(), one.get()) == 0) {
                    return candidate;
                }
            }
        }

        std::string Basename(const std::string& path)
        {
            const std::size_t pos = path.find_last_of("/\\");
            if (pos == std::string::npos) {
                return path;
            }
            return path.substr(pos + 1);
        }

        std::string ParentPath(const std::string& path)
        {
            const std::size_t pos = path.find_last_of("/\\");
            if (pos == std::string::npos) {
                return std::string();
            }
            return path.substr(0, pos);
        }

        bool MakeSingleDirectory(const std::string& dir)
        {
#ifdef _WIN32
            const int rc = _mkdir(dir.c_str());
#else
            const int rc = mkdir(dir.c_str(), 0755);
#endif
            return (rc == 0) || (errno == EEXIST);
        }

        bool CreateDirectoriesIfNeeded(const std::string& dir)
        {
            if (dir.empty()) {
                return true;
            }

            std::string normalized = dir;
            std::replace(normalized.begin(), normalized.end(), '\\', '/');

            std::string current;
            if (normalized.size() >= 2 && normalized[1] == ':') {
                current = normalized.substr(0, 2);
            }

            std::size_t start = 0;
            if (!current.empty()) {
                start = 2;
                if (normalized.size() > 2 && normalized[2] == '/') {
                    current.push_back('/');
                    start = 3;
                }
            }
            else if (!normalized.empty() && normalized[0] == '/') {
                current = "/";
                start = 1;
            }

            while (start < normalized.size()) {
                const std::size_t slash = normalized.find('/', start);
                const std::string part = normalized.substr(start, slash == std::string::npos ? std::string::npos : slash - start);
                if (!part.empty()) {
                    if (!current.empty() && current[current.size() - 1] != '/') {
                        current.push_back('/');
                    }
                    current += part;
                    if (!MakeSingleDirectory(current)) {
                        return false;
                    }
                }
                if (slash == std::string::npos) {
                    break;
                }
                start = slash + 1;
            }

            return true;
        }

        std::size_t InferNFromFilename(const std::string& path)
        {
            const std::string filename = Basename(path);
            if (filename.find("16") != std::string::npos) return std::size_t(1) << 16;
            if (filename.find("18") != std::string::npos) return std::size_t(1) << 18;
            if (filename.find("20") != std::string::npos) return std::size_t(1) << 20;
            if (filename.find("22") != std::string::npos) return std::size_t(1) << 22;
            return 0;
        }

        std::string LocalTrim(const std::string& s)
        {
            const std::size_t begin = s.find_first_not_of(" \t\r\n");
            if (begin == std::string::npos) {
                return "";
            }
            const std::size_t end = s.find_last_not_of(" \t\r\n");
            return s.substr(begin, end - begin + 1);
        }

        bool FileContainsHexLine(const std::string& file_path, const std::string& target_hex)
        {
            std::ifstream fin(file_path.c_str());
            if (!fin) {
                throw std::runtime_error("Failed to open dataset file for verification: " + file_path);
            }

            std::string line;
            while (std::getline(fin, line)) {
                const std::string t = LocalTrim(line);
                if (t == target_hex) {
                    return true;
                }
            }
            return false;
        }

        std::string Random256BitHex()
        {
            std::array<unsigned char, IDB::kInputBytes> bytes;
            bytes.fill(0);
            CheckOpenSSL(RAND_priv_bytes(bytes.data(), static_cast<int>(bytes.size())),
                "RAND_priv_bytes failed");
            return BytesToHex(bytes.data(), bytes.size());
        }

        std::string FindAbsent256BitHex(const std::string& dataset_path)
        {
            for (;;) {
                const std::string candidate = Random256BitHex();
                if (!FileContainsHexLine(dataset_path, candidate)) {
                    return candidate;
                }
            }
        }

        struct QueryResult {
            bool found;
            double server_online_ms;
            double client_online_ms;

            QueryResult() : found(false), server_online_ms(0.0), client_online_ms(0.0) {}
        };

        QueryResult RunSingleQuery(const std::string& query_hex,
            const BIGNUM* s,
            const BIGNUM* p,
            const BIGNUM* phi,
            BN_MONT_CTX* mont,
            const std::vector<unsigned char>& stored_tokens,
            std::size_t element_bytes)
        {
            BN_CTX_ptr ctx = MakeCtx();

            const std::chrono::steady_clock::time_point client_begin = std::chrono::steady_clock::now();
            BN_ptr r = RandomCoprimeExponent(phi, ctx.get());

            BN_ptr encoded_y = EncodeInputToGroupElement(query_hex);
            BN_ptr y_pow_r = MakeBN();
            CheckOpenSSL(BN_mod_exp_mont(y_pow_r.get(), encoded_y.get(), r.get(), p, ctx.get(), mont),
                "BN_mod_exp_mont for y^r failed");

            const std::chrono::steady_clock::time_point server_begin = std::chrono::steady_clock::now();
            BN_ptr y_pow_rs = MakeBN();
            CheckOpenSSL(BN_mod_exp_mont(y_pow_rs.get(), y_pow_r.get(), s, p, ctx.get(), mont),
                "BN_mod_exp_mont for (y^r)^s failed");
            const std::chrono::steady_clock::time_point server_end = std::chrono::steady_clock::now();

            BN_ptr r_inv(BN_mod_inverse(NULL, r.get(), phi, ctx.get()), &BN_free);
            if (!r_inv) {
                ThrowOpenSSLError("BN_mod_inverse for r^{-1} failed");
            }
            BN_set_flags(r_inv.get(), BN_FLG_CONSTTIME);

            BN_ptr y_pow_s = MakeBN();
            CheckOpenSSL(BN_mod_exp_mont(y_pow_s.get(), y_pow_rs.get(), r_inv.get(), p, ctx.get(), mont),
                "BN_mod_exp_mont for ((y^r)^s)^(r^{-1}) failed");

            std::vector<unsigned char> target(element_bytes, 0);
            BNToFixedBytes(y_pow_s.get(), target);

            bool found = false;
            for (std::size_t offset = 0; offset < stored_tokens.size(); offset += element_bytes) {
                if (std::memcmp(stored_tokens.data() + offset, target.data(), element_bytes) == 0) {
                    found = true;
                    break;
                }
            }
            const std::chrono::steady_clock::time_point client_end = std::chrono::steady_clock::now();

            QueryResult qr;
            qr.found = found;
            qr.server_online_ms =
                std::chrono::duration<double, std::milli>(server_end - server_begin).count();
            qr.client_online_ms =
                std::chrono::duration<double, std::milli>(client_end - client_begin).count();
            return qr;
        }

    } // namespace

    const std::size_t IDB::kInputBytes;

    IDB::IDB()
    {
    }

    const char* IDB::DefaultDataDir()
    {
        return "D:\\KWPIR\\test_data\\IDBtestdata";
    }

    std::string IDB::Trim(const std::string& s)
    {
        const std::size_t begin = s.find_first_not_of(" \t\r\n");
        if (begin == std::string::npos) {
            return "";
        }
        const std::size_t end = s.find_last_not_of(" \t\r\n");
        return s.substr(begin, end - begin + 1);
    }

    void IDB::GenerateDataset(const std::string& file_path, std::size_t N) const
    {
        const std::string parent = ParentPath(file_path);
        if (!CreateDirectoriesIfNeeded(parent)) {
            throw std::runtime_error("Failed to create dataset directory: " + parent);
        }

        std::ofstream fout(file_path.c_str(), std::ios::binary | std::ios::trunc);
        if (!fout) {
            throw std::runtime_error("Failed to create dataset file: " + file_path);
        }

        std::array<unsigned char, kInputBytes> bytes;
        bytes.fill(0);
        for (std::size_t i = 0; i < N; ++i) {
            CheckOpenSSL(RAND_priv_bytes(bytes.data(), static_cast<int>(bytes.size())),
                "RAND_priv_bytes failed while generating dataset");
            fout << BytesToHex(bytes.data(), bytes.size()) << '\n';
        }
    }

    Metrics IDB::RunFromFile(const std::string& file_path) const
    {
        Metrics metrics;

        BN_ptr p = BNFromHex(kModulusHex);
        BN_ptr phi = MakeBN();
        CheckOpenSSL(BN_copy(phi.get(), p.get()) != NULL ? 1 : 0, "BN_copy failed");
        CheckOpenSSL(BN_sub_word(phi.get(), 1), "BN_sub_word failed");
        BN_MONT_CTX_ptr mont = MakeMontCtx(p.get());

        std::ifstream fin(file_path.c_str());
        if (!fin) {
            throw std::runtime_error("Failed to open dataset file: " + file_path);
        }

        metrics.element_bytes = static_cast<std::size_t>(BN_num_bytes(p.get()));
        metrics.N = InferNFromFilename(file_path);
        if (metrics.N == 0) {
            throw std::runtime_error("Cannot infer N from file name. Use names containing 16, 18, 20, or 22.");
        }

        std::vector<unsigned char> stored_tokens;
        stored_tokens.reserve(metrics.N * metrics.element_bytes);

        std::string first_line_hex;
        bool have_first_line = false;

        const std::chrono::steady_clock::time_point offline_begin = std::chrono::steady_clock::now();
        BN_CTX_ptr ctx = MakeCtx();
        BN_ptr s = RandomCoprimeExponent(phi.get(), ctx.get());

        std::string line;
        std::size_t line_count = 0;
        while (std::getline(fin, line)) {
            const std::string hex = Trim(line);
            if (hex.empty()) {
                continue;
            }
            if (!have_first_line) {
                first_line_hex = hex;
                have_first_line = true;
            }

            BN_ptr encoded = EncodeInputToGroupElement(hex);
            BN_ptr token = MakeBN();
            CheckOpenSSL(BN_mod_exp_mont(token.get(), encoded.get(), s.get(), p.get(), ctx.get(), mont.get()),
                "BN_mod_exp_mont for x_i^s failed");

            const std::size_t old_size = stored_tokens.size();
            stored_tokens.resize(old_size + metrics.element_bytes);
            BNToFixedBytes(token.get(), stored_tokens.data() + old_size, metrics.element_bytes);
            ++line_count;
        }
        const std::chrono::steady_clock::time_point offline_end = std::chrono::steady_clock::now();

        if (!have_first_line) {
            throw std::runtime_error("Dataset is empty: " + file_path);
        }
        if (line_count != metrics.N) {
            throw std::runtime_error("Dataset line count does not match N inferred from file name.");
        }

        metrics.server_offline_ms =
            std::chrono::duration<double, std::milli>(offline_end - offline_begin).count();
        metrics.server_to_client_mb =
            static_cast<double>((metrics.N + 1) * metrics.element_bytes) / (1024.0 * 1024.0);
        metrics.client_to_server_kb =
            static_cast<double>(metrics.element_bytes) / 1024.0;

        metrics.positive_query_hex = first_line_hex;
        QueryResult pos = RunSingleQuery(first_line_hex, s.get(), p.get(), phi.get(), mont.get(),
            stored_tokens, metrics.element_bytes);
        metrics.positive_found = pos.found;
        metrics.server_online_ms = pos.server_online_ms;
        metrics.client_online_ms = pos.client_online_ms;

        metrics.negative_query_hex = FindAbsent256BitHex(file_path);
        QueryResult neg = RunSingleQuery(metrics.negative_query_hex, s.get(), p.get(), phi.get(), mont.get(),
            stored_tokens, metrics.element_bytes);
        metrics.negative_found = neg.found;

        return metrics;
    }

} // namespace idb
