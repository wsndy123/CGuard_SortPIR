#include <iostream>
#include <vector>
#include <algorithm>
#include "pla.h"
#include "discrete_gaussian.h"
#include <random>
#include <chrono>
#include "config.h"
#include "macros.h"
#include "mathtool.h"
#include "makedata.h"
#include "kwpir_index.h"
#include "kwpir_sort.h"
#include "ourC3.h"
#include <Python.h>
#include <fstream>
#include <iomanip>
#include "idb.h"

database dataBase;

void pynumpytest() {
    // 设置 Python 根目录
    auto _ = _putenv("PYTHONHOME=C:\\Users\\liyongqi\\AppData\\Local\\Programs\\Python\\Python313");
    // 设置 site-packages 路径
    auto __ = _putenv("PYTHONPATH=C:\\Users\\liyongqi\\AppData\\Local\\Programs\\Python\\Python313\\Lib\\site-packages");

    Py_Initialize();

    int n = 102, m = 102, k = 102;
    uint32_t mod = 100007;

    std::vector<uint32_t> A(n * n);
    std::vector<uint32_t> B(n * n);

    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<uint32_t> dist(0, mod - 1);

    for (int i = 0; i < n * n; i++)
    {
        A[i] = dist(gen);
        B[i] = dist(gen);
    }

    PyObject* numpy = PyImport_ImportModule("numpy");
    auto start = std::chrono::high_resolution_clock::now();
    /* A -> Python */
    PyObject* pyA = PyList_New(n);
    for (int i = 0; i < n; i++)
    {
        PyObject* row = PyList_New(m);
        for (int j = 0; j < m; j++)
            PyList_SetItem(row, j, PyLong_FromUnsignedLong(A[i * m + j]));
        PyList_SetItem(pyA, i, row);
    }

    /* B -> Python */
    PyObject* pyB = PyList_New(m);
    for (int i = 0; i < m; i++)
    {
        PyObject* row = PyList_New(k);
        for (int j = 0; j < k; j++)
            PyList_SetItem(row, j, PyLong_FromUnsignedLong(B[i * k + j]));
        PyList_SetItem(pyB, i, row);
    }

    PyObject* array_func = PyObject_GetAttrString(numpy, "array");

    PyObject* npA = PyObject_CallFunctionObjArgs(array_func, pyA, NULL);
    PyObject* npB = PyObject_CallFunctionObjArgs(array_func, pyB, NULL);

    /* C = dot(A,B) */
    PyObject* dot_func = PyObject_GetAttrString(numpy, "dot");
    PyObject* args = PyTuple_Pack(2, npA, npB);

    PyObject* npC = PyObject_CallObject(dot_func, args);

    /* C % mod */
    PyObject* pyMod = PyLong_FromUnsignedLong(mod);
    PyObject* npCmod = PyNumber_Remainder(npC, pyMod);

    /* 转回C++ */
    std::vector<uint32_t> C(n * k);

    for (int i = 0; i < n; i++)
    {
        PyObject* row = PyObject_GetItem(npCmod, PyLong_FromLong(i));

        for (int j = 0; j < k; j++)
        {
            PyObject* item = PyObject_GetItem(row, PyLong_FromLong(j));

            PyObject* pyInt = PyNumber_Long(item);

            C[i * k + j] = (uint32_t)PyLong_AsUnsignedLong(pyInt);

            Py_DECREF(pyInt);
            Py_DECREF(item);
        }

        Py_DECREF(row);
    }
    auto end = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::nanoseconds>(end - start);
    std::cout << "numpy 计算两个 1024 x 1024 的矩阵乘法用时 " << duration.count() / ((double)1e9) << " s \n";


    /* 输出 */


    Py_Finalize();
    return ;
}

void ourC3test() {

    // 通信量：只测离线阶段和在线阶段就好
    // 用时：也是只测离线阶段和在线阶段

    srand(time(0));
    std::vector<std::vector<uint32>> testdata = {
        //{1 << 16, 8, 24},
        //{1 << 18, 8, 24},
        //{1 << 20, 8, 24}
        {1 << 22, 8, 24}
    };
    std::vector<uint64> Comm;
    std::vector<double> Time;
    for (auto test : testdata) {
        dataBase.datasize = test[0];
        dataBase.keylen = test[1];
        dataBase.vallen = test[2];
        std::vector<unitP> keys, values;
        std::cout << "数据库大小: 2^" << pownumber(dataBase.datasize) << " 个元素.   键长度：" << dataBase.keylen << " 个8bit.   值长度："
            << dataBase.vallen << " 个8bit.   查询元素长度：" << dataBase.keylen + dataBase.vallen << " 个8bit \n";
        makedata();
        std::cout << "getdata start. \n";
        auto start = std::chrono::high_resolution_clock::now();
        getdata(keys, values);
        auto end = std::chrono::high_resolution_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::nanoseconds>(end - start);
        double getdatatime = duration.count() / ((double)1e6);
        unitP targetu, targetp;
        targetu = keys[111], targetp = values[111];
        simulate_ourC3(keys, values, targetu, targetp, Comm, Time);
        std::cout << "ourC3: ";
        std::cout << std::fixed << std::setprecision(6)
            << 1.0 * (Comm[0] + Comm[2]) / 8.0 / 1024.0 / 1024.0 << "MB  "
            << 1.0 * Comm[1] / 8.0 / 1024.0 / 1024.0 << "MB  "
            << Time[2] << "ms  "
            << Time[1] + Time[3] << "ms  "
            << Time[0] << "ms  \n";
        /*
        std::cout << "ourC3: ";
            std::cout << std::fixed << std::setprecision(6)
            << 1.0 * Comm[0] / 8.0 / 1024.0 / 1024.0 << "MB  "
            << 1.0 * (Comm[1] + Comm[2]) / 8.0 / 1024.0 / 1024.0 << "MB  "
            << Time[0] + getdatatime << "ms   "
            << Time[1] + Time[2] + Time[3] << "ms   \n";
        */
    }
}


int maintt() {
    // 我们采取的策略是：先发送桶标识符 再把该桶的hint发过来 再进行后续交互 
    //IDB_makedata();

    //ourC3test();

    //pynumpytest();

    //IDBtest();
    //return 0;

    std::vector<std::vector<uint32>> testdata = {
        // databasesize  keylen(/8bit)  valuelen(/8bit)  128B
       //{1 << 16, 8, 24},
        //{1 << 16, 8, 56},
       //{1 << 16, 8, 120},
        //{1 << 16, 8, 248},
        //{1 << 17, 8, 120},
        //{1 << 19, 8, 120},
        //{1 << 21, 8, 120},
        //{1 << 18, 8, 24},
        //{1 << 18, 8, 56},
       //{1 << 18, 8, 120},
        //{1 << 18, 8, 248},
         //{1 << 20, 8, 24},
        //{1 << 20, 8, 56},
        //{1 << 20, 8, 120},
        //{1 << 20, 8, 248},
         {1 << 22, 8, 24},
        //{1 << 22, 8, 56},
        //{1 << 22, 8, 120},
        //{1 << 22, 8, 248}
    };


    // 时间 ms
    // 通信 bit
    std::vector<std:: vector<uint64> > Comm_index, Comm_sort;
    std::vector<std::vector<double>> Time_index, Time_sort;
    for (auto test : testdata) {
        dataBase.datasize = test[0];
        dataBase.keylen = test[1];
        dataBase.vallen = test[2];
        std::vector<unitP> keys, values;
        std::cout << "数据库大小: 2^" << pownumber(dataBase.datasize) << " 个元素.   键长度：" << dataBase.keylen << " 个8bit.   值长度：" 
            << dataBase.vallen << " 个8bit.   查询元素长度：" << dataBase.keylen + dataBase.vallen << " 个8bit \n";
        std::cout << "makedata. \n";
        makedata();
        std::cout << "getdata start. \n";
        auto start = std::chrono::high_resolution_clock::now();
        getdata(keys, values);
        auto end = std::chrono::high_resolution_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::nanoseconds>(end - start);
        double getdatatime = duration.count() / ((double)1e6);

        std::cout << "getdata over. \n";
        uint64 targetkey = mergernumber(keys[111]);

        std::vector<uint64> comm;
        std::vector<double> time;
        std::vector<uint64> comm_;
        std::vector<double> time_;

        //RE_simulate_kwpir_index(keys, values, targetkey, comm_, time_);
        //return 0;
        RE_simulate_kwpir_sort(keys, values, targetkey, comm, time);
        std::cout << "sort :";
        std::cout << std::fixed << std::setprecision(4)
            << 1.0 * comm[0] / 8.0 / 1024.0 / 1024.0 << "MB  "
            << 1.0 * (comm[1] + comm[2]) / 8.0 / 1024.0 / 1024.0 << "MB  "
            << time[0] + getdatatime << "ms   "
            << time[1] + time[2] + time[3] << "ms   "
            << time[2] << "ms   ";
        continue;
        /*std::cout << std::fixed << std::setprecision(4)
            << 1.0 * comm[0] / 8.0 / 1024.0 / 1024.0 << "MB  "
            << time[0] + getdatatime << "ms   "
            << 1.0 * comm[1] / 8.0 / 1024.0 / 1024.0 << "MB  "
            << 1.0 * comm[2] / 8.0 / 1024.0 / 1024.0 << "MB  "
            << 1.0 * (comm[1] + comm[2]) / 8.0 / 1024.0 / 1024.0 << "MB  "
            << time[1] << "ms   "
            << time[2] << "ms   "
            << time[3] << "ms   "
            << time[1] + time[2] + time[3] << "ms   ";*/
        std::cout << "\n";
        //return 0;

        
        Comm_index.push_back(comm_);
        Time_index.push_back(time_);
        std::cout << "index:";
        std::cout << std::fixed << std::setprecision(4)
            << 1.0 * comm_[0] / 8.0 / 1024.0 / 1024.0 << "MB  "
            << 1.0 * (comm_[1] + comm_[2]) / 8.0 / 1024.0 / 1024.0 << "MB  "
            << time_[0] + getdatatime << "ms   "
            << time_[1] + time_[2] + time_[3] << "ms   "
            << time_[2] << "ms   ";
        /*std::cout << std::fixed << std::setprecision(4)
            << 1.0 * comm_[0] / 8.0 / 1024.0 / 1024.0 << "MB  "
            << time_[0] + getdatatime << "ms   "
            << 1.0 * comm_[1] / 8.0 / 1024.0 / 1024.0 << "MB  "
            << 1.0 * comm_[2] / 8.0 / 1024.0 / 1024.0 << "MB  "
            << 1.0 * (comm_[1] + comm_[2]) / 8.0 / 1024.0 / 1024.0 << "MB  "
            << time_[1] << "ms   "
            << time_[2] << "ms   "
            << time_[3] << "ms   "
            << time_[1] + time_[2] + time_[3] << "ms   "; */
        std::cout << "\n";
        //return 0;

        
        
        



        //auto end = std::chrono::high_resolution_clock::now();
        //auto duration = std::chrono::duration_cast<std::chrono::nanoseconds>(end - start);
        //std::cout << "用时 " << duration.count() / ((double)1e6) << " ms \n";
        //break;
    }
    return 0;
    std::string outfile_index = "D:\\KWPIR\\test_data\\index_max_bug.txt";
    std::ofstream fout;
    fout.open(outfile_index);
    for (auto a : Comm_index) {
        for (auto b : a) {
            fout << b << " ";
        }
        fout << "\n";
    }
    for (auto a : Time_index) {
        for (auto b : a) {
            fout << b << " ";
        }
        fout << "\n";
    }

}