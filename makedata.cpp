#include "makedata.h"
#include "config.h"
#include <random>
#include <cstring>
#include <string>
#include "mathtool.h"
#include <fstream>
#include <filesystem>

void IDB_makedata() {
    const uint64_t ROWS = 1ULL << 22;  // 2^20

    std::ofstream fout("D:\\KWPIR\\test_data\\IDB_22_8_8.txt");


    std::random_device rd;
    std::mt19937_64 rng(rd());

    for (uint64_t i = 0; i < ROWS; ++i) {
        uint64_t a = rng();
        uint64_t b = rng();
        fout << a << ' ' << b << '\n';
    }

    fout.close();
    std::cout << "Generated " << ROWS << " rows into data.txt\n";
}

void makedata() {

    std::random_device rd;            // 随机种子
    std::mt19937 gen(rd());           // 随机数引擎

    std::string s = std::to_string(pownumber(dataBase.datasize)) + "_" +
        std::to_string(dataBase.keylen) + "_" +
        std::to_string(dataBase.vallen);
    ;
    std::string filepath = "D:\\KWPIR\\test_data\\" + s + ".txt";
    
    std::ifstream file(filepath, std::ios::ate);

    if (file && file.tellg() > 0) {
        //file exists and not empty;
        std::cout << filepath << "已存在且不为空。\n";
        return;
    }
    
    std::ofstream fout;
    fout.open(filepath);

    std::uniform_int_distribution<> dist(1, indexPIR::PLAINMOD_p - 1);

    for (int i = 0; i < dataBase.datasize; i++) {
        for (int j = 0; j < dataBase.keylen + dataBase.vallen; j++) {
            fout << dist(gen) << " ";
        }
        fout << "\n";
    }
    fout.close();
}

void getdata(std::vector<unitP>& keys, std::vector<unitP>& values) {
    std::string s = std::to_string(pownumber(dataBase.datasize)) + "_" +
        std::to_string(dataBase.keylen) + "_" +
        std::to_string(dataBase.vallen);
    std::string filepath = "D:\\KWPIR\\test_data\\" + s + ".txt";
    std::ifstream fin;
    fin.open(filepath);
    for (int i = 0; i < dataBase.datasize; i++) {
        unitP ls, rs; int numberls;
        for (int j = 0; j < dataBase.keylen; j++) {
            fin >> numberls;
            ls.data.push_back((uint8)numberls);
        }
        keys.push_back(ls);
        for (int j = 0; j < dataBase.vallen; j++) {
            fin >> numberls;
            rs.data.push_back((uint8)numberls);
        }
        values.push_back(rs);
    }
}