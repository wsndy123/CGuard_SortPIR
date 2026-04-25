#include "ourC3.h"
#include "sample_matrix.h"
#include "discrete_gaussian.h"
#include <chrono>

unitC Regev_Enc(unitP p, std::vector<uint32>& sXAx) {
	int plen = p.data.size();
	unitC ret;
	std::vector<uint32> e = sample_discrete_gaussian(plen, indexPIR::LWE_sigma);
	for (int i = 0; i < plen; i++) {
		ret.data.push_back((1ll * sXAx[i] + e[i] + (1ull << 24) * p.data[i] % indexPIR::CIPHERMOD_q) % indexPIR::CIPHERMOD_q);
	}
	return ret;
}

void simulate_ourC3(std::vector<unitP>& keys, std::vector<unitP>& values, unitP targetu, unitP targetp, std::vector<uint64>& Comm, std::vector<double>& Time) {
	// 所有的C3协议都设置 h1(targetu) 是 64 bit（8B），h2(targetu || targetp) 是 192 bit（24B）
	// Setup
	// Setup: server
	auto start = std::chrono::high_resolution_clock::now();
	uint32 B_len = 32; // 32 B
	uint32 dbsize = keys.size();
	std::cout << "dbsize = " << dbsize << "\n";
	std::vector<kimap> ki;
	for (int i = 0; i < dbsize; i++) {
		ki.push_back({ mergernumber(keys[i]), i });
	}
	sort(ki.begin(), ki.end(), cmp);

	std::vector<uint64> sortkeys;
	std::vector<unitP> sortvalues;
	for (int i = 0; i < dbsize; i++) {
		sortkeys.push_back(ki[i].k);
		sortvalues.push_back(values[ki[i].i]);
	}
	std::vector<unitP> vals_keys;
	for (int i = 0; i < dbsize; i++) {
		vals_keys.push_back(sortvalues[i]);
		for (int j = 0; j < dataBase.keylen; j++) {
			vals_keys[i].data.push_back(keys[ki[i].i].data[j]);
		}
	}
	uint32 sqrtN = (uint32)sqrt(dbsize);
	uint32 r = (uint32)sqrt(dbsize);
	uint32 c = r;
	std::vector<std::vector<uint8>> simplepirmatrix = RE_getmatrix(r, 0, vals_keys);
	std::vector<std::vector<uint32>> A;
	uint32 r1 = simplepirmatrix.size(), r2 = simplepirmatrix[0].size();
	uint32 r2_ = r2 / (dataBase.keylen + dataBase.vallen); // 每一行有多少个 k||v
	std::vector<uint64> state;
	for (int i = 0; i < r1 - 1; i++) {
		uint64 l = sortkeys[(i + 1) * r2_ - 1];
		state.push_back(l);
	}
	state.push_back(sortkeys[dbsize - 1]); // 为最后一行添加标杆

	std::vector<std::vector<uint32>> hint = RE_Setup(simplepirmatrix, A);

	uint32 hintn = hint.size(), hintm = hint[0].size();
	uint32 vectorr_len = hintm, Ax_len = indexPIR::LWE_n;
	
	std::vector<uint8> vectorr = randomVector8(vectorr_len, indexPIR::PLAINMOD_p);
	//for (int i = 0; i < vectorr.size(); i++) vectorr[i] = 1;
	std::vector<std::vector<uint32>> Ax = randomMatrix(Ax_len, B_len, indexPIR::CIPHERMOD_q);

	for (int i = 0; i < hintn; i++) {
		for (int j = 0; j < hintm; j++) {
			hint[i][j] = (uint32)((1ll * hint[i][j] + Ax[i][j % B_len]) % indexPIR::CIPHERMOD_q);
		}
	}
	
	for (int i = 0; i < hintn; i++) {
		for (int j = 0; j < hintm; j++) {
			hint[i][j] = (uint32)((1ll * hint[i][j] * vectorr[j]) % indexPIR::CIPHERMOD_q);
		}
	}
	//Setup: client
	std::vector<uint32> s = randomVector(indexPIR::LWE_n, indexPIR::CIPHERMOD_q);
	
	
	auto end = std::chrono::high_resolution_clock::now();
	auto duration = std::chrono::duration_cast<std::chrono::nanoseconds>(end - start);
	Time.push_back(duration.count() / ((double)1e6));
	Comm.push_back(1ll * state.size() * 64 + 1ll * hint.size() * hint[0].size() * 32 + 1ll * Ax.size() * Ax[0].size() * 32);

	std::vector<uint32> sXhint = sXA(s, hint); // 把 query发过去的同时可以做 不计算时间

	// Query
	start = std::chrono::high_resolution_clock::now();
	std::vector<uint32> sXAx = sXA(s, Ax);
	unitP invmu;
	for (auto _ : targetp.data) invmu.data.push_back(_);
	for (auto _ : targetu.data) invmu.data.push_back(_);
	for (int i = 0; i < invmu.data.size(); i++) {
		invmu.data[i] = (uint8)(indexPIR::PLAINMOD_p - invmu.data[i]);
	}
	uint64 targetkey = mergernumber(targetu);
	//std::cout << "targetkey = " << targetkey << "\n";
	auto it = lower_bound(state.begin(), state.end(), targetkey);
	uint32 rowindex = (uint32)(it - state.begin());
	
	std::vector<uint32> qu = Query(rowindex, s, r1, A);
	
	unitC Einvmu = Regev_Enc(invmu, sXAx);
	end = std::chrono::high_resolution_clock::now();
	duration = std::chrono::duration_cast<std::chrono::nanoseconds>(end - start);
	Time.push_back(duration.count() / ((double)1e6));
	Comm.push_back(1ll * qu.size() * 32 + 1ll * Einvmu.data.size() * 32);

	// Answer 
	start = std::chrono::high_resolution_clock::now();
	std::vector<uint32> ans = RE_Answer(simplepirmatrix, qu);

	int anssize = ans.size();
	for (int i = 0; i < anssize; i++) {
		ans[i] = (1ll * ans[i] + Einvmu.data[i % B_len]) % indexPIR::CIPHERMOD_q;
		ans[i] = (1ll * ans[i] * vectorr[i]) % indexPIR::CIPHERMOD_q;
	}
	end = std::chrono::high_resolution_clock::now();
	duration = std::chrono::duration_cast<std::chrono::nanoseconds>(end - start);
	Time.push_back(duration.count() / ((double)1e6));
	Comm.push_back(1ll * ans.size() * 32);

	// Recover
	start = std::chrono::high_resolution_clock::now();
	uint32 del = 1 << 24; // q / p 
	std::vector<uint8> ansp;
	uint8 res;
	bool find_ = 0;
	for (int i = 0; i < anssize; i ++) {
		uint32 ansi = ans[i];
		uint32 dhati = (1ll * ansi + indexPIR::CIPHERMOD_q - sXhint[i]) % indexPIR::CIPHERMOD_q;
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
		ansp.push_back((uint8)res);
		//std::cout << (int)res << " ";
		if (i == 0) continue;
		//if ((i + 1) % B_len == 0) {
		//	std::cout << "\n";
		//}
		if (((i + 1) % B_len == 0) && res == 0) {
			
			bool flag = 1;
			for (int j = 0; j < B_len; j++) {
				if (ansp[i - j] != 0) {
					flag = 0; break;
				}
			}
			if (flag) {
				std::cout << "found it !!!\n";
				find_ = 1;
				break;
			}
		}
	}
	if(find_ == 0)
		std::cout << "not found it !!! \n";
	end = std::chrono::high_resolution_clock::now();
	duration = std::chrono::duration_cast<std::chrono::nanoseconds>(end - start);
	Time.push_back(duration.count() / ((double)1e6));

}