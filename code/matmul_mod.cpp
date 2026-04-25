#include "matmul_mod.h"
#include "modint.h"
#include <Eigen/Dense>

namespace Eigen {

    template<>
    struct NumTraits<ModInt> : NumTraits<uint64_t> {

        typedef ModInt Real;
        typedef ModInt NonInteger;
        typedef ModInt Nested;

        enum {
            IsComplex = 0,
            IsInteger = 1,
            IsSigned = 0,
            RequireInitialization = 1,
            ReadCost = 1,
            AddCost = 2,
            MulCost = 2
        };
    };

}

std::vector<std::vector<uint64_t>> matmul_mod(
    const std::vector<std::vector<uint32_t>>& A,
    const std::vector<std::vector<uint32_t>>& B,
    uint64_t mod)
{

    ModInt::MOD = mod;

    int n = A.size();
    int k = A[0].size();
    int m = B[0].size();

    using Mat = Eigen::Matrix<ModInt, Eigen::Dynamic, Eigen::Dynamic, Eigen::RowMajor>;

    Mat eA(n, k);
    Mat eB(k, m);

    for (int i = 0; i < n; i++)
        for (int j = 0; j < k; j++)
            eA(i, j) = ModInt(A[i][j]);

    for (int i = 0; i < k; i++)
        for (int j = 0; j < m; j++)
            eB(i, j) = ModInt(B[i][j]);

    Mat eC = eA * eB;

    std::vector<std::vector<uint64_t>> C(n, std::vector<uint64_t>(m));

    for (int i = 0; i < n; i++)
        for (int j = 0; j < m; j++)
            C[i][j] = eC(i, j).v;

    return C;
}