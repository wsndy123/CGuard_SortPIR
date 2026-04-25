#pragma once
#include "macros.h"
#include <vector>

namespace indexPIR {
	// LWE 参数
	constexpr uint32 LWE_n = 1 << 10;
	constexpr double LWE_sigma = 6.4;
	constexpr uint32 PLAINMOD_p = 1 << 8;
	constexpr uint8 PLAINMOD_len = 8;
	constexpr uint64 CIPHERMOD_q = 1ll << 32;
	constexpr uint8 CIPHERMOD_len = 32;
	constexpr uint8 EPS = 4;
	constexpr uint32 DBSIZE = 1 << 22;
}

struct database {
	int datasize;
	int keylen;
	int vallen;
};
extern database dataBase;

// simplepir 数据
struct unitP {
	std::vector<uint8> data;
};
struct unitC {
	std::vector<uint32> data;
};

struct kimap {
	uint64 k;
	int i;
};