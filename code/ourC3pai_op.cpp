#include "ourC3pai_op.h"

#include <openssl/bn.h>
#include <openssl/err.h>
#include <openssl/rand.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstring>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <limits>
#include <memory>
#include <random>
#include <sstream>
#include <stdexcept>
#include <vector>

#ifdef _WIN32
#include <direct.h>
#else
#include <errno.h>
#include <sys/stat.h>
#include <sys/types.h>
#endif

namespace ourc3pai_op {
    namespace {

        using Clock = std::chrono::high_resolution_clock;

        struct BNDeleter {
            void operator()(BIGNUM* p) const { BN_free(p); }
        };
        struct BNCTXDeleter {
            void operator()(BN_CTX* p) const { BN_CTX_free(p); }
        };
        struct BNMONTDeleter {
            void operator()(BN_MONT_CTX* p) const { BN_MONT_CTX_free(p); }
        };

        using BNPtr = std::unique_ptr<BIGNUM, BNDeleter>;
        using BNCTXPtr = std::unique_ptr<BN_CTX, BNCTXDeleter>;
        using BNMONTPtr = std::unique_ptr<BN_MONT_CTX, BNMONTDeleter>;

        inline BNPtr make_bn()
        {
            BNPtr p(BN_new());
            if (!p) {
                throw std::runtime_error("BN_new failed");
            }
            return p;
        }

        inline BNCTXPtr make_ctx()
        {
            BNCTXPtr p(BN_CTX_new());
            if (!p) {
                throw std::runtime_error("BN_CTX_new failed");
            }
            return p;
        }

        inline BNMONTPtr make_mont()
        {
            BNMONTPtr p(BN_MONT_CTX_new());
            if (!p) {
                throw std::runtime_error("BN_MONT_CTX_new failed");
            }
            return p;
        }

        inline void throw_openssl(const std::string& where)
        {
            unsigned long err = ERR_get_error();
            std::ostringstream oss;
            oss << where;
            if (err != 0) {
                char buf[256];
                ERR_error_string_n(err, buf, sizeof(buf));
                oss << ": " << buf;
            }
            throw std::runtime_error(oss.str());
        }

        inline void check_one(int ok, const std::string& where)
        {
            if (ok != 1) {
                throw_openssl(where);
            }
        }

        static std::size_t choose_near_square_rows(std::size_t N, std::size_t preferred_rows)
        {
            if (preferred_rows != 0) {
                return preferred_rows;
            }
            std::size_t r = static_cast<std::size_t>(std::sqrt(static_cast<long double>(N)));
            while (r * r < N) {
                ++r;
            }
            return r;
        }

        static std::string dirname_of(const std::string& path)
        {
            std::size_t pos = path.find_last_of("\\/");
            if (pos == std::string::npos) {
                return std::string();
            }
            return path.substr(0, pos);
        }

        static bool create_dir_if_needed(const std::string& path)
        {
            if (path.empty()) {
                return true;
            }
#ifdef _WIN32
            int rc = _mkdir(path.c_str());
            return rc == 0 || errno == EEXIST;
#else
            int rc = mkdir(path.c_str(), 0755);
            return rc == 0 || errno == EEXIST;
#endif
        }

        static bool ensure_directories_impl(const std::string& path)
        {
            if (path.empty()) {
                return true;
            }

            std::string normalized = path;
            for (std::size_t i = 0; i < normalized.size(); ++i) {
                if (normalized[i] == '\\') {
                    normalized[i] = '/';
                }
            }

            std::string current;
            std::size_t pos = 0;

#ifdef _WIN32
            if (normalized.size() >= 2 && normalized[1] == ':') {
                current = normalized.substr(0, 2);
                pos = 2;
                if (pos < normalized.size() && normalized[pos] == '/') {
                    current += '/';
                    ++pos;
                }
            }
            else if (!normalized.empty() && normalized[0] == '/') {
                current = "/";
                pos = 1;
            }
#else
            if (!normalized.empty() && normalized[0] == '/') {
                current = "/";
                pos = 1;
            }
#endif

            while (pos < normalized.size()) {
                while (pos < normalized.size() &&
                    (normalized[pos] == '/' || normalized[pos] == '\\')) {
                    ++pos;
                }
                if (pos >= normalized.size()) {
                    break;
                }

                std::size_t next = pos;
                while (next < normalized.size() &&
                    normalized[next] != '/' && normalized[next] != '\\') {
                    ++next;
                }

                std::string part = normalized.substr(pos, next - pos);
                if (!part.empty()) {
                    if (!current.empty() && current.back() != '/' && current.back() != '\\') {
                        current += '/';
                    }
                    current += part;
                    if (!create_dir_if_needed(current)) {
                        return false;
                    }
                }
                pos = next;
            }

            return true;
        }

        static void write_u64_le(std::ofstream& ofs, std::uint64_t x)
        {
            unsigned char buf[8];
            for (int i = 0; i < 8; ++i) {
                buf[i] = static_cast<unsigned char>((x >> (8 * i)) & 0xffu);
            }
            ofs.write(reinterpret_cast<const char*>(buf), 8);
        }

        static bool read_u64_le(std::ifstream& ifs, std::uint64_t* x)
        {
            unsigned char buf[8];
            if (!ifs.read(reinterpret_cast<char*>(buf), 8)) {
                return false;
            }
            std::uint64_t v = 0;
            for (int i = 0; i < 8; ++i) {
                v |= (static_cast<std::uint64_t>(buf[i]) << (8 * i));
            }
            *x = v;
            return true;
        }

        static std::string to_fixed(double value, int precision)
        {
            std::ostringstream oss;
            oss << std::fixed << std::setprecision(precision) << value;
            return oss.str();
        }

        static bool is_hex_char(char c)
        {
            return (c >= '0' && c <= '9') ||
                (c >= 'a' && c <= 'f') ||
                (c >= 'A' && c <= 'F');
        }

        static int hex_value(char c)
        {
            if (c >= '0' && c <= '9') return c - '0';
            if (c >= 'a' && c <= 'f') return 10 + (c - 'a');
            if (c >= 'A' && c <= 'F') return 10 + (c - 'A');
            return -1;
        }

        static BNPtr bn_from_u256(const U256& u)
        {
            BIGNUM* bn = BN_bin2bn(u.bytes.data(), static_cast<int>(U256_BYTES), NULL);
            if (!bn) {
                throw_openssl("BN_bin2bn(u256)");
            }
            return BNPtr(bn);
        }

        class PaillierCore {
        public:
            explicit PaillierCore(int min_modulus_bits)
                : min_modulus_bits_(min_modulus_bits),
                p_(make_bn()), q_(make_bn()), n_(make_bn()), n_square_(make_bn()),
                lambda_(make_bn()), mu_(make_bn()), one_(make_bn()),
                p_minus_1_(make_bn()), q_minus_1_(make_bn()), gcd_tmp_(make_bn()),
                actual_modulus_bits_(0), cipher_bytes_(0)
            {
                if (min_modulus_bits_ < 257) {
                    throw std::runtime_error("Paillier modulus must be at least 257 bits for 256-bit plaintexts");
                }
                check_one(BN_one(one_.get()), "BN_one");
                keygen();
            }

            int modulus_bits() const { return actual_modulus_bits_; }
            std::size_t cipher_bytes() const { return cipher_bytes_; }
            const BIGNUM* n() const { return n_.get(); }
            const BIGNUM* n_square() const { return n_square_.get(); }

            BNPtr encrypt_bn(const BIGNUM* m, BN_CTX* ctx, BN_MONT_CTX* mont) const
            {
                BNPtr r = make_bn();
                BNPtr gcd = make_bn();
                BNPtr rn = make_bn();
                BNPtr gm = make_bn();
                BNPtr out = make_bn();

                for (;;) {
                    check_one(BN_rand_range(r.get(), n_.get()), "BN_rand_range(r)");
                    if (BN_is_zero(r.get())) {
                        continue;
                    }
                    check_one(BN_gcd(gcd.get(), r.get(), n_.get(), ctx), "BN_gcd(r,n)");
                    if (BN_is_one(gcd.get())) {
                        break;
                    }
                }

                check_one(BN_mod_exp_mont(rn.get(), r.get(), n_.get(), n_square_.get(), ctx, mont),
                    "BN_mod_exp_mont(r^n)");
                check_one(BN_mul(gm.get(), m, n_.get(), ctx), "BN_mul(m,n)");
                check_one(BN_add(gm.get(), gm.get(), one_.get()), "BN_add(1+m*n)");
                check_one(BN_mod_mul(out.get(), gm.get(), rn.get(), n_square_.get(), ctx),
                    "BN_mod_mul(encrypt)");
                return out;
            }

            BNPtr encrypt_zero(BN_CTX* ctx, BN_MONT_CTX* mont) const
            {
                BNPtr z = make_bn();
                BN_zero(z.get());
                return encrypt_bn(z.get(), ctx, mont);
            }

            BNPtr encrypt_one(BN_CTX* ctx, BN_MONT_CTX* mont) const
            {
                BNPtr o = make_bn();
                check_one(BN_one(o.get()), "BN_one(query_one)");
                return encrypt_bn(o.get(), ctx, mont);
            }

            BNPtr encrypt_neg_u256(const U256& value, BN_CTX* ctx, BN_MONT_CTX* mont) const
            {
                BNPtr v = bn_from_u256(value);
                if (BN_cmp(v.get(), n_.get()) >= 0) {
                    throw std::runtime_error("mu is not smaller than Paillier modulus n");
                }
                BNPtr neg = make_bn();
                check_one(BN_sub(neg.get(), n_.get(), v.get()), "BN_sub(n-mu)");
                return encrypt_bn(neg.get(), ctx, mont);
            }

            BNPtr decrypt_bn(const BIGNUM* cipher, BN_CTX* ctx) const
            {
                BNPtr u = make_bn();
                BNPtr l = make_bn();
                BNPtr out = make_bn();

                check_one(BN_mod_exp(u.get(), cipher, lambda_.get(), n_square_.get(), ctx),
                    "BN_mod_exp(c^lambda)");
                check_one(BN_sub(u.get(), u.get(), one_.get()), "BN_sub(u-1)");
                check_one(BN_div(l.get(), NULL, u.get(), n_.get(), ctx), "BN_div(L)");
                check_one(BN_mod_mul(out.get(), l.get(), mu_.get(), n_.get(), ctx),
                    "BN_mod_mul(decrypt)");
                return out;
            }

        private:
            void keygen()
            {
                BNCTXPtr ctx = make_ctx();

                const int prime_bits = std::max(129, (min_modulus_bits_ + 1) / 2);
                check_one(BN_generate_prime_ex(p_.get(), prime_bits, 0, NULL, NULL, NULL),
                    "BN_generate_prime_ex(p)");
                do {
                    check_one(BN_generate_prime_ex(q_.get(), prime_bits, 0, NULL, NULL, NULL),
                        "BN_generate_prime_ex(q)");
                } while (BN_cmp(p_.get(), q_.get()) == 0);

                check_one(BN_mul(n_.get(), p_.get(), q_.get(), ctx.get()), "BN_mul(n)");
                check_one(BN_sqr(n_square_.get(), n_.get(), ctx.get()), "BN_sqr(n^2)");

                BNPtr two_256 = make_bn();
                check_one(BN_one(two_256.get()), "BN_one(two_256)");
                check_one(BN_lshift(two_256.get(), two_256.get(), 256), "BN_lshift(2^256)");
                if (BN_cmp(n_.get(), two_256.get()) <= 0) {
                    throw std::runtime_error("generated Paillier modulus n is not larger than 2^256");
                }

                check_one(BN_sub(p_minus_1_.get(), p_.get(), one_.get()), "BN_sub(p-1)");
                check_one(BN_sub(q_minus_1_.get(), q_.get(), one_.get()), "BN_sub(q-1)");
                check_one(BN_gcd(gcd_tmp_.get(), p_minus_1_.get(), q_minus_1_.get(), ctx.get()),
                    "BN_gcd(lcm gcd)");
                check_one(BN_div(lambda_.get(), NULL, p_minus_1_.get(), gcd_tmp_.get(), ctx.get()),
                    "BN_div((p-1)/gcd)");
                check_one(BN_mul(lambda_.get(), lambda_.get(), q_minus_1_.get(), ctx.get()),
                    "BN_mul(lcm)");
                if (BN_mod_inverse(mu_.get(), lambda_.get(), n_.get(), ctx.get()) == NULL) {
                    throw_openssl("BN_mod_inverse(mu=lambda^{-1} mod n)");
                }

                actual_modulus_bits_ = BN_num_bits(n_.get());
                cipher_bytes_ = static_cast<std::size_t>(BN_num_bytes(n_square_.get()));
            }

            int min_modulus_bits_;
            BNPtr p_;
            BNPtr q_;
            BNPtr n_;
            BNPtr n_square_;
            BNPtr lambda_;
            BNPtr mu_;
            BNPtr one_;
            BNPtr p_minus_1_;
            BNPtr q_minus_1_;
            BNPtr gcd_tmp_;
            int actual_modulus_bits_;
            std::size_t cipher_bytes_;
        };

    } // namespace

    U256::U256()
    {
        bytes.fill(0);
    }

    bool U256::operator==(const U256& other) const
    {
        return bytes == other.bytes;
    }

    bool U256::operator!=(const U256& other) const
    {
        return !(*this == other);
    }

    std::string U256::to_hex() const
    {
        std::ostringstream oss;
        oss << std::hex << std::setfill('0');
        for (std::size_t i = 0; i < bytes.size(); ++i) {
            oss << std::setw(2) << static_cast<unsigned int>(bytes[i]);
        }
        return oss.str();
    }

    U256 U256::from_hex(const std::string& hex)
    {
        std::string s;
        s.reserve(hex.size());
        for (std::size_t i = 0; i < hex.size(); ++i) {
            if (hex[i] == ' ' || hex[i] == '\t' || hex[i] == '\n' || hex[i] == '\r') {
                continue;
            }
            if (hex[i] == '0' && i + 1 < hex.size() && (hex[i + 1] == 'x' || hex[i + 1] == 'X')) {
                ++i;
                continue;
            }
            if (!is_hex_char(hex[i])) {
                throw std::runtime_error("invalid hex in U256::from_hex");
            }
            s.push_back(hex[i]);
        }
        if (s.size() > 64) {
            throw std::runtime_error("hex string is too long for U256");
        }
        if (s.size() % 2 == 1) {
            s = std::string("0") + s;
        }

        U256 out;
        out.bytes.fill(0);
        std::size_t dst = U256_BYTES - s.size() / 2;
        for (std::size_t i = 0; i < s.size(); i += 2) {
            int hi = hex_value(s[i]);
            int lo = hex_value(s[i + 1]);
            out.bytes[dst++] = static_cast<unsigned char>((hi << 4) | lo);
        }
        return out;
    }

    U256 U256::random()
    {
        U256 out;
        check_one(RAND_bytes(out.bytes.data(), static_cast<int>(out.bytes.size())),
            "RAND_bytes(U256::random)");
        return out;
    }

    class OurC3PaiOpSystem::Impl {
    public:
        explicit Impl(int paillier_modulus_bits)
            : paillier_(paillier_modulus_bits), rows_(0), cols_(0)
        {
        }

        bool build_from_records(const std::vector<Record>& records, std::size_t preferred_rows)
        {
            if (records.empty()) {
                return false;
            }

            records_sorted_ = records;
            std::sort(records_sorted_.begin(), records_sorted_.end(),
                [](const Record& a, const Record& b) { return a.k < b.k; });

            for (std::size_t i = 1; i < records_sorted_.size(); ++i) {
                if (records_sorted_[i - 1].k == records_sorted_[i].k) {
                    throw std::runtime_error("duplicate keys are not allowed");
                }
            }

            rows_ = choose_near_square_rows(records_sorted_.size(), preferred_rows);
            cols_ = (records_sorted_.size() + rows_ - 1) / rows_;
            if (rows_ * cols_ != records_sorted_.size()) {
                throw std::runtime_error("this implementation requires N = rows * cols exactly");
            }

            exponent_col_major_.clear();
            exponent_col_major_.resize(records_sorted_.size());
            row_first_keys_.resize(rows_);
            row_last_keys_.resize(rows_);
            flags_.resize(rows_);

            std::mt19937_64 prng(0xC3A5112233445566ULL);
            for (std::size_t r = 0; r < rows_; ++r) {
                std::size_t begin = r * cols_;
                std::size_t end = begin + cols_ - 1;
                row_first_keys_[r] = records_sorted_[begin].k;
                row_last_keys_[r] = records_sorted_[end].k;
            }

            for (std::size_t r = 0; r + 1 < rows_; ++r) {
                std::uint64_t lo = row_last_keys_[r];
                std::uint64_t hi_exclusive = row_first_keys_[r + 1];
                if (lo >= hi_exclusive) {
                    throw std::runtime_error("invalid row boundary while generating flags");
                }
                std::uniform_int_distribution<std::uint64_t> dist(lo, hi_exclusive - 1);
                flags_[r] = dist(prng);
            }
            flags_[rows_ - 1] = row_last_keys_[rows_ - 1];

            for (std::size_t j = 0; j < cols_; ++j) {
                for (std::size_t i = 0; i < rows_; ++i) {
                    const U256& v = records_sorted_[i * cols_ + j].v;
                    exponent_col_major_[j * rows_ + i] = bn_from_u256(v);
                }
            }

            std::vector<Record>().swap(records_sorted_);
            return true;
        }

        QueryResult query(std::uint64_t k, const U256& mu) const
        {
            if (rows_ == 0 || cols_ == 0) {
                throw std::runtime_error("system is not initialized");
            }

            const std::size_t row = locate_row_by_flags(k);

            BNCTXPtr client_ctx = make_ctx();
            BNMONTPtr client_mont = make_mont();
            check_one(BN_MONT_CTX_set(client_mont.get(), paillier_.n_square(), client_ctx.get()),
                "BN_MONT_CTX_set(client)");

            std::vector<BNPtr> qu(rows_);
            auto client_start = Clock::now();
            for (std::size_t i = 0; i < rows_; ++i) {
                qu[i] = (i == row) ? paillier_.encrypt_one(client_ctx.get(), client_mont.get())
                    : paillier_.encrypt_zero(client_ctx.get(), client_mont.get());
            }
            BNPtr e_neg_mu = paillier_.encrypt_neg_u256(mu, client_ctx.get(), client_mont.get());
            auto client_mid = Clock::now();

            std::vector<BNPtr> ans(cols_);
            auto server_start = Clock::now();
            evaluate_server(qu, e_neg_mu.get(), &ans);
            auto server_end = Clock::now();

            auto decrypt_start = Clock::now();
            bool found = false;
            std::size_t match_col = cols_;
            for (std::size_t j = 0; j < cols_; ++j) {
                BNPtr dec = paillier_.decrypt_bn(ans[j].get(), client_ctx.get());
                if (BN_is_zero(dec.get())) {
                    found = true;
                    match_col = j;
                    break;
                }
            }
            auto decrypt_end = Clock::now();

            QueryResult out;
            out.found = found;
            out.row_index = row;
            out.match_col_index = found ? match_col : static_cast<std::size_t>(-1);
            out.communication.server_to_client_mb =
                static_cast<double>(flags_.size() * sizeof(std::uint64_t) + cols_ * paillier_.cipher_bytes()) /
                (1024.0 * 1024.0);
            out.communication.client_to_server_qu_kb =
                static_cast<double>(rows_ * paillier_.cipher_bytes()) / 1024.0;
            out.communication.client_to_server_total_kb =
                static_cast<double>((rows_ + 1) * paillier_.cipher_bytes()) / 1024.0;
            out.timing.server_ms =
                std::chrono::duration<double, std::milli>(server_end - server_start).count();
            out.timing.client_ms =
                std::chrono::duration<double, std::milli>(client_mid - client_start).count() +
                std::chrono::duration<double, std::milli>(decrypt_end - decrypt_start).count();
            return out;
        }

        Dimensions dims() const
        {
            Dimensions d;
            d.rows = rows_;
            d.cols = cols_;
            return d;
        }

        std::size_t cipher_bytes() const { return paillier_.cipher_bytes(); }
        std::size_t paillier_modulus_bits() const { return static_cast<std::size_t>(paillier_.modulus_bits()); }
        const std::vector<std::uint64_t>& flags() const { return flags_; }

        static std::vector<Record> generate_random_records(std::size_t N)
        {
            std::vector<Record> records(N);
            std::mt19937_64 prng(0x5EED123456789ABCLL ^ static_cast<unsigned long long>(N));
            std::uniform_int_distribution<std::uint64_t> gap_dist(1, 1024);

            std::uint64_t current = 0;
            for (std::size_t i = 0; i < N; ++i) {
                current += gap_dist(prng);
                records[i].k = current;
                records[i].v = U256::random();
            }
            return records;
        }

        static bool save_records_binary(const std::string& path, const std::vector<Record>& records)
        {
            if (!OurC3PaiOpSystem::ensure_parent_directories(path)) {
                return false;
            }
            std::ofstream ofs(path.c_str(), std::ios::binary);
            if (!ofs) {
                std::cerr << "cannot open file for writing: " << path << std::endl;
                return false;
            }
            const char magic[8] = { 'O', 'C', '3', 'O', 'P', 'A', 'I', 0 };
            ofs.write(magic, 8);
            write_u64_le(ofs, static_cast<std::uint64_t>(records.size()));
            for (std::size_t i = 0; i < records.size(); ++i) {
                write_u64_le(ofs, records[i].k);
                ofs.write(reinterpret_cast<const char*>(records[i].v.bytes.data()), U256_BYTES);
            }
            return static_cast<bool>(ofs);
        }

        static bool load_records_binary(const std::string& path, std::vector<Record>* records)
        {
            if (!records) {
                return false;
            }
            std::ifstream ifs(path.c_str(), std::ios::binary);
            if (!ifs) {
                return false;
            }
            char magic[8];
            if (!ifs.read(magic, 8)) {
                return false;
            }
            const char expect[8] = { 'O', 'C', '3', 'O', 'P', 'A', 'I', 0 };
            if (std::memcmp(magic, expect, 8) != 0) {
                return false;
            }
            std::uint64_t count = 0;
            if (!read_u64_le(ifs, &count)) {
                return false;
            }
            records->resize(static_cast<std::size_t>(count));
            for (std::size_t i = 0; i < records->size(); ++i) {
                if (!read_u64_le(ifs, &(*records)[i].k)) {
                    return false;
                }
                if (!ifs.read(reinterpret_cast<char*>((*records)[i].v.bytes.data()), U256_BYTES)) {
                    return false;
                }
            }
            return true;
        }

    private:
        std::size_t locate_row_by_flags(std::uint64_t k) const
        {
            std::vector<std::uint64_t>::const_iterator it =
                std::lower_bound(flags_.begin(), flags_.end(), k);
            if (it == flags_.end()) {
                return rows_ - 1;
            }
            return static_cast<std::size_t>(it - flags_.begin());
        }

        static BNPtr sample_small_invertible(const BIGNUM* n, BN_CTX* ctx)
        {
            static std::mt19937_64 prng(
                static_cast<unsigned long long>(std::chrono::high_resolution_clock::now().time_since_epoch().count()) ^
                0xC3A5F00D12345678ULL);
            std::uniform_int_distribution<std::uint64_t> dist(1, std::numeric_limits<std::uint64_t>::max());

            BNPtr rnd = make_bn();
            BNPtr gcd = make_bn();
            for (;;) {
                std::uint64_t x = dist(prng);
                if (x == 0) {
                    continue;
                }
                if (sizeof(BN_ULONG) >= sizeof(std::uint64_t)) {
                    check_one(BN_set_word(rnd.get(), static_cast<BN_ULONG>(x)), "BN_set_word(random scalar)");
                }
                else {
                    unsigned char buf[8];
                    for (int i = 0; i < 8; ++i) {
                        buf[7 - i] = static_cast<unsigned char>((x >> (8 * i)) & 0xffu);
                    }
                    if (!BN_bin2bn(buf, 8, rnd.get())) {
                        throw_openssl("BN_bin2bn(random scalar)");
                    }
                }
                check_one(BN_gcd(gcd.get(), rnd.get(), n, ctx), "BN_gcd(random scalar, n)");
                if (BN_is_one(gcd.get())) {
                    return rnd;
                }
            }
        }

        void evaluate_server(const std::vector<BNPtr>& qu,
            const BIGNUM* e_neg_mu,
            std::vector<BNPtr>* ans) const
        {
            if (!ans) {
                throw std::runtime_error("ans is null");
            }
            ans->resize(cols_);

            BNCTXPtr ctx = make_ctx();
            BNMONTPtr mont = make_mont();
            check_one(BN_MONT_CTX_set(mont.get(), paillier_.n_square(), ctx.get()),
                "BN_MONT_CTX_set(server)");

            BNPtr term = make_bn();
            BNPtr acc = make_bn();
            BNPtr tmp = make_bn();

            for (std::size_t j = 0; j < cols_; ++j) {
                check_one(BN_one(acc.get()), "BN_one(acc)");
                const std::size_t col_base = j * rows_;
                for (std::size_t i = 0; i < rows_; ++i) {
                    check_one(BN_mod_exp_mont(term.get(), qu[i].get(), exponent_col_major_[col_base + i].get(),
                        paillier_.n_square(), ctx.get(), mont.get()),
                        "BN_mod_exp_mont(qu^v cached)");
                    check_one(BN_mod_mul(acc.get(), acc.get(), term.get(), paillier_.n_square(), ctx.get()),
                        "BN_mod_mul(column accumulate)");
                }

                check_one(BN_mod_mul(tmp.get(), acc.get(), e_neg_mu, paillier_.n_square(), ctx.get()),
                    "BN_mod_mul(ans1)");
                BNPtr scalar = sample_small_invertible(paillier_.n(), ctx.get());
                BNPtr masked = make_bn();
                check_one(BN_mod_exp_mont(masked.get(), tmp.get(), scalar.get(), paillier_.n_square(),
                    ctx.get(), mont.get()),
                    "BN_mod_exp_mont(masking)");
                (*ans)[j] = std::move(masked);
            }
        }

        PaillierCore paillier_;
        std::size_t rows_;
        std::size_t cols_;
        std::vector<Record> records_sorted_;
        std::vector<BNPtr> exponent_col_major_;
        std::vector<std::uint64_t> row_first_keys_;
        std::vector<std::uint64_t> row_last_keys_;
        std::vector<std::uint64_t> flags_;
    };

    OurC3PaiOpSystem::OurC3PaiOpSystem(int paillier_modulus_bits)
        : impl_(new Impl(paillier_modulus_bits))
    {
    }

    OurC3PaiOpSystem::~OurC3PaiOpSystem()
    {
        delete impl_;
    }

    bool OurC3PaiOpSystem::build_from_records(const std::vector<Record>& records,
        std::size_t preferred_rows)
    {
        return impl_->build_from_records(records, preferred_rows);
    }

    QueryResult OurC3PaiOpSystem::query(std::uint64_t k, const U256& mu) const
    {
        return impl_->query(k, mu);
    }

    Dimensions OurC3PaiOpSystem::dims() const
    {
        return impl_->dims();
    }

    std::size_t OurC3PaiOpSystem::cipher_bytes() const
    {
        return impl_->cipher_bytes();
    }

    std::size_t OurC3PaiOpSystem::paillier_modulus_bits() const
    {
        return impl_->paillier_modulus_bits();
    }

    const std::vector<std::uint64_t>& OurC3PaiOpSystem::flags() const
    {
        return impl_->flags();
    }

    std::vector<Record> OurC3PaiOpSystem::generate_random_records(std::size_t N)
    {
        return Impl::generate_random_records(N);
    }

    bool OurC3PaiOpSystem::save_records_binary(const std::string& path,
        const std::vector<Record>& records)
    {
        return Impl::save_records_binary(path, records);
    }

    bool OurC3PaiOpSystem::load_records_binary(const std::string& path,
        std::vector<Record>* records)
    {
        return Impl::load_records_binary(path, records);
    }

    bool OurC3PaiOpSystem::ensure_parent_directories(const std::string& path)
    {
        return ensure_directories_impl(dirname_of(path));
    }

    std::string format_mb(double value_mb)
    {
        return to_fixed(value_mb, 4) + " MB";
    }

    std::string format_kb(double value_kb)
    {
        return to_fixed(value_kb, 4) + " KB";
    }

    std::string format_ms(double value_ms)
    {
        return to_fixed(value_ms, 4) + " ms";
    }

} // namespace ourc3pai_op
