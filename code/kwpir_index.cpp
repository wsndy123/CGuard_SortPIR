#include "kwpir_index.h"
#include "mathtool.h"
#include <iostream>
#include <chrono>
#include <algorithm>

void simulate_kwpir_index(std::vector<unitP> keys, std::vector<unitP> values, uint64 targetkey, std::vector<uint64>& Comm, std::vector<double>& Time) {
	

	/*time1 */
	std::cout << "Setup start ...\n";
	auto start = std::chrono::high_resolution_clock::now();
	/*
		step1: pla前的准备工作
	*/
	uint32 dbsize = keys.size();
	std::vector<kimap> ki;
	for (int i = 0; i < dbsize; i++) {
		ki.push_back({ mergernumber(keys[i]), i });
	}
	sort(ki.begin(), ki.end(), cmp);
	srand(time(0));
	uint32 targetpos = 20977;// 1ll * rand() * rand() * rand() % dbsize;
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
	//std::cout << "step1 over\n";

	/*
		step2: pla构造
	*/
	std::vector<Segment> segment = build_PLA_model(sortkeys);
	//std::cout << "step2 over\n";
	

	/*time 2*/
	//start = std::chrono::high_resolution_clock::now();
	/*
		step3: PIR数据库变换
	*/
	uint32 r = (uint32)sqrt(dbsize);
	uint32 c = r + 2 * indexPIR::EPS;
	std::vector<std::vector<unitP>> simplepirmatrix = getmatrix(r, indexPIR::EPS, vals_keys);
	//std::cout << "step3 over\n";

	/*
		step4: Setup
	*/
	std::vector<std::vector<uint32>> A;

	auto start_ = std::chrono::high_resolution_clock::now();
	std::vector<std::vector<unitC>> hint = Setup(simplepirmatrix, A);
	std::cout << "index n m k = " << A.size() << " " << A[0].size() << " " << simplepirmatrix[0].size() << "\n";
	auto end_ = std::chrono::high_resolution_clock::now();
	auto duration_ = std::chrono::duration_cast<std::chrono::nanoseconds>(end_ - start_);
	std::cout << "index make hint costs time = " << duration_.count() / ((double)1e6) << "\n";
	//std::cout << "step4 over\n";
	auto end = std::chrono::high_resolution_clock::now();
	auto duration = std::chrono::duration_cast<std::chrono::nanoseconds>(end - start);
	Time.push_back(duration.count() / ((double)1e6));
	Comm.push_back(1ll * segment.size() * 32 * 3 + 1ll * hint.size() * hint[0].size() * 32 * hint[0][0].data.size());
	std::cout << "Setup over. \n";


	/*
		step5: Query
	*/

	std::cout << "Query start ...\n";
	start = std::chrono::high_resolution_clock::now();
	uint32 pos = askPLA0(segment, targetkey);
	uint32 rowindex = pos / r, colindex = pos % r;
	
#ifdef DEBUG
	for (int i = (int)colindex - indexPIR::EPS; i <= (int)colindex + indexPIR::EPS; i++) {
		unitP d = simplepirmatrix[rowindex][i + indexPIR::EPS];
		unitP d_key;
		int d_size = d.data.size();
		for (int j = d_size - dataBase.keylen; j < d_size; j++) {
			d_key.data.push_back(d.data[j]);
		}
		std::cout << mergernumber(d_key) << " ";
	}
	std::cout << "\n";
#endif // DEBUG


	std::vector<uint32> s;
	std::vector<uint32> qu = Query(rowindex, s, r, A);
	end = std::chrono::high_resolution_clock::now();
	duration = std::chrono::duration_cast<std::chrono::nanoseconds>(end - start);
	Time.push_back(duration.count() / ((double)1e6));
	Comm.push_back(1ll * qu.size() * 32);
	std::cout << "Query over.\n";
	//std::cout << "step5 over\n";

	/*
		step6: Answer
	*/


	std::cout << "Answer start ...\n";
	start = std::chrono::high_resolution_clock::now();
	std::vector<unitC>ans = Answer(simplepirmatrix, qu);
	end = std::chrono::high_resolution_clock::now();
	duration = std::chrono::duration_cast<std::chrono::nanoseconds>(end - start);
	Time.push_back(duration.count() / ((double)1e6));
	Comm.push_back(1ll * ans.size() * 32 * ans[0].data.size());
	std::cout << "Answer over.\n";
	//std::cout << "step6 over\n";

	/*
		step7: Recover
	*/

	std::cout << "Recover start ...\n";
	start = std::chrono::high_resolution_clock::now();
	for (int i = (int)colindex - indexPIR::EPS; i <= (int)colindex + indexPIR::EPS; i++) {
		std::cout << "1 times\n";
		//auto start_ = std::chrono::high_resolution_clock::now();
		unitP d = Recover(hint, ans, s, i + indexPIR::EPS);
		//auto end_ = std::chrono::high_resolution_clock::now();
		//auto duration_ = std::chrono::duration_cast<std::chrono::nanoseconds>(end_ - start_);
		//std::cout << "Recover 1 times costs time = " << duration_.count() / ((double)1e6) << "\n";
		unitP d_key;
		int d_size = d.data.size();
		for (int j = d_size - dataBase.keylen; j < d_size; j++) {
			d_key.data.push_back(d.data[j]);
		}

		

#ifdef DEBUG
		std::cout << "try_key = " << mergernumber(d_key) << "\n";
#endif // DEBUG


		if (mergernumber(d_key) == targetkey) {
			std::cout << "found  : ";
			for (int j = 0; j < dataBase.vallen; j++) {
				std::cout << (int)d.data[j] << " ";
			}
			std::cout << "\n";
			break;
		}
	}
	end = std::chrono::high_resolution_clock::now();
	duration = std::chrono::duration_cast<std::chrono::nanoseconds>(end - start);
	Time.push_back(duration.count() / ((double)1e6));
	std::cout << "Recover over.\n";
	//std::cout << "step7 over\n";

}


void RE_simulate_kwpir_index(std::vector<unitP> keys, std::vector<unitP> values, uint64 targetkey, std::vector<uint64>& Comm, std::vector<double>& Time) {


	/*time1 */
	std::cout << "Setup start ...\n"; 
	auto start = std::chrono::high_resolution_clock::now();
	/*
		step1: pla前的准备工作
	*/
	uint32 dbsize = keys.size();
	std::vector<kimap> ki;
	for (int i = 0; i < dbsize; i++) {
		ki.push_back({ mergernumber(keys[i]), i });
	}
	sort(ki.begin(), ki.end(), cmp);
	srand(time(0));
	uint32 targetpos = 20977;// 1ll * rand() * rand() * rand() % dbsize;
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
	//std::cout << "step1 over\n";

	/*
		step2: pla构造
	*/
	std::vector<Segment> segment = build_PLA_model(sortkeys);
	//std::cout << "step2 over\n";


	/*time 2*/
	//start = std::chrono::high_resolution_clock::now();
	/*
		step3: PIR数据库变换
	*/
	uint32 r = (uint32)sqrt(dbsize);
	uint32 c = r + 2 * indexPIR::EPS;
	std::vector<std::vector<uint8>> simplepirmatrix = RE_getmatrix(r, indexPIR::EPS, vals_keys);
	//std::cout << "step3 over\n";
	//std::cout << simplepirmatrix.size() << " " << simplepirmatrix[0].size() << "\n"; return;
	/*
		step4: Setup
	*/
	std::vector<std::vector<uint32>> A;

	auto start_ = std::chrono::high_resolution_clock::now();
	std::vector<std::vector<uint32>> hint = RE_Setup(simplepirmatrix, A);
	auto end_ = std::chrono::high_resolution_clock::now();
	auto duration_ = std::chrono::duration_cast<std::chrono::nanoseconds>(end_ - start_);
	std::cout << "index n m k = " << A.size() << " " << A[0].size() << " " << simplepirmatrix[0].size() << "\n";
	std::cout << "index make hint costs time = " << duration_.count() / ((double)1e6) << "\n";
	//std::cout << "step4 over\n";
	auto end = std::chrono::high_resolution_clock::now();
	auto duration = std::chrono::duration_cast<std::chrono::nanoseconds>(end - start);
	Time.push_back(duration.count() / ((double)1e6));
	Comm.push_back(1ll * segment.size() * 32 * 3 + 1ll * hint.size() * hint[0].size() * 32);
	std::cout << "Setup over. \n";


	/*
		step5: Query
	*/

	std::cout << "Query start ...\n";
	start = std::chrono::high_resolution_clock::now();
	uint32 pos = askPLA0(segment, targetkey);
	uint32 r1 = simplepirmatrix.size(), r2 = simplepirmatrix[0].size();
	uint32 r2_ = (r2 - indexPIR::EPS * 2 * (dataBase.keylen + dataBase.vallen)) / (dataBase.keylen + dataBase.vallen); // 每一行有多少个 k||v
	uint32 rowindex = pos / r2_ , colindex = pos % r2_;

#ifdef DEBUG
	for (int i = (int)colindex - indexPIR::EPS; i <= (int)colindex + indexPIR::EPS; i++) {
		unitP d = simplepirmatrix[rowindex][i + indexPIR::EPS];
		unitP d_key;
		int d_size = d.data.size();
		for (int j = d_size - dataBase.keylen; j < d_size; j++) {
			d_key.data.push_back(d.data[j]);
		}
		std::cout << mergernumber(d_key) << " ";
	}
	std::cout << "\n";
#endif // DEBUG


	std::vector<uint32> s;
	std::vector<uint32> qu = Query(rowindex, s, r1, A);
	end = std::chrono::high_resolution_clock::now();
	duration = std::chrono::duration_cast<std::chrono::nanoseconds>(end - start);
	Time.push_back(duration.count() / ((double)1e6));
	Comm.push_back(1ll * qu.size() * 32);
	std::cout << "Query over.\n";
	//std::cout << "step5 over\n";

	/*
		step6: Answer
	*/


	std::cout << "Answer start ...\n";
	start = std::chrono::high_resolution_clock::now();
	std::vector<uint32>ans = RE_Answer(simplepirmatrix, qu);
	end = std::chrono::high_resolution_clock::now();
	duration = std::chrono::duration_cast<std::chrono::nanoseconds>(end - start);
	Time.push_back(duration.count() / ((double)1e6));
	Comm.push_back(1ll * ans.size() * 32);
	std::cout << "Answer over.\n";
	//std::cout << "step6 over\n";

	/*
		step7: Recover
	
	int to = 14;
	for (int i = 0; i < 30; i++) {
		std::cout << ans[to * 256 + i] << " ";
	}
	std::cout << "\n";
	for (int i = 0; i < 30; i++) {
		std::cout << ans[(to + 1) * 256 + i] << " ";
	}

	return;*/
	uint32 kvlen = dataBase.keylen + dataBase.vallen;
	std::cout << "Recover start ...\n";
	start = std::chrono::high_resolution_clock::now();
	for (int i = (int)colindex - indexPIR::EPS; i <= (int)colindex + indexPIR::EPS; i++) {
		std::cout << "1 times\n";
		//auto start_ = std::chrono::high_resolution_clock::now();
		uint32 to = i + indexPIR::EPS;
		//std::cout << "to = " << to << "\n";
		//return;
		unitP d_v, d_k;
		for (int j = 0; j < dataBase.vallen; j++) {
			//std::cout << "col = " << to * kvlen + j << "\n";
			uint8 d_ = RE_Recover(hint, ans, s, to * kvlen + j);
			d_v.data.push_back(d_);
		}
		for (int j = 0; j < dataBase.keylen; j++) {
			uint8 d_ = RE_Recover(hint, ans, s, to * kvlen + j + dataBase.vallen);
			d_k.data.push_back(d_);
		}

		//std::cout << "mergetkey = " << mergernumber(d_k) << "\n";
		if (mergernumber(d_k) == targetkey) {
			
			std::cout << "found  : ";
			for (int j = 0; j < dataBase.vallen; j++) {
				std::cout << (int)d_v.data[j] << " ";
			}
			std::cout << "\n";
			break;
		}
	}
	end = std::chrono::high_resolution_clock::now();
	duration = std::chrono::duration_cast<std::chrono::nanoseconds>(end - start);
	Time.push_back(duration.count() / ((double)1e6));
	std::cout << "Recover over.\n";
	//std::cout << "step7 over\n";

}