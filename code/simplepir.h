#pragma once

#include "config.h"
#include <vector>



// 构建数据库 返回一个二维的向量 
std::vector<std::vector<unitP>> getmatrix(uint32 r, uint32 eps, std::vector<unitP>& data);

std::vector<std::vector<unitC>> Setup(std::vector<std::vector<unitP>>& DB, std::vector<std::vector<uint32>>& A);

std::vector<uint32> Query(uint32 rowindex, std:: vector<uint32>& s, uint32 elen, std::vector<std::vector<uint32>>& A);

std::vector<unitC> Answer(std::vector<std::vector<unitP>>& db, std::vector<uint32>& qu);

unitP Recover(std::vector<std::vector<unitC>>& hint, std::vector<unitC>& ans, std::vector<uint32>& s, uint32 colindex);

// 重构
std::vector<std::vector<uint8>> RE_getmatrix(uint32 r, uint32 eps, std::vector<unitP>& data);

std::vector<std::vector<uint32>> RE_Setup(std::vector<std::vector<uint8>>& DB, std::vector<std::vector<uint32>>& A);

std::vector<uint32> RE_Query(uint32 rowindex, std::vector<uint32>& s, uint32 elen, std::vector<std::vector<uint32>>& A);

std::vector<uint32> RE_Answer(std::vector<std::vector<uint8>>& db, std::vector<uint32>& qu);

uint8 RE_Recover(std::vector<std::vector<uint32>>& hint, std::vector<uint32>& ans, std::vector<uint32>& s, uint32 colindex);
