#pragma once

#include "config.h"
#include "simplepir.h"
#include "mathtool.h"
#include <iostream>
#include <chrono>
#include <algorithm>
#include <random>

void simulate_ourC3(std::vector<unitP>& keys, std::vector<unitP>& values, unitP targetu, unitP targetp,std::vector<uint64>& Comm, std::vector<double>& Time);

