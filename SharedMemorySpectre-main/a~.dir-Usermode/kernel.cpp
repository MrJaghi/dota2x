#include "kernel.h"
#include <thread>
#include <Windows.h>
#include "misc.h"
 // Waiting for the kernel mode response
 
#include "gD/Bypass.h"

int dEnd() {
    Sleep(3000);
    exit(1);
}

PWCHAR ACToPWCHAR(const std::string& String)
{
    int BufferSize = MultiByteToWideChar(CP_UTF8, 0, String.c_str(), -1, nullptr, 0);
    if (BufferSize == 0)
    {
        return nullptr;
    }
    PWCHAR Buffer = new WCHAR[BufferSize];
    if (!MultiByteToWideChar(CP_UTF8, 0, String.c_str(), -1, Buffer, BufferSize))
    {
        delete[] Buffer;
        return nullptr;
    }
    return Buffer;
}
void dd_map() {
    WindUnloadDriver(ACToPWCHAR(("C:\\Windows\\System32\\drivers\\sys64x.sys")), TRUE);
    std::getchar();
    Sleep(500); // C:\\Windows\\System32\\Drivers\\i86intel.sys
    WindLoadDriver(ACToPWCHAR(("C:\\Windows\\System32\\Drivers\\gdrv.sys")), ACToPWCHAR(("C:\\Windows\\System32\\drivers\\sys64x.sys")), TRUE);
    Sleep(500);
    WindUnloadDriver(ACToPWCHAR(("C:\\Windows\\System32\\Drivers\\gdrv.sys")), TRUE);
}
std::mutex& getMutex() {
    static std::mutex mtx;
    return mtx;
}
 
// initialisation for the communication with kernel mode
bool initCommunication() {
	req = nullptr;
    req = (Requests*)VirtualAlloc(NULL, sizeof(Requests),
        MEM_COMMIT | MEM_RESERVE,
        PAGE_READWRITE);
    if (req == nullptr) {
        std::cout << "[-] Failed to allocate memory for queue" << std::endl;
        return false;
    }

    memset(req, 0, sizeof(Requests));
    req->g_pid = GetCurrentProcessId(); //getprocessid(L"DriverTester.exe");
	req->Op = op_a;

    std::cout << "[+] Que initialized at address: " << std::hex << reinterpret_cast<uintptr_t>(req) << std::endl;
    // Way to send data to kernel mode
    if (!WriteRegistryDword(HKEY_LOCAL_MACHINE, L"", L"oPid", GetCurrentProcessId())) { return false; }
    if (!WriteRegistryQword(HKEY_LOCAL_MACHINE, L"", L"oAddr", reinterpret_cast<DWORD_PTR>(req))) {
        VirtualFree(req, 0, MEM_RELEASE);
        return false;
    }     
  
    std::cout << "[+] loading the driver" << std::endl;
    std::thread t(dd_map);
    t.detach();;
    Sleep(500);
    std::cout << "--------------------------" << std::endl;
    std::cout << "                            " << std::endl;
    std::cout << "[~] Waiting for kernel" << std::endl;

  
      InterlockedExchange(&req->Ready, 1);

    while (InterlockedCompareExchange(&req->uready, 0, 1) != 1) {
        _mm_pause();   
    }
    InterlockedExchange(&req->uready, 0);

	int buffer = (int)req->buffer;
	std::cout << "buffer: "  << std::dec<< buffer << std::endl;

 
    return true;
}
void cr3_update(){
    req->Op = op_cr3;
	req->InUse = 1;
	InterlockedExchange(&req->Ready, 1);

    while (InterlockedCompareExchange(&req->uready, 0, 1) != 1) {
        _mm_pause();
    }
    InterlockedExchange(&req->uready, 0);
 
     req->InUse = 0;
}
std::uint64_t get_peb_address() {
	req->Op = op_peb;
	req->InUse = 1;
	InterlockedExchange(&req->Ready, 1);
	while (InterlockedCompareExchange(&req->uready, 0, 1) != 1) {
		_mm_pause();
	}
	InterlockedExchange(&req->uready, 0);
	req->InUse = 0;
	return (std::uint64_t)req->buffer;
}
std::uint64_t get_base_address() {
	req->Op = op_base;
	req->InUse = 1;
   
	InterlockedExchange(&req->Ready, 1);

	while (InterlockedCompareExchange(&req->uready, 0, 1) != 1) {
		_mm_pause();
	}
	InterlockedExchange(&req->uready, 0);
    req->InUse = 0;
	return (std::uint64_t)req->baseaddress;
}
void exitKernel() {
	req->Op = op_exit;  
	req->InUse = 1;
    InterlockedExchange(&req->Ready, 1);
	Sleep(1000);
 
  
     std::cout << "[+] Goodbye kernel !" << std::endl;
}