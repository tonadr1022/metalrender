#pragma once

#include <string>

#define ARRAY_SIZE(arr) (sizeof(arr) / sizeof((arr)[0]))

std::string binary_rep(size_t val);
