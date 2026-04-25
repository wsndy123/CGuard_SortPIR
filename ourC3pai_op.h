#ifndef OURC3PAI_OP_H
#define OURC3PAI_OP_H

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace ourc3pai_op {

    static const std::size_t U256_BYTES = 32;

    struct U256 {
        std::array<unsigned char, U256_BYTES> bytes;

        U256();
        bool operator==(const U256& other) const;
        bool operator!=(const U256& other) const;
        std::string to_hex() const;
        static U256 from_hex(const std::string& hex);
        static U256 random();
    };

    struct Record {
        std::uint64_t k;
        U256 v;
    };

    struct CommunicationStats {
        double server_to_client_mb;       // f + ans
        double client_to_server_qu_kb;    // qu only, as requested
        double client_to_server_total_kb; // qu + E(-mu)
    };

    struct TimingStats {
        double server_ms; // online server evaluation only
        double client_ms; // query generation + final decryption/matching
    };

    struct QueryResult {
        bool found;
        std::size_t row_index;
        std::size_t match_col_index;
        CommunicationStats communication;
        TimingStats timing;
    };

    struct Dimensions {
        std::size_t rows;
        std::size_t cols;
    };

    class OurC3PaiOpSystem {
    public:
        explicit OurC3PaiOpSystem(int paillier_modulus_bits = 257);
        ~OurC3PaiOpSystem();

        OurC3PaiOpSystem(const OurC3PaiOpSystem&) = delete;
        OurC3PaiOpSystem& operator=(const OurC3PaiOpSystem&) = delete;

        bool build_from_records(const std::vector<Record>& records,
            std::size_t preferred_rows = 0);

        QueryResult query(std::uint64_t k, const U256& mu) const;

        Dimensions dims() const;
        std::size_t cipher_bytes() const;
        std::size_t paillier_modulus_bits() const;
        const std::vector<std::uint64_t>& flags() const;

        static std::vector<Record> generate_random_records(std::size_t N);
        static bool save_records_binary(const std::string& path,
            const std::vector<Record>& records);
        static bool load_records_binary(const std::string& path,
            std::vector<Record>* records);
        static bool ensure_parent_directories(const std::string& path);

    private:
        class Impl;
        Impl* impl_;
    };

    std::string format_mb(double value_mb);
    std::string format_kb(double value_kb);
    std::string format_ms(double value_ms);

} // namespace ourc3pai_op

#endif // OURC3PAI_OP_H
