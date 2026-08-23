#include "service.hpp"
#include <iostream>

namespace service
{
	bool RegisterAndStart(const std::string& driver_path, const std::string& service_name)
	{
		SC_HANDLE scm_handle = OpenSCManagerA(nullptr, nullptr, SC_MANAGER_CREATE_SERVICE);
		if (!scm_handle)
		{
			std::cout << "[-] Failed to open SCManager" << std::endl;
			return false;
		}

		SC_HANDLE service_handle = CreateServiceA(
			scm_handle,
			service_name.c_str(),
			service_name.c_str(),
			SERVICE_ALL_ACCESS,
			SERVICE_KERNEL_DRIVER,
			SERVICE_DEMAND_START,
			SERVICE_ERROR_IGNORE,
			driver_path.c_str(),
			nullptr,
			nullptr,
			nullptr,
			nullptr,
			nullptr
		);

		if (!service_handle)
		{
			if (GetLastError() == ERROR_SERVICE_EXISTS)
			{
				service_handle = OpenServiceA(scm_handle, service_name.c_str(), SERVICE_ALL_ACCESS);
			}
			if (!service_handle)
			{
				CloseServiceHandle(scm_handle);
				return false;
			}
		}

		bool status = StartServiceA(service_handle, 0, nullptr);
		if (!status)
		{
			if (GetLastError() == ERROR_SERVICE_ALREADY_RUNNING)
				status = true;
		}

		CloseServiceHandle(service_handle);
		CloseServiceHandle(scm_handle);
		return status;
	}

	bool StopAndRemove(const std::string& service_name)
	{
		SC_HANDLE scm_handle = OpenSCManagerA(nullptr, nullptr, SC_MANAGER_CREATE_SERVICE);
		if (!scm_handle)
			return false;

		SC_HANDLE service_handle = OpenServiceA(scm_handle, service_name.c_str(), SERVICE_ALL_ACCESS);
		if (!service_handle)
		{
			CloseServiceHandle(scm_handle);
			return false;
		}

		SERVICE_STATUS service_status;
		ControlService(service_handle, SERVICE_CONTROL_STOP, &service_status);
		DeleteService(service_handle);

		CloseServiceHandle(service_handle);
		CloseServiceHandle(scm_handle);
		return true;
	}
}
