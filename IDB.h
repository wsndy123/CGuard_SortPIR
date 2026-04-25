#ifndef IDB_H
#define IDB_H

#include <cstddef>
#include <cstdint>
#include <string>

namespace idb {

    struct Metrics {
        std::size_t N = 0;
        std::size_t element_bytes = 0;

        double server_to_client_mb = 0.0;
        double client_to_server_kb = 0.0;
        double server_online_ms = 0.0;
        double client_online_ms = 0.0;
        double server_offline_ms = 0.0;

        bool positive_found = false;
        bool negative_found = false;

        std::string positive_query_hex;
        std::string negative_query_hex;
    };

    class IDB {
    public:
        IDB();

        static constexpr std::size_t kInputBytes = 32; // 256-bit input numbers
        static const char* DefaultDataDir();

        void GenerateDataset(const std::string& file_path, std::size_t N) const;
        Metrics RunFromFile(const std::string& file_path) const;

    private:
        static std::string Trim(const std::string& s);
    };

} // namespace idb

#endif // IDB_H
