#include "sample_matrix.h"

std::vector<std::vector<uint32>>randomMatrix(int n, int m, uint64 upnumber) {
    std:: vector<std:: vector<uint32>> mat(n, std:: vector<uint32>(m));

    std :: mt19937_64 rng(std:: random_device{}());
    std:: uniform_int_distribution<uint64_t> dist(0, upnumber - 1);

    for (int i = 0; i < n; i++)
    {
        for (int j = 0; j < m; j++)
        {
            mat[i][j] = dist(rng);
        }
    }

    return mat;
}

// 只随机生成行向量
std::vector<uint32> randomVector(int n, uint64 upnumber) {
    std::vector<uint32> mat(n);
    std::mt19937_64 rng(std::random_device{}());
    std::uniform_int_distribution<uint64_t> dist(0, upnumber - 1);
    for (int i = 0; i < n; i++) {
        mat[i] = dist(rng);
    }
    return mat;
}

std::vector<uint8> randomVector8(int n, uint64 upnumber) {
    std::vector<uint8> mat(n);
    std::mt19937_64 rng(std::random_device{}());
    std::uniform_int_distribution<uint64_t> dist(0, upnumber - 1);
    for (int i = 0; i < n; i++) {
        mat[i] = dist(rng);
    }
    return mat;
}