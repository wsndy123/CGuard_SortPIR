#include "simplepir.h"
#include "sample_matrix.h"
#include "mathtool.h"
#include "discrete_gaussian.h"
#include <iostream>
#include <chrono>

// 构建数据库 返回一个二维的向量 
std::vector<std::vector<unitP>> getmatrix(uint32 r, uint32 eps, std::vector<unitP>& data) {

	if (1ll * r * r != data.size()) {
		std::cout << "ERROR\n";
		exit(0);
	}

	uint32 c = r + 2 * eps;
	std::vector<std::vector<unitP>> ret(r, std::vector<unitP>(c));
	int dim3 = data[0].data.size();

	// 先填充主体部分
	for (int i = 0; i < r; i++) {
		for (int j = 0; j < r; j++) {
			ret[i][j + eps] = data[i * r + j];
		}
	}
	// 填充两侧
	
	if (eps) {
		// 右侧
		for (int i = 0; i < r - 1; i++) {
			for (int j = 0; j < eps; j++) {
				ret[i][c - 1 - j] = ret[i + 1][2 * eps - 1 - j];
			}
		}
		uint32 len = dataBase.keylen + dataBase.vallen;
		for (int i = 0; i < eps; i++) {
			for (int j = 0; j < len; j++) {
				ret[r - 1][r + eps + i].data.push_back(0);
			}
		}
		// 左侧
		for (int i = 1; i < r; i++) {
			for (int j = 0; j < eps; j++) {
				ret[i][j] = ret[i - 1][c - 2 * eps - 1 + j];
			}
		}
		for (int i = 0; i < eps; i++) {
			for (int j = 0; j < len; j++) {
				ret[0][i].data.push_back(0);
			}
		}
	}
	return ret;
}

std::vector<std::vector<uint8>> RE_getmatrix(uint32 r, uint32 eps, std::vector<unitP>& data) {
	/*if (1ULL * r * r != data.size()) {
		std::cout << "ERROR: data.size() != r * r\n";
		exit(0);
	}*/
	if (data.empty()) return {};

	uint32 n1 = (uint32)data.size();          // unitP 的个数
	uint32 n2 = (uint32)data[0].data.size();  // 每个 unitP 里 uint8 的个数
	if (n2 == 0) return {};

	// 每行放多少个 unitP，记作 blocks_per_row
	// 则：
	//   行数 rows = ceil(n1 / blocks_per_row)
	//   列数 cols = blocks_per_row * n2
	// 为了让 rows 和 cols 尽量接近，有：
	//   n1 / blocks_per_row ≈ blocks_per_row * n2
	//   => blocks_per_row ≈ sqrt(n1 / n2)

	auto eval_choice = [&](uint32 blocks_per_row,
		uint64& diff,
		uint64& padding_blocks,
		uint32& rows,
		uint32& cols) {
			rows = (n1 + blocks_per_row - 1) / blocks_per_row;
			cols = blocks_per_row * n2;
			diff = (rows > cols) ? (uint64)(rows - cols) : (uint64)(cols - rows);
			padding_blocks = 1ULL * rows * blocks_per_row - n1; // 最后一行补了多少个 unitP
	};

	uint32 lower = std::max<uint32>(1, eps); // 因为后面要复制 eps*n2 个元素，所以核心区至少要有 eps 个 unitP
	long double target_ld = std::sqrt((long double)n1 / (long double)n2);

	std::vector<uint32> cand;
	auto add_cand = [&](uint32 x) {
		if (x < lower) x = lower;
		cand.push_back(x);
	};

	uint32 base_floor = (uint32)std::floor(target_ld);
	uint32 base_ceil = (uint32)std::ceil(target_ld);

	add_cand(lower);
	add_cand(base_floor);
	add_cand(base_ceil);
	if (base_floor > 1) add_cand(base_floor - 1);
	add_cand(base_floor + 1);
	if (base_ceil > 1) add_cand(base_ceil - 1);
	add_cand(base_ceil + 1);
	add_cand(std::max<uint32>(n1, lower)); // 单行方案（或 eps > n1 时的最小可行方案）

	// 去重
	std::sort(cand.begin(), cand.end());
	cand.erase(std::unique(cand.begin(), cand.end()), cand.end());

	uint32 best_blocks_per_row = cand[0];
	uint64 best_diff = ~0ULL, best_padding = ~0ULL;
	uint32 best_rows = 0, best_cols = 0;

	for (uint32 c : cand) {
		uint64 diff, padding_blocks;
		uint32 rows, cols;
		eval_choice(c, diff, padding_blocks, rows, cols);

		if (diff < best_diff ||
			(diff == best_diff && padding_blocks < best_padding) ||
			(diff == best_diff && padding_blocks == best_padding && c < best_blocks_per_row)) {
			best_diff = diff;
			best_padding = padding_blocks;
			best_blocks_per_row = c;
			best_rows = rows;
			best_cols = cols;
		}
	}

	uint32 r1 = best_rows;                 // 行数
	uint32 r2 = best_cols;                 // 核心区列数（不含左右扩展）
	uint32 halo = eps * n2;                // 左右各扩展多少个 uint8

	// 返回矩阵大小：r1 x (r2 + 2*halo)
	// 中间 [halo, halo + r2) 是核心区
	std::vector<std::vector<uint8>> mat(
		r1, std::vector<uint8>(r2 + 2 * halo, (uint8)0)
	);

	// 先把 data 按行填入核心区
	for (uint32 idx = 0; idx < n1; ++idx) {
		uint32 row = idx / best_blocks_per_row;
		uint32 col_block = idx % best_blocks_per_row;
		uint32 col = halo + col_block * n2;
		std::copy(data[idx].data.begin(), data[idx].data.end(), mat[row].begin() + col);
	}

	// 再填左右扩展区
	// 左侧：复制上一行核心区最后 halo 个元素
	// 右侧：复制下一行核心区前 halo 个元素
	for (uint32 i = 0; i < r1; ++i) {
		if (halo > 0) {
			if (i > 0) {
				std::copy(
					mat[i - 1].begin() + halo + r2 - halo,
					mat[i - 1].begin() + halo + r2,
					mat[i].begin()
				);
			}
			if (i + 1 < r1) {
				std::copy(
					mat[i + 1].begin() + halo,
					mat[i + 1].begin() + halo + halo,
					mat[i].begin() + halo + r2
				);
			}
		}
	}

	return mat;
}

std::vector<std::vector<unitC>> Setup(std::vector<std::vector<unitP>>& db, std::vector<std::vector<uint32>>& A) {
	uint32 r = db.size(), c = db[0].size();
	A = randomMatrix(indexPIR::LWE_n, r, indexPIR::CIPHERMOD_q);
	std::vector<std::vector<unitC>> hintc = AXdb(A, db);
	return hintc;
}

std::vector<std::vector<uint32>> RE_Setup(std::vector<std::vector<uint8>>& db, std::vector<std::vector<uint32>>& A) {
	uint32 r1 = db.size(), r2 = db[0].size();
	A = randomMatrix(indexPIR::LWE_n, r1, indexPIR::CIPHERMOD_q);
	std::vector<std::vector<uint32>> hintc = RE_AXdb(A, db);
	return hintc;
}


std::vector<uint32> Query(uint32 rowindex, std::vector<uint32>& s, uint32 elen, std::vector<std::vector<uint32>>& A) {
	if(s.size() == 0) s = randomVector(indexPIR::LWE_n, indexPIR::CIPHERMOD_q);
	std::vector<uint32> e = sample_discrete_gaussian(elen, indexPIR::LWE_sigma);

#ifdef DEBUG
	std::cout << "e = :\n";
	for (int i = 0; i < e.size(); i++) {
		std::cout << e[i] << " ";
	}
	std::cout << "\n";

#endif // DEBUG

	auto sxa = sXA(s, A);
	auto qu = sAadde(sxa, e);
#ifdef DEBUG
	std::cout << "qu = :\n";
	for (int i = 0; i < qu.size(); i++) {
		std::cout << qu[i] << " ";
	}
	std::cout << "\n";
	auto sxa = sXA(s, A);
	std::cout << "sxa = :\n";
	for (int i = 0; i < sxa.size(); i++) {
		std::cout << sxa[i] << " ";
	}
	std::cout << "\n";
#endif // DEBUG
	qu[rowindex] = (1ll * qu[rowindex] + (indexPIR::CIPHERMOD_q / indexPIR::PLAINMOD_p)) % indexPIR::CIPHERMOD_q;

#ifdef DEBUG
	std::cout << "qu = :\n";
	for (int i = 0; i < qu.size(); i++) {
		std::cout << qu[i] << " ";
	}
	std::cout << "\n"; 
#endif // DEBUG

	return qu;
}

std::vector<unitC> Answer(std::vector<std::vector<unitP>>& db, std::vector<uint32>& qu) {
	return quXdb(qu, db);
}

std::vector<uint32> RE_Answer(std::vector<std::vector<uint8>>& db, std::vector<uint32>& qu) {
	return RE_quXdb(qu, db);
}

unitP Recover(std::vector<std::vector<unitC>>& hint, std::vector<unitC>& ans, std::vector<uint32>& s, uint32 colindex) {
	//auto start_ = std::chrono::high_resolution_clock::now();
	unitC ansi = ans[colindex];
	uint64 mod = indexPIR::CIPHERMOD_q, n = indexPIR::LWE_n;
	int dim3 = hint[0][0].data.size();
	unitC hintXs; // = hintiXs(getcol(hint, colindex), s);
	//hintXs.data.resize(dim3);
	//std::cout << "n = " << n << " hintsize = " << hint.size() << " " << hint[0].size() << " " << "ssize = " << s.size() << "\n";
	for (int d3 = 0; d3 < dim3; d3++) {
		uint32 sum = 0;
		for (int i = 0; i < n; i++) {
			sum = (sum + 1ll * hint[i][colindex].data[d3] * s[i] % mod) % mod;
		}
		hintXs.data.push_back(sum);
	}
	unitP res;
	uint32 del = 1 << 24; // q / p
	//int dim3 = ansi.data.size();
	for (int d3 = 0; d3 < dim3; d3++) {
		//dhat.data.push_back((1ll * ansi.data[d3] + indexPIR::CIPHERMOD_q - hintXs.data[d3]) % indexPIR::CIPHERMOD_q);
		uint32 dhati = (1ll * ansi.data[d3] + indexPIR::CIPHERMOD_q - hintXs.data[d3]) % indexPIR::CIPHERMOD_q;
		uint32 bs1 = (dhati / del) % indexPIR::PLAINMOD_p;
		uint32 bs2 = (bs1 + 1) % indexPIR::PLAINMOD_p;
		uint64 bs1del = 1ll * bs1 * del, bs2del = 1ll * bs2 * del;
		int64 jl1 = bs1del - dhati, jl2 = bs2del - dhati;
		jl1 = jl1 < 0 ? (-jl1) : jl1;
		jl2 = jl2 < 0 ? (-jl2) : jl2;
		if (jl1 < jl2) {
			res.data.push_back((uint8)bs1);
		}
		else {
			res.data.push_back((uint8)bs2);
		}
	}
	//auto end_ = std::chrono::high_resolution_clock::now();
	//auto duration_ = std::chrono::duration_cast<std::chrono::nanoseconds>(end_ - start_);
	//std::cout << "************************ Recover n costs time = " << duration_.count() / ((double)1e6) << "\n";
	return res;
}
uint8 RE_Recover(std::vector<std::vector<uint32>>& hint, std::vector<uint32>& ans, std::vector<uint32>& s, uint32 colindex) {
	//auto start_ = std::chrono::high_resolution_clock::now();
	uint32 ansi = ans[colindex];
	//std::cout << "col = " << colindex << "  anscol = " << ans[colindex] << "\n";
	uint64 mod = indexPIR::CIPHERMOD_q, n = indexPIR::LWE_n;
	uint32 hintXs; // = hintiXs(getcol(hint, colindex), s);
	uint32 sum = 0;
	for (int i = 0; i < n; i++) {
		sum = (sum + 1ll * hint[i][colindex] * s[i] % mod) % mod;
		//if (i < 30)
		//std::cout << hint[i][colindex] << " ";
	}
	//std::cout << "\n";
	hintXs = sum;
	uint8 res;
	uint32 del = 1 << 24; // q / p
	//int dim3 = ansi.data.size();
	//for (int d3 = 0; d3 < dim3; d3++) {
		//dhat.data.push_back((1ll * ansi.data[d3] + indexPIR::CIPHERMOD_q - hintXs.data[d3]) % indexPIR::CIPHERMOD_q);
		uint32 dhati = (1ll * ansi + indexPIR::CIPHERMOD_q - hintXs) % indexPIR::CIPHERMOD_q;
		uint32 bs1 = (dhati / del);//% indexPIR::PLAINMOD_p;
		uint32 bs2 = (bs1 + 1);//% indexPIR::PLAINMOD_p;
		uint64 bs1del = 1ll * bs1 * del, bs2del = 1ll * bs2 * del;
		int64 jl1 = bs1del - dhati, jl2 = bs2del - dhati;
		jl1 = jl1 < 0 ? (-jl1) : jl1;
		jl2 = jl2 < 0 ? (-jl2) : jl2;
		if (jl1 < jl2) {
			res = (uint8)(bs1 % indexPIR::PLAINMOD_p);
		}
		else {
			res = (uint8)(bs2 % indexPIR::PLAINMOD_p);
		}
	//}
	//auto end_ = std::chrono::high_resolution_clock::now();
	//auto duration_ = std::chrono::duration_cast<std::chrono::nanoseconds>(end_ - start_);
	//std::cout << "************************ Recover n costs time = " << duration_.count() / ((double)1e6) << "\n";
	return res;
}