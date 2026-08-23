#pragma once
#include <windows.h>
#include <stdint.h>
#include <string>

namespace service
{
	bool RegisterAndStart(const std::string& driver_path, const std::string& service_name);
	bool StopAndRemove(const std::string& service_name);
}
