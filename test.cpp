#include "test.h"


// 测试对于索引的查询 pla的递归查询于非递归查询 那个快 以及ours的查询
int test1() {

    uint32 N = indexPIR::DBSIZE;
    uint8 eps = indexPIR::EPS;
    std::cout << "数据库大小为 = " << N << "\n";
    std::vector<uint64> keys;
    std::mt19937_64 rng(2147482647ll);
    for (int i = 0; i < N; i++) keys.push_back(rng());
    uint64 targetkey = keys[0];
    std::cout << "targetkey = " << targetkey << "\n";
    sort(keys.begin(), keys.end());
    std::vector<Segment> use0 = build_PLA_model(keys);
    std::cout << "线段数量 = " << use0.size() << "\n";

    std::vector<std::vector<Segment>> levels = buildPLA(keys);

    auto start = std::chrono::high_resolution_clock::now();
    uint32 pos0 = askPLA0(use0, targetkey);
    auto end = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::nanoseconds>(end - start);
    std::cout << "Now faster pir 非递归查询时间 = " << duration.count() << " ns \n";

    start = std::chrono::high_resolution_clock::now();
    uint32 pos1 = askPLA(targetkey, levels);
    end = std::chrono::high_resolution_clock::now();
    duration = std::chrono::duration_cast<std::chrono::nanoseconds>(end - start);
    std::cout << "Now faster pir 递归查询时间 = " << duration.count() << " ns \n";

    uint32 sqrtN = (uint32)sqrt(N);
    std::cout << (sqrtN * sqrtN == N) << "\n";
    std::vector<uint64> state;
    for (int i = 0; i < sqrtN - 1; i++) {
        uint64 l = keys[(i + 1) * sqrtN - 1]; // 每一行的最后一个数
        uint64 r = keys[(i + 1) * sqrtN]; // 每行的第一个数
        std::mt19937_64 rng(std::random_device{}());
        std::uniform_int_distribution<uint64_t> dist(l, r - 1);
        state.push_back(dist(rng));
    }

    srand(time(0) + 12345);
    state.push_back(keys[N - 1] + rand() % 10); // 为最后一行添加标杆
    start = std::chrono::high_resolution_clock::now();
    auto it = lower_bound(state.begin(), state.end(), targetkey);
    int pos = it - state.begin();
    end = std::chrono::high_resolution_clock::now();
    duration = std::chrono::duration_cast<std::chrono::nanoseconds>(end - start);
    std::cout << "our 查询时间 = " << duration.count() << " ns \n";

    return 0;
}