#include "pla.h"
#include <cmath>
#include <algorithm>
#include <iostream>

std:: vector<Segment> build_PLA_model(
    const std:: vector<uint64_t>& keys)
{
    uint32 epsilon = indexPIR::EPS;
    std:: vector<Segment> segs;

    int n = keys.size();

    int start = 0;

    double s_low = -1e30;
    double s_high = 1e30;

    uint64_t k0 = keys[0];
    int y0 = 0;

    for (int i = 1; i < n; i++) {

        uint64_t k = keys[i];
        int y = i;

        if (k == k0) continue;

        double low =
            ((double)(y - epsilon) - y0) /
            (double)(k - k0);

        double high =
            ((double)(y + epsilon) - y0) /
            (double)(k - k0);

        s_low = std:: max(s_low, low);
        s_high = std:: min(s_high, high);

        if (s_low > s_high) {

            double slope =
                (s_low + s_high) / 2;

            double intercept =
                y0 - slope * k0;

            segs.push_back({
                k0,
                slope,
                intercept
                });

            start = i - 1;

            k0 = keys[start];
            y0 = start;

            s_low = -1e30;
            s_high = 1e30;
        }
    }

    double slope = (s_low + s_high) / 2;
    double intercept = y0 - slope * k0;

    segs.push_back({
        k0,
        slope,
        intercept
        });

    return segs;
}

std::vector<std::vector<Segment>> buildPLA(std:: vector<uint64> keys) {
    sort(keys.begin(), keys.end());
    std::vector<std::vector<Segment>> levels;
    int flag = 1;
    while (true) {
        auto M = build_PLA_model(keys);
        if (flag == 1) {
            // 第一次处理 这里可以表示映射通信量
            std::cout << "映射通信量: " << M.size() * 64 * 3 << "bit \n";
            flag = 0;
        }

        levels.push_back(M);
        int m = M.size();
        if (m == 1)
            break;
        keys.clear();
        for (int i = 0; i < m; i++)
            keys.push_back(M[i].key);
    }
    reverse(levels.begin(), levels.end());
    return levels;
}

uint64 askPLA(uint64 key, std::vector<std::vector<Segment>> levels) {

    uint8 epsilon = indexPIR::EPS;
    int pos = levels[0][0].predict(key);

    for (size_t i = 1; i < levels.size(); i++) {

        auto& lvl = levels[i];

        int lo = std:: max(pos - epsilon, 0);
        int hi = std:: min(pos + epsilon, (int)lvl.size() - 1);
        /*auto it = upper_bound(
            lvl.begin() + lo,
            lvl.begin() + hi + 1,
            key,
            [](uint64_t val, const Segment& seg) { return val < seg.key; }
        );
        int s = (it == lvl.begin() + lo) ? lo : (int)(it - lvl.begin() - 1);
        */
        int s = lo;

        for (int j = lo; j <= hi; j++) {

            if (lvl[j].key <= key)
                s = j;
            else
                break;
        }

        int t = std:: min(s + 1, (int)lvl.size() - 1);
        int fs = lvl[s].predict(key);
        int ft = lvl[t].predict(lvl[t].key);
        pos = std:: min(fs, ft);
    }
    return (uint64)pos;
}

// 非递归版本的查询
uint64 askPLA0(const std:: vector<Segment>& segs, uint64_t k) {
    if (segs.empty()) return -1;
    uint32 epsilon = indexPIR::EPS;
    // 找到 segment 对应 key 所在区间
    auto it = upper_bound(segs.begin(), segs.end(), k,
        [](uint64_t val, const Segment& seg) { return val < seg.key; });
    int seg_idx = (it == segs.begin()) ? 0 : (int)(it - segs.begin() - 1);

    const Segment& seg = segs[seg_idx];
    uint64 pos = seg.predict(k);
    return pos;
}