#include "misc.h"
#include "kernel.h"
#include <iostream>

int main() {

    if (!initCommunication()) {
        return -1;
    }
    std::cout << "[+] Communication with kernel has been established" << std::endl;

    std::cout << "[~] Cleaning registry" << std::endl;
    if (!CleanRegistry()) {
        std::cout << "[-] Failed to clean, skip" << std::endl;
    }
    else {
        std::cout << "[+] Registry has been cleaned" << std::endl;
    }

    std::cout << "[~] Get proc id" << std::endl;
  
    float testNumber = 12.342f;
    int errorCount = 0;

    std::cout << "[++] Starting simple read test\n";
    std::cout << "[++] Initial value: " << testNumber << std::endl;
    cr3_update();
   uint64_t baseaddress = get_base_address();
 	std::cout << "[++] Base address: " << std::hex << baseaddress << std::dec << std::endl;
	std::cout << "[++] PEB address: " << std::hex << get_peb_address() << std::dec << std::endl;
    auto start = std::chrono::high_resolution_clock::now();
    int readcount = 1000000;
    // Single test iteration
    for (int i = 0; i < readcount; ++i) {
        float readValue = read<float>(reinterpret_cast<uintptr_t>(&testNumber));
         // Verify result
        if (readValue != testNumber) {
            std::cout << "[-] Mismatch at iteration " << i + 1 << "\n";
            std::cout << "[~] Expected: " << testNumber << "\n";
            std::cout << "[~] Received: " << readValue << "\n";
            errorCount++;
        }
        testNumber = randomFloat();
    }
    auto end = std::chrono::high_resolution_clock::now();
    std::chrono::duration<double, std::nano> duration = end - start;
 
    std::cout << "[~] Test completed\n";
    std::cout << "[~] Duration: " << duration.count() << " ns\n" << "for " <<std::dec<< readcount << " iterations" << std::endl;
    std::cout << "[~] Total errors: " << errorCount << std::endl;
	
	
	Sleep(1000);
	getchar();
    std::cout << "[~] Exiting usermode" << std::endl;

    std::cout << "[+] Usermode exited successfully" << std::endl;
    exitKernel();
    if (req) {
        VirtualFree(req, 0, MEM_RELEASE);
        req = nullptr;
    }
    return 0;
}