#pragma once

#include <Eigen/Core>
#include "modint.h"

namespace Eigen
{

    template<>
    struct NumTraits<ModInt> : GenericNumTraits<ModInt>
    {
        typedef ModInt Real;
        typedef ModInt NonInteger;
        typedef ModInt Nested;

        enum
        {
            IsComplex = 0,
            IsInteger = 1,
            IsSigned = 0,
            RequireInitialization = 1,
            ReadCost = 1,
            AddCost = 2,
            MulCost = 2
        };

        static inline Real epsilon()
        {
            return ModInt(0);
        }

        static inline Real dummy_precision()
        {
            return ModInt(0);
        }

        static inline int digits10()
        {
            return 0;
        }
    };

}