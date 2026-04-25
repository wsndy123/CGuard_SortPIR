#include "mathtool.h"
#include <Python.h>
#include <iostream>
#include <chrono>

// A x db
std::vector<std::vector<unitC>> AXdb(std::vector<std::vector<uint32>>& A, std::vector<std::vector<unitP>>& db) {
	uint64 mod = indexPIR::CIPHERMOD_q;
	int n = A.size(), m = A[0].size(), k = db[0].size(), dim3 = db[0][0].data.size();
	//std::cout << "A x db debug output: \n";
	std::cout << "n = " << n << " m = " << m << " k = " << k << "\n";
	bool flag = 0;
	if ((k & (k - 1)) == 0) {
		flag = 1;
		unitP ls;
		for (int i = 0; i < dim3; i++) ls.data.push_back(0);
		for (int i = 0; i < m; i++) {
			db[i].push_back(ls); db[i].push_back(ls);
		}
		k+=2;
	}

	//std::cout << "function AXdb debug output: \n n = " << n << " m = " << m << " k = " << k << "\n";
	std::vector<std::vector<unitC>> Adb(n, std::vector<unitC>(k - 1));
	//std::cout << "0000000000000000000 Adb size " << Adb.size() << " " << Adb[0].size() << "\n";
	auto start_ = std::chrono::high_resolution_clock::now();

	// 设置 Python 根目录
	auto _ = _putenv("PYTHONHOME=C:\\Users\\liyongqi\\AppData\\Local\\Programs\\Python\\Python313");
	// 设置 site-packages 路径
	auto __ = _putenv("PYTHONPATH=C:\\Users\\liyongqi\\AppData\\Local\\Programs\\Python\\Python313\\Lib\\site-packages");
	Py_Initialize();
	PyObject* numpy = PyImport_ImportModule("numpy");
	/* A -> Python */
	PyObject* pyA = PyList_New(n);
	for (int i = 0; i < n; i++)
	{
		PyObject* row = PyList_New(m);
		for (int j = 0; j < m; j++)
			PyList_SetItem(row, j, PyLong_FromUnsignedLongLong(A[i][j]));
		PyList_SetItem(pyA, i, row);
	}
	PyObject* array_func = PyObject_GetAttrString(numpy, "array");
	PyObject* npA = PyObject_CallFunctionObjArgs(array_func, pyA, NULL);
	PyObject* dot_func = PyObject_GetAttrString(numpy, "dot");
	for (int d3 = 0; d3 < dim3; d3++) {
		PyObject* pyB = PyList_New(m);
		for (int i = 0; i < m; i++)
		{
			PyObject* row = PyList_New(k);
			for (int j = 0; j < k; j++)
				PyList_SetItem(row, j, PyLong_FromUnsignedLongLong(db[i][j].data[d3]));
			PyList_SetItem(pyB, i, row);
		}
		//PyObject* array_func = PyObject_GetAttrString(numpy, "array");

		//PyObject* npA = PyObject_CallFunctionObjArgs(array_func, pyA, NULL);
		PyObject* npB = PyObject_CallFunctionObjArgs(array_func, pyB, NULL);

		/* C = dot(A,B) */
		//PyObject* dot_func = PyObject_GetAttrString(numpy, "dot");
		PyObject* args = PyTuple_Pack(2, npA, npB);

		PyObject* npC = PyObject_CallObject(dot_func, args);

		/* C % mod */
		PyObject* pyMod = PyLong_FromUnsignedLongLong(mod);
		PyObject* npCmod = PyNumber_Remainder(npC, pyMod);
		for (int i = 0; i < n; i++)
		{
			PyObject* row = PyObject_GetItem(npCmod, PyLong_FromLongLong(i));

			for (int j = 0; j < k - 2; j++)
			{
				PyObject* item = PyObject_GetItem(row, PyLong_FromLongLong(j));

				PyObject* pyInt = PyNumber_Long(item);

				Adb[i][j].data.push_back((uint32)PyLong_AsUnsignedLongLong(pyInt));

				Py_DECREF(pyInt);
				Py_DECREF(item);
			}
			//if(flag)Adb[i].pop_back();
			Py_DECREF(row);
		}
	}
	if(flag)
	for (int i = 0; i < m; i++) {
		db[i].pop_back(); db[i].pop_back();
	}
	auto end_ = std::chrono::high_resolution_clock::now();
	auto duration_ = std::chrono::duration_cast<std::chrono::nanoseconds>(end_ - start_);
	std::cout << "matrix costs time = ********************************** " << duration_.count() / ((double)1e6) << "\n";
	//std::cout << "11111111111111111111111 Adb size " << Adb.size() << " " << Adb[0].size() << "\n";
	/*
	for (int i = 0; i < n; i++) {
		for (int j = 0; j < k; j++) {
			for (int d3 = 0; d3 < dim3; d3++) {
				uint32 sum = 0;
				for (int t = 0; t < m; t++) {
					sum = (sum + 1ll * A[i][t] * db[t][j].data[d3] % mod) % mod;
				}
				Adb[i][j].data.push_back(sum);
			}
		}
	}*/
	return Adb;
}


static bool g_py_inited = false;
static PyObject* g_numpy = nullptr;
static PyObject* g_np_frombuffer = nullptr;
static PyObject* g_np_dot = nullptr;
static PyObject* g_np_float64 = nullptr;
static PyObject* g_np_ascontiguousarray = nullptr;

static bool InitPythonAndNumpyRuntime()
{
	if (g_py_inited) return true;

	// 按你的安装路径写
	_putenv("PYTHONHOME=C:\\Users\\liyongqi\\AppData\\Local\\Programs\\Python\\Python313");
	_putenv("PYTHONPATH=C:\\Users\\liyongqi\\AppData\\Local\\Programs\\Python\\Python313\\Lib\\site-packages");

	if (!Py_IsInitialized()) {
		Py_Initialize();
	}

	g_numpy = PyImport_ImportModule("numpy");
	if (!g_numpy) {
		PyErr_Print();
		std::cerr << "Failed to import numpy.\n";
		return false;
	}

	g_np_frombuffer = PyObject_GetAttrString(g_numpy, "frombuffer");
	g_np_dot = PyObject_GetAttrString(g_numpy, "dot");
	g_np_float64 = PyObject_GetAttrString(g_numpy, "float64");
	g_np_ascontiguousarray = PyObject_GetAttrString(g_numpy, "ascontiguousarray");

	if (!g_np_frombuffer || !g_np_dot || !g_np_float64 || !g_np_ascontiguousarray) {
		PyErr_Print();
		std::cerr << "Failed to get numpy attributes.\n";
		return false;
	}

	g_py_inited = true;
	return true;
}

static void CleanupPythonAndNumpyRuntime()
{
	Py_XDECREF(g_np_ascontiguousarray); g_np_ascontiguousarray = nullptr;
	Py_XDECREF(g_np_float64);           g_np_float64 = nullptr;
	Py_XDECREF(g_np_dot);               g_np_dot = nullptr;
	Py_XDECREF(g_np_frombuffer);        g_np_frombuffer = nullptr;
	Py_XDECREF(g_numpy);                g_numpy = nullptr;

	if (Py_IsInitialized()) {
		Py_Finalize();
	}
	g_py_inited = false;
}

static PyObject* MakeNumpyArrayFromDoubleBuffer(double* data, int rows, int cols)
{
	// 用 memoryview 直接把 C++ 连续内存暴露给 Python
	PyObject* mv = PyMemoryView_FromMemory(
		reinterpret_cast<char*>(data),
		static_cast<Py_ssize_t>(sizeof(double) * (size_t)rows * (size_t)cols),
		PyBUF_READ
	);
	if (!mv) {
		PyErr_Print();
		return nullptr;
	}

	// np.frombuffer(memoryview, dtype=np.float64)
	PyObject* arr1d = PyObject_CallFunctionObjArgs(g_np_frombuffer, mv, g_np_float64, NULL);
	Py_DECREF(mv);
	if (!arr1d) {
		PyErr_Print();
		return nullptr;
	}

	// reshape(rows, cols)
	PyObject* arr2d = PyObject_CallMethod(arr1d, "reshape", "ii", rows, cols);
	Py_DECREF(arr1d);
	if (!arr2d) {
		PyErr_Print();
		return nullptr;
	}

	return arr2d;
}

static bool NumpyDotToDoubleVector(PyObject* npA, PyObject* npB, int rows, int cols, std::vector<double>& out)
{
	PyObject* npC = PyObject_CallFunctionObjArgs(g_np_dot, npA, npB, NULL);
	if (!npC) {
		PyErr_Print();
		return false;
	}

	// 保证是连续数组
	PyObject* npC_contig = PyObject_CallFunctionObjArgs(g_np_ascontiguousarray, npC, NULL);
	Py_DECREF(npC);
	if (!npC_contig) {
		PyErr_Print();
		return false;
	}

	// 转 bytes：这样就不需要 numpy/arrayobject.h
	PyObject* pyBytes = PyObject_CallMethod(npC_contig, "tobytes", NULL);
	Py_DECREF(npC_contig);
	if (!pyBytes) {
		PyErr_Print();
		return false;
	}

	char* buf = nullptr;
	Py_ssize_t len = 0;
	if (PyBytes_AsStringAndSize(pyBytes, &buf, &len) == -1) {
		PyErr_Print();
		Py_DECREF(pyBytes);
		return false;
	}

	size_t expected = sizeof(double) * (size_t)rows * (size_t)cols;
	if ((size_t)len != expected) {
		std::cerr << "Unexpected byte length from NumPy result.\n";
		Py_DECREF(pyBytes);
		return false;
	}

	out.resize((size_t)rows * (size_t)cols);
	std::memcpy(out.data(), buf, expected);

	Py_DECREF(pyBytes);
	return true;
}
std::vector<std::vector<uint32>> RE_AXdb(std::vector<std::vector<uint32>>& A,
	std::vector<std::vector<uint8>>& db)
{
	uint64 mod = indexPIR::CIPHERMOD_q;
	int n = (int)A.size();
	int m = (int)A[0].size();
	int k = (int)db[0].size();

	bool flag = false;
	if ((k & (k - 1)) == 0) {
		flag = true;
		for (int i = 0; i < m; i++) {
			db[i].push_back(0);
			db[i].push_back(0);
		}
		k += 2;
	}

	std::cout << "function AXdb debug output: \n n = " << n << " m = " << m << " k = " << k << "\n";

	std::vector<std::vector<uint32>> Adb(n, std::vector<uint32>(k - 2, 0));

	auto start_ = std::chrono::high_resolution_clock::now();

	if (!InitPythonAndNumpyRuntime()) {
		std::cerr << "InitPythonAndNumpyRuntime failed.\n";
		if (flag) {
			for (int i = 0; i < m; i++) {
				db[i].pop_back();
				db[i].pop_back();
			}
		}
		return Adb;
	}

	// 1) 准备 A_lo, A_hi, B 的连续 double 缓冲区
	std::vector<double> A_lo((size_t)n * (size_t)m);
	std::vector<double> A_hi((size_t)n * (size_t)m);
	std::vector<double> B_d((size_t)m * (size_t)k);

	for (int i = 0; i < n; i++) {
		size_t base = (size_t)i * (size_t)m;
		for (int j = 0; j < m; j++) {
			uint32 x = A[i][j];
			A_lo[base + j] = (double)(x & 0xFFFFu);
			A_hi[base + j] = (double)(x >> 16);
		}
	}

	for (int i = 0; i < m; i++) {
		size_t base = (size_t)i * (size_t)k;
		for (int j = 0; j < k; j++) {
			B_d[base + j] = (double)db[i][j];
		}
	}

	// 2) 包装成 NumPy 数组
	PyObject* npA_lo = MakeNumpyArrayFromDoubleBuffer(A_lo.data(), n, m);
	PyObject* npA_hi = MakeNumpyArrayFromDoubleBuffer(A_hi.data(), n, m);
	PyObject* npB = MakeNumpyArrayFromDoubleBuffer(B_d.data(), m, k);

	if (!npA_lo || !npA_hi || !npB) {
		std::cerr << "Failed to create numpy arrays from buffers.\n";
		Py_XDECREF(npA_lo);
		Py_XDECREF(npA_hi);
		Py_XDECREF(npB);

		if (flag) {
			for (int i = 0; i < m; i++) {
				db[i].pop_back();
				db[i].pop_back();
			}
		}
		return Adb;
	}

	// 3) 两次 float64 dot
	std::vector<double> C_lo;
	std::vector<double> C_hi;

	if (!NumpyDotToDoubleVector(npA_lo, npB, n, k, C_lo)) {
		std::cerr << "NumPy dot for low part failed.\n";
		Py_DECREF(npA_lo);
		Py_DECREF(npA_hi);
		Py_DECREF(npB);

		if (flag) {
			for (int i = 0; i < m; i++) {
				db[i].pop_back();
				db[i].pop_back();
			}
		}
		return Adb;
	}

	if (!NumpyDotToDoubleVector(npA_hi, npB, n, k, C_hi)) {
		std::cerr << "NumPy dot for high part failed.\n";
		Py_DECREF(npA_lo);
		Py_DECREF(npA_hi);
		Py_DECREF(npB);

		if (flag) {
			for (int i = 0; i < m; i++) {
				db[i].pop_back();
				db[i].pop_back();
			}
		}
		return Adb;
	}

	Py_DECREF(npA_lo);
	Py_DECREF(npA_hi);
	Py_DECREF(npB);

	// 4) 精确重组并取模
	for (int i = 0; i < n; i++) {
		size_t base = (size_t)i * (size_t)k;
		for (int j = 0; j < k - 2; j++) {
			uint64 lo = (uint64)std::llround(C_lo[base + j]);
			uint64 hi = (uint64)std::llround(C_hi[base + j]);

			uint64 lo_mod = lo % mod;
			uint64 hi_mod = hi % mod;
			uint64 val = (lo_mod + ((hi_mod << 16) % mod)) % mod;

			Adb[i][j] = (uint32)val;
		}
	}

	if (flag) {
		for (int i = 0; i < m; i++) {
			db[i].pop_back();
			db[i].pop_back();
		}
	}

	auto end_ = std::chrono::high_resolution_clock::now();
	auto duration_ = std::chrono::duration_cast<std::chrono::nanoseconds>(end_ - start_);
	std::cout << "matrix costs time = ********************************** "
		<< duration_.count() / ((double)1e6) << "\n";

	return Adb;
}


std::vector<std::vector<uint32>> RE_AXdb0(std::vector<std::vector<uint32>>& A, std::vector<std::vector<uint8>>& db) {
	uint64 mod = indexPIR::CIPHERMOD_q;
	int n = A.size(), m = A[0].size(), k = db[0].size();
	bool flag = 0;
	if ((k & (k - 1)) == 0) {
		flag = 1;
		//unitP ls;
		//for (int i = 0; i < dim3; i++) ls.data.push_back(0);
		for (int i = 0; i < m; i++) {
			db[i].push_back(0); db[i].push_back(0);
		}
		k += 2;
	}
	std::cout << "function AXdb debug output: \n n = " << n << " m = " << m << " k = " << k << "\n";
	std::vector<std::vector<uint32>> Adb(n, std::vector<uint32>(k - 2));
	//std::cout << "0000000000000000000 Adb size " << Adb.size() << " " << Adb[0].size() << "\n";
	auto start_ = std::chrono::high_resolution_clock::now();

	// 设置 Python 根目录
	auto _ = _putenv("PYTHONHOME=C:\\Users\\liyongqi\\AppData\\Local\\Programs\\Python\\Python313");
	// 设置 site-packages 路径
	auto __ = _putenv("PYTHONPATH=C:\\Users\\liyongqi\\AppData\\Local\\Programs\\Python\\Python313\\Lib\\site-packages");
	Py_Initialize();
	PyObject* numpy = PyImport_ImportModule("numpy");
	/* A -> Python */
	PyObject* pyA = PyList_New(n);
	for (int i = 0; i < n; i++)
	{
		PyObject* row = PyList_New(m);
		for (int j = 0; j < m; j++)
			PyList_SetItem(row, j, PyLong_FromUnsignedLongLong(A[i][j]));
		PyList_SetItem(pyA, i, row);
	}
	PyObject* array_func = PyObject_GetAttrString(numpy, "array");
	PyObject* npA = PyObject_CallFunctionObjArgs(array_func, pyA, NULL);
	PyObject* dot_func = PyObject_GetAttrString(numpy, "dot");
	//for (int d3 = 0; d3 < dim3; d3++) {
		PyObject* pyB = PyList_New(m);
		for (int i = 0; i < m; i++)
		{
			PyObject* row = PyList_New(k);
			for (int j = 0; j < k; j++)
				PyList_SetItem(row, j, PyLong_FromUnsignedLongLong(db[i][j]));
			PyList_SetItem(pyB, i, row);
		}
		//PyObject* array_func = PyObject_GetAttrString(numpy, "array");

		//PyObject* npA = PyObject_CallFunctionObjArgs(array_func, pyA, NULL);
		PyObject* npB = PyObject_CallFunctionObjArgs(array_func, pyB, NULL);

		/* C = dot(A,B) */
		//PyObject* dot_func = PyObject_GetAttrString(numpy, "dot");
		PyObject* args = PyTuple_Pack(2, npA, npB);

		PyObject* npC = PyObject_CallObject(dot_func, args);

		/* C % mod */
		PyObject* pyMod = PyLong_FromUnsignedLongLong(mod);
		PyObject* npCmod = PyNumber_Remainder(npC, pyMod);
		for (int i = 0; i < n; i++)
		{
			PyObject* row = PyObject_GetItem(npCmod, PyLong_FromLongLong(i));
			for (int j = 0; j < k - 2; j++)
			{
				PyObject* item = PyObject_GetItem(row, PyLong_FromLongLong(j));
				PyObject* pyInt = PyNumber_Long(item);
				Adb[i][j] = (uint32)PyLong_AsUnsignedLongLong(pyInt);
				Py_DECREF(pyInt);
				Py_DECREF(item);
			}
			Py_DECREF(row);
		}
	if (flag)
		for (int i = 0; i < m; i++) {
			db[i].pop_back(); db[i].pop_back();
		}
	auto end_ = std::chrono::high_resolution_clock::now();
	auto duration_ = std::chrono::duration_cast<std::chrono::nanoseconds>(end_ - start_);
	std::cout << "matrix costs time = ********************************** " << duration_.count() / ((double)1e6) << "\n";

	return Adb;
}

// qu x db
std::vector<unitC>quXdb(std::vector<uint32>& qu, std::vector<std::vector<unitP>>& db) {
	uint64 mod = indexPIR::CIPHERMOD_q;
	int n = qu.size(), m = db[0].size(), dim3 = db[0][0].data.size();
	std::cout << "func quXdb n m = " << n << " " << m << "\n";
	auto start_ = std::chrono::high_resolution_clock::now();
	std::vector<unitC>qudb(m);
	for (int i = 0; i < m; i++) {
		for (int d3 = 0; d3 < dim3; d3++) {
			uint32 sum = 0;
			for (int j = 0; j < n; j++) {
				sum = (sum + 1ll * qu[j] * db[j][i].data[d3] % mod) % mod;
			}
			qudb[i].data.push_back(sum);
		}
	}
	auto end_ = std::chrono::high_resolution_clock::now();
	auto duration_ = std::chrono::duration_cast<std::chrono::nanoseconds>(end_ - start_);
	std::cout << "matrix costs time =  ********************************** " << duration_.count() / ((double)1e6) << "\n";
	return qudb;
}

// qu x db
std::vector<uint32> RE_quXdb(std::vector<uint32>& qu, std::vector<std::vector<uint8>>& db) {
	uint64 mod = indexPIR::CIPHERMOD_q;
	int n = qu.size(), m = db[0].size();// dim3 = db[0][0].data.size();
	std::cout << "func quXdb n m = " << n << " " << m << "\n";
	auto start_ = std::chrono::high_resolution_clock::now();
	std::vector<uint32>qudb(m);
	for (int i = 0; i < m; i++) {
		//for (int d3 = 0; d3 < dim3; d3++) {
			uint32 sum = 0;
			for (int j = 0; j < n; j++) {
				sum = (sum + 1ll * qu[j] * db[j][i] % mod) % mod;
			}
			qudb[i] = sum;
		//}
	}
	auto end_ = std::chrono::high_resolution_clock::now();
	auto duration_ = std::chrono::duration_cast<std::chrono::nanoseconds>(end_ - start_);
	std::cout << "matrix costs time =  ********************************** " << duration_.count() / ((double)1e6) << "\n";
	return qudb;
}


// s x A
std::vector<uint32> sXA(std::vector<uint32>& s, std::vector<std::vector<uint32>>& A) {
	uint64 mod = indexPIR::CIPHERMOD_q;
	int n = s.size(), m = A[0].size();
	std::vector<uint32> sA;
	for (int i = 0; i < m; i++) {
		uint32 sum = 0;
		for (int j = 0; j < n; j++) {
			sum = (sum + 1ll * s[j] * A[j][i] % mod) % mod;
		}
		sA.push_back(sum);
	}
	return sA;
}

// sA + e
std::vector<uint32> sAadde(std::vector<uint32>& sA, std::vector<uint32>& e) {
	uint32 len = sA.size();
	uint64 mod = indexPIR::CIPHERMOD_q;
	std::vector<uint32> res;
	for (int i = 0; i < len; i++) {
		res.push_back(1ll * sA[i] + e[i] % mod);
	}
	return res;
}

// hinti x s
unitC hintiXs(std::vector<unitC>& hinti, std::vector<uint32>& s) {
	int dim3 = hinti[0].data.size(), len = s.size();
	uint64 mod = indexPIR::CIPHERMOD_q;
	unitC res;
	for (int d3 = 0; d3 < dim3; d3++) {
		uint32 sum = 0;
		for (int i = 0; i < len; i++) {
			sum = (sum + 1ll * hinti[i].data[d3] * s[i] % mod) % mod;
		}
		res.data.push_back(sum);
	}
	return res;
}

// hint[colindex]
std::vector<unitC> getcol(std::vector<std::vector<unitC>>& hint, uint32 colindex) {
	std::vector<unitC> res; 
	int len = hint.size();
	for (int i = 0; i < len; i++) {
		res.push_back(hint[i][colindex]);
	}
	return res;
}

// pownumber number 是2的多少次幂
int pownumber(uint32 number) {
	int js = 0;
	while (! (number & 1)) {
		js++;
		number >>= 1;
	}
	return js;
}

// 合并一个uniP.data中所有的8bit数组成64bit
uint64 mergernumber(unitP& number) {

#ifdef DEBUG0
	for (auto e : number.data) {
		std::cout << (int)e << " ";
	}
	std::cout << "\n";
#endif

	if (number.data.size() != 8) {
		throw "Length does not meet requirements";   // 抛出异常
	}
	uint64 ret = 1ll * number.data[0];
	for (int i = 1; i < 8; i++) {
		ret = (ret << 8) | (1ll * number.data[i]);
	}
	return ret;
}

// 用于 kimap 的排序
bool cmp(const kimap& a, const kimap& b) {
	if (a.k != b.k) return a.k < b.k;
	return a.i < b.i;
}

//判断两个明文unitP是否相等
bool eq(unitP& a, unitP& b) {
	int size_ = a.data.size();
	for (int i = 0; i < size_; i++) {
		if (a.data[i] != b.data[i]) {
			return 0;
		}
	}
	return 1;
}