#include "kwpir_sort.h"
#include "mathtool.h"
#include <iostream>
#include <chrono>
#include <algorithm>
#include <random>

void simulate_kwpir_sort(std::vector<unitP> keys, std::vector<unitP> values, uint64 targetkey, std::vector<uint64>& Comm, std::vector<double>& Time) {
	std::cout << "Setup start ...\n";
	auto start = std::chrono::high_resolution_clock::now();
	uint32 dbsize = keys.size();
	std::cout << "dbsize = " << dbsize << "\n";
	std::vector<kimap> ki;
	for (int i = 0; i < dbsize; i++) {
		ki.push_back({ mergernumber(keys[i]), i });
	}
	sort(ki.begin(), ki.end(), cmp);
	srand(time(0));
	uint32 targetpos = 20981;// 1ll * rand() * rand() * rand() % dbsize;
	targetkey = ki[targetpos].k;
	std::cout << "pos = " << targetpos << "\n";
	std::cout << "查询: \ntargetkey = " << targetkey << "\nvalues = ";
	for (auto e : values[ki[targetpos].i].data) {
		std::cout << (int)e << " ";
	}
	puts("");

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
	
	std::vector<uint64> state;
	for (int i = 0; i < sqrtN - 1; i++) {
		uint64 l = sortkeys[(i + 1) * sqrtN - 1]; // 每一行的最后一个数
		//uint64 r = sortkeys[(i + 1) * sqrtN]; // 每行的第一个数
		//std::mt19937_64 rng(std::random_device{}());
		//std::uniform_int_distribution<uint64_t> dist(l, r - 1);
		//if(l + 3 <= r)
		state.push_back(l);
	}
	srand(time(0) + 12345);
	state.push_back(sortkeys[dbsize - 1]); // 为最后一行添加标杆

	uint32 r = (uint32)sqrt(dbsize);
	uint32 c = r;
	std::vector<std::vector<unitP>> simplepirmatrix = getmatrix(r, 0, vals_keys);
	std::vector<std::vector<uint32>> A;


	auto start_ = std::chrono::high_resolution_clock::now();
	std::vector<std::vector<unitC>> hint = Setup(simplepirmatrix, A);
	//std::cout << "sort n m k = " << A.size() << " " << A[0].size() << " " << simplepirmatrix[0].size() << "\n";
	auto end_ = std::chrono::high_resolution_clock::now();
	auto duration_ = std::chrono::duration_cast<std::chrono::nanoseconds>(end_ - start_);
	std::cout << "sort make hint costs time = " << duration_.count() / ((double)1e6) << "\n";

	auto end = std::chrono::high_resolution_clock::now();
	auto duration = std::chrono::duration_cast<std::chrono::nanoseconds>(end - start);
	Time.push_back(duration.count() / ((double)1e6));
	Comm.push_back(1ll * state.size() * 32 + 1ll * hint.size() * hint[0].size() * 32 * hint[0][0].data.size());
	std::cout << "Setup over. \n";


	std::cout << "Query start ...\n";
	start = std::chrono::high_resolution_clock::now();
	auto it = lower_bound(state.begin(), state.end(), targetkey);
	uint32 rowindex = (uint32)(it - state.begin());
	std::vector<uint32> s;
	std::vector<uint32> qu = Query(rowindex, s, r, A);
	end = std::chrono::high_resolution_clock::now();
	duration = std::chrono::duration_cast<std::chrono::nanoseconds>(end - start);
	Time.push_back(duration.count() / ((double)1e6));
	Comm.push_back(1ll * qu.size() * 32);
	std::cout << "Query over.\n";

	std::cout << "Answer start ...\n";
	start = std::chrono::high_resolution_clock::now();
	std::vector<unitC>ans = Answer(simplepirmatrix, qu);
	end = std::chrono::high_resolution_clock::now();
	duration = std::chrono::duration_cast<std::chrono::nanoseconds>(end - start);
	Time.push_back(duration.count() / ((double)1e6));
	Comm.push_back(1ll * ans.size() * 32 * ans[0].data.size());
	std::cout << "Answer over.\n";


	std::cout << "Recover start ...\n";
	start = std::chrono::high_resolution_clock::now();
	int L = 0, R = sqrtN - 1;
	while (L <= R) {
		int mid = (L + R) >> 1;
		//std::cout << "mid = " << mid << "\n";
		unitP d = Recover(hint, ans, s, mid);
		unitP d_key;
		int d_size = d.data.size();
		for (int j = d_size - dataBase.keylen; j < d_size; j++) {
			d_key.data.push_back(d.data[j]);
		}
		uint64 merkey = mergernumber(d_key);
		std::cout << "1 times\n";
		//std::cout << "merkey = " << merkey << "\n";
		if (merkey == targetkey) {
			std::cout << "found  : ";
			for (int j = 0; j < dataBase.vallen; j++) {
				std::cout << (int)d.data[j] << " ";
			}
			std::cout << "\n";
			break;
		}
		else if (merkey < targetkey) {
			L = mid + 1;
		}
		else {
			R = mid - 1;
		}
	}
	end = std::chrono::high_resolution_clock::now();
	duration = std::chrono::duration_cast<std::chrono::nanoseconds>(end - start);
	Time.push_back(duration.count() / ((double)1e6));
	std::cout << "Recover over.\n";

}

void RE_simulate_kwpir_sort(std::vector<unitP> keys, std::vector<unitP> values, uint64 targetkey, std::vector<uint64>& Comm, std::vector<double>& Time) {
	std::cout << "Setup start ...\n";
	auto start = std::chrono::high_resolution_clock::now();
	uint32 dbsize = keys.size();
	std::cout << "dbsize = " << dbsize << "\n";
	std::vector<kimap> ki;
	for (int i = 0; i < dbsize; i++) {
		ki.push_back({ mergernumber(keys[i]), i });
	}
	sort(ki.begin(), ki.end(), cmp);
	srand(time(0));
	uint32 targetpos = 20981;// 1ll * rand() * rand() * rand() % dbsize;
	targetkey = ki[targetpos].k;
	std::cout << "pos = " << targetpos << "\n";
	std::cout << "查询: \ntargetkey = " << targetkey << "\nvalues = ";
	for (auto e : values[ki[targetpos].i].data) {
		std::cout << (int)e << " ";
	}
	puts("");

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
	std::cout << "r1 = " << simplepirmatrix.size() << "  r2 = " << simplepirmatrix[0].size() << "\n";
	std::vector<std::vector<uint32>> A;
	return;
	uint32 r1 = simplepirmatrix.size(), r2 = simplepirmatrix[0].size();
	uint32 r2_ = r2 / (dataBase.keylen + dataBase.vallen); // 每一行有多少个 k||v
	std::vector<uint64> state;
	for (int i = 0; i < r1 - 1; i++) {
		uint64 l = sortkeys[(i + 1) * r2_ - 1];
		state.push_back(l);
	}
	state.push_back(sortkeys[dbsize - 1]); // 为最后一行添加标杆


	auto start_ = std::chrono::high_resolution_clock::now();
	std::vector<std::vector<uint32>> hint = RE_Setup(simplepirmatrix, A);

	auto end_ = std::chrono::high_resolution_clock::now();
	auto duration_ = std::chrono::duration_cast<std::chrono::nanoseconds>(end_ - start_);
	std::cout << "sort make hint costs time = " << duration_.count() / ((double)1e6) << "\n";

	auto end = std::chrono::high_resolution_clock::now();
	auto duration = std::chrono::duration_cast<std::chrono::nanoseconds>(end - start);
	Time.push_back(duration.count() / ((double)1e6));
	Comm.push_back(1ll * state.size() * 32 + 1ll * hint.size() * hint[0].size() * 32);
	std::cout << "Setup over. \n";


	std::cout << "Query start ...\n";
	start = std::chrono::high_resolution_clock::now();
	auto it = lower_bound(state.begin(), state.end(), targetkey);
	uint32 rowindex = (uint32)(it - state.begin());
	std::vector<uint32> s;
	std::vector<uint32> qu = Query(rowindex, s, r1, A);
	end = std::chrono::high_resolution_clock::now();
	duration = std::chrono::duration_cast<std::chrono::nanoseconds>(end - start);
	Time.push_back(duration.count() / ((double)1e6));
	Comm.push_back(1ll * qu.size() * 32);
	std::cout << "Query over.\n";

	std::cout << "Answer start ...\n";
	start = std::chrono::high_resolution_clock::now();
	std::vector<uint32>ans = RE_Answer(simplepirmatrix, qu);
	end = std::chrono::high_resolution_clock::now();
	duration = std::chrono::duration_cast<std::chrono::nanoseconds>(end - start);
	Time.push_back(duration.count() / ((double)1e6));
	Comm.push_back(1ll * ans.size() * 32);
	std::cout << "Answer over.\n";


	std::cout << "Recover start ...\n";
	start = std::chrono::high_resolution_clock::now();
	int L = 0, R = r2_;
	uint32 kvlen = dataBase.keylen + dataBase.vallen;
	while (L <= R) {
		int mid = (L + R) >> 1;
		uint32 to = mid + indexPIR::EPS;
		unitP d_v, d_k;
		for (int j = 0; j < dataBase.vallen; j++) {
			uint8 d_ = RE_Recover(hint, ans, s, to * kvlen + j);
			d_v.data.push_back(d_);
		}
		for (int j = 0; j < dataBase.keylen; j++) {
			uint8 d_ = RE_Recover(hint, ans, s, to * kvlen + j + dataBase.vallen);
			d_k.data.push_back(d_);
		}
		uint64 merkey = mergernumber(d_k);
		std::cout << "1 times\n";
		if (merkey == targetkey) {
			std::cout << "found  : ";
			for (int j = 0; j < dataBase.vallen; j++) {
				std::cout << (int)d_v.data[j] << " ";
			}
			std::cout << "\n";
			break;
		}
		else if (merkey < targetkey) {
			L = mid + 1;
		}
		else {
			R = mid - 1;
		}
	}
	end = std::chrono::high_resolution_clock::now();
	duration = std::chrono::duration_cast<std::chrono::nanoseconds>(end - start);
	Time.push_back(duration.count() / ((double)1e6));
	std::cout << "Recover over.\n";

}