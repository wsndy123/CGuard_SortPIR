#pragma once

#include "config.h"
#include <random>
#include <vector>

std::vector<std::vector<uint32>>randomMatrix(int n, int m, uint64 p);


// 只随机生成行向量
std::vector<uint32> randomVector(int n, uint64 p);

std::vector<uint8> randomVector8(int n, uint64 p);