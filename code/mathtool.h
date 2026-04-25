#pragma once

#include "macros.h"
#include "config.h"
#include <vector>

// A x db
std::vector<std::vector<unitC>> AXdb(std::vector<std::vector<uint32>>& A, std::vector<std::vector<unitP>>& db);
std::vector<std::vector<uint32>> RE_AXdb(std::vector<std::vector<uint32>>& A, std::vector<std::vector<uint8>>& db);
// qu x db
std::vector<unitC>quXdb(std::vector<uint32>& qu, std::vector<std::vector<unitP>>& db);
// qu x db
std::vector<uint32>RE_quXdb(std::vector<uint32>& qu, std::vector<std::vector<uint8>>& db);
// s x A
std::vector<uint32> sXA(std::vector<uint32>& s, std::vector<std::vector<uint32>>& A);

// sA + e
std::vector<uint32> sAadde(std::vector<uint32>& sA, std::vector<uint32>& e);

// hinti x s
unitC hintiXs(std::vector<unitC>& hinti, std::vector<uint32>& s);

// hint[colindex]
std::vector<unitC> getcol(std::vector<std::vector<unitC>>& hint, uint32 colindex);

// pownumber number 是2的多少次幂
int pownumber(uint32 number);

// 合并一个uniP.data中所有的8bit数组成64bit
uint64 mergernumber(unitP& number);

//用于 kimap 的排序
bool cmp(const kimap& a, const kimap& b);

//判断两个明文unitP是否相等
bool eq(unitP& a, unitP& b);