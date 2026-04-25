#include "IDB.h"

#include <openssl/crypto.h>
#include <openssl/opensslv.h>

#include <fstream>
#include <iomanip>
#include <iostream>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace {

    std::string JoinPath(const std::string& dir, const std::string& file)
    {
        if (dir.empty()) {
            return file;
        }
        const char last = dir[dir.size() - 1];
        if (last == '/' || last == '\\') {
            return dir + file;
        }
        return dir + "\\" + file;
    }

    bool FileExists(const std::string& path)
    {
        std::ifstream fin(path.c_str(), std::ios::binary);
        return static_cast<bool>(fin);
    }

    void PrintMetrics(const idb::Metrics& m)
    {
        std::cout << "N = " << m.N << '\n';
        std::cout << "Serialized group element bytes = " << m.element_bytes << '\n';
        std::cout << std::fixed << std::setprecision(6);
        std::cout << "1. server_to_client = " << m.server_to_client_mb << " MB\n";
        std::cout << "2. client_to_server = " << m.client_to_server_kb << " KB\n";
        std::cout << "3. server_online_time = " << m.server_online_ms << " ms\n";
        std::cout << "4. client_online_time = " << m.client_online_ms << " ms\n";
        std::cout << "5. server_offline_time = " << m.server_offline_ms << " ms\n";
        std::cout << "positive query found = " << (m.positive_found ? "true" : "false") << '\n';
        std::cout << "negative query found = " << (m.negative_found ? "true" : "false") << '\n';
    }

} // namespace

int mainIDBtest()
{
    try {
        std::cout << "OpenSSL version: " << OpenSSL_version(OPENSSL_VERSION) << "\n\n";

        const std::string base_dir = idb::IDB::DefaultDataDir();
        const std::vector<std::pair<std::string, std::size_t> > datasets = {
            std::make_pair(std::string("IDB_16.txt"), std::size_t(1) << 16),
            std::make_pair(std::string("IDB_18.txt"), std::size_t(1) << 18),
            std::make_pair(std::string("IDB_20.txt"), std::size_t(1) << 20),
            std::make_pair(std::string("IDB_22.txt"), std::size_t(1) << 22)
        };

        idb::IDB protocol;

        for (std::size_t i = 0; i < datasets.size(); ++i) {
            const std::string filename = datasets[i].first;
            const std::size_t N = datasets[i].second;
            const std::string path = JoinPath(base_dir, filename);

            std::cout << "============================================================\n";
            std::cout << "Dataset: " << path << '\n';

            if (!FileExists(path)) {
                std::cout << "Generating dataset with N = " << N << " ...\n";
                protocol.GenerateDataset(path, N);
            }
            else {
                std::cout << "Dataset already exists, skip generation.\n";
            }

            const idb::Metrics metrics = protocol.RunFromFile(path);
            PrintMetrics(metrics);

            if (!metrics.positive_found || metrics.negative_found) {
                throw std::runtime_error("Correctness check failed on " + path);
            }

            std::cout << '\n';
        }

        std::cout << "All datasets passed correctness checks.\n";
        return 0;
    }
    catch (const std::exception& ex) {
        std::cerr << "ERROR: " << ex.what() << '\n';
        return 1;
    }
}
