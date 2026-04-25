#pragma once

#include "config.h"
#include "simplepir.h"
#include "pla.h"

void simulate_kwpir_index(std::vector<unitP> keys, std::vector<unitP> values, uint64 targetkey, std:: vector<uint64>& Comm, std:: vector<double>& Time);

void RE_simulate_kwpir_index(std::vector<unitP> keys, std::vector<unitP> values, uint64 targetkey, std::vector<uint64>& Comm, std::vector<double>& Time);
