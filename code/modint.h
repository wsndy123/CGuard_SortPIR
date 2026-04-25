#pragma once

#include <cstdint>
#include <limits>

struct ModInt
{
    using Scalar = uint64_t;

    static uint64_t MOD;

    uint64_t v;

    // constructors
    ModInt() : v(0) {}

    ModInt(uint64_t x)
    {
        v = x % MOD;
    }

    // basic arithmetic

    inline ModInt operator+(const ModInt& o) const
    {
        uint64_t r = v + o.v;
        if (r >= MOD) r -= MOD;
        return ModInt(r);
    }

    inline ModInt operator-(const ModInt& o) const
    {
        uint64_t r = v >= o.v ? v - o.v : v + MOD - o.v;
        return ModInt(r);
    }

    inline ModInt operator*(const ModInt& o) const
    {
        return ModInt((v * o.v) % MOD);
    }

    inline ModInt operator-() const
    {
        return v ? ModInt(MOD - v) : ModInt(0);
    }

    // compound ops

    inline ModInt& operator+=(const ModInt& o)
    {
        v += o.v;
        if (v >= MOD) v -= MOD;
        return *this;
    }

    inline ModInt& operator-=(const ModInt& o)
    {
        v = (v >= o.v) ? v - o.v : v + MOD - o.v;
        return *this;
    }

    inline ModInt& operator*=(const ModInt& o)
    {
        v = (v * o.v) % MOD;
        return *this;
    }

    // comparisons

    inline bool operator==(const ModInt& o) const
    {
        return v == o.v;
    }

    inline bool operator!=(const ModInt& o) const
    {
        return v != o.v;
    }

    inline bool operator<(const ModInt& o) const
    {
        return v < o.v;
    }

    inline bool operator>(const ModInt& o) const
    {
        return v > o.v;
    }

    // conversion

    explicit operator uint64_t() const
    {
        return v;
    }
};