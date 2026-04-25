#include "ourC3pai_op.h"

#include <openssl/crypto.h>

#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>
#include <chrono>
namespace {

    std::string dataset_path_for_N(std::size_t N)
    {
#ifdef _WIN32
        const std::string base = "D:\\KWPIR\\test_data\\ourC3pai_op\\";
#else
        const std::string base = "./ourC3pai_op_data/";
#endif
        return base + "ourC3pai_op_N" + std::to_string(N) + ".bin";
    }

    void ensure_dataset(std::size_t N, std::vector<ourc3pai_op::Record>* records)
    {
        const std::string path = dataset_path_for_N(N);
        if (ourc3pai_op::OurC3PaiOpSystem::load_records_binary(path, records)) {
            return;
        }

        std::cout << "Generating dataset: " << path << std::endl;
        *records = ourc3pai_op::OurC3PaiOpSystem::generate_random_records(N);
        if (!ourc3pai_op::OurC3PaiOpSystem::save_records_binary(path, *records)) {
            throw std::runtime_error("failed to save dataset to " + path);
        }
    }

    std::size_t exact_sqrt(std::size_t N)
    {
        std::size_t r = 1;
        while (r * r < N) {
            ++r;
        }
        if (r * r != N) {
            throw std::runtime_error("N is not a perfect square in this experiment");
        }
        return r;
    }

    void run_one(std::size_t N)
    {
        std::vector<ourc3pai_op::Record> records;
        ensure_dataset(N, &records);

        const std::size_t rows = exact_sqrt(N);
        const std::size_t pick = N / 2;
        const std::uint64_t query_k = records[pick].k;
        const ourc3pai_op::U256 query_mu = records[pick].v;

        ourc3pai_op::OurC3PaiOpSystem sys(257);
        auto start = std::chrono::high_resolution_clock::now();
        if (!sys.build_from_records(records, rows)) {
            throw std::runtime_error("build_from_records failed");
        }
        auto end = std::chrono::high_resolution_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::nanoseconds>(end - start);
        std::cout << "!!!!!!!!!!!!!!!!!!!" << duration.count() / ((double)1e6) << "ms \n";
        //exit(0);
        return;
        ourc3pai_op::QueryResult positive = sys.query(query_k, query_mu);
        if (!positive.found) {
            throw std::runtime_error("correctness check failed on positive query for N=" + std::to_string(N));
        }

        ourc3pai_op::U256 negative_mu = query_mu;
        negative_mu.bytes[31] ^= 0x01u;
        ourc3pai_op::QueryResult negative = sys.query(query_k, negative_mu);
        if (negative.found) {
            throw std::runtime_error("correctness check failed on negative query for N=" + std::to_string(N));
        }

        std::cout << "===== N = " << N << " =====" << std::endl;
        std::cout << "rows = cols = " << rows << std::endl;
        std::cout << "Execution mode = single-thread optimized" << std::endl;
        std::cout << "Paillier modulus bits (actual n bits) = " << sys.paillier_modulus_bits() << std::endl;
        std::cout << "Ciphertext bytes = " << sys.cipher_bytes() << std::endl;
        std::cout << "Positive query found = " << (positive.found ? "true" : "false") << std::endl;
        std::cout << "Negative query found = " << (negative.found ? "true" : "false") << std::endl;
        std::cout << "Server -> client communication (f + ans) = "
            << ourc3pai_op::format_mb(positive.communication.server_to_client_mb) << std::endl;
        std::cout << "Client -> server communication (qu only, requested metric) = "
            << ourc3pai_op::format_kb(positive.communication.client_to_server_qu_kb) << std::endl;
        std::cout << "Client -> server total communication (qu + E(-mu)) = "
            << ourc3pai_op::format_kb(positive.communication.client_to_server_total_kb) << std::endl;
        std::cout << "Server online runtime = "
            << ourc3pai_op::format_ms(positive.timing.server_ms) << std::endl;
        std::cout << "Client runtime = "
            << ourc3pai_op::format_ms(positive.timing.client_ms) << std::endl;
        std::cout << std::endl;
    }

} // namespace

int main()
{
    try {
        std::cout << "OpenSSL version: " << OpenSSL_version(OPENSSL_VERSION) << std::endl;
        std::cout << std::endl;

        const std::vector<std::size_t> sizes = {
            static_cast<std::size_t>(1) << 16,
            static_cast<std::size_t>(1) << 18,
            static_cast<std::size_t>(1) << 20,
            static_cast<std::size_t>(1) << 22
        };

        for (std::size_t i = 0; i < sizes.size(); ++i) {
            run_one(sizes[i]);
        }
        return 0;
    }
    catch (const std::exception& e) {
        std::cerr << "ERROR: " << e.what() << std::endl;
        return 1;
    }
}
