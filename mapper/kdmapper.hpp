#pragma once
#include <windows.h>
#include <stdint.h>
#include <vector>
#include "intel_driver.hpp"

namespace kdmapper
{
	uint64_t MapDriver(HANDLE device_handle, const std::vector<uint8_t>& raw_image);
}
