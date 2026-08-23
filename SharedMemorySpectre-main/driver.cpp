#include "driver.h"
#include "communication.hpp"
uintptr_t ntos_image_base;
uintptr_t kernel_base;
PVOID sig;

_GLOBAL_UEX GLOBAL_UEX;
_GLOBAL_UEX GLOBAL_UEX2;
 
NTSTATUS main() {
    bool UMalive = true;
    const UINT32 pageIndex = KeGetCurrentProcessorIndex();
	printfx("[+] page index: %d\n", pageIndex);

    PEPROCESS target_process = nullptr;
	bool firsttimepid = false;
	int target_pid = 0;
    
    while (UMalive) {
          
      
         
         if(InterlockedCompareExchange(&req->Ready, 0, 1) == 1){
             InterlockedExchange(&req->Ready, 0);

         switch (req->Op) {
        case op_base:
            game::_baseAddress();
            break;
		case op_peb:
			game::_peb();
			break;
        case op_r:
            game::read();  
            break;
        case op_w:
            game::write();
            break;
        case op_exit:
            UMalive = false;
            break;
        case op_cr3:
			game::_cr3();
            break;
        case op_a:
            req->buffer = (PVOID)212;
			target_pid = req->g_pid;
			printfx("[+] target pid: %d\n", target_pid);
            InterlockedExchange(&req->uready, 1);
            
			//usermode::end();
              
             break;
        }
         if (!UMalive) {
             break;
         }
        }
         YieldProcessor(); // Prevent CPU hogging

    }
    usermode::exit();

  

    return STATUS_SUCCESS;
}

NTSTATUS EP() {
    //getting ntos
    NTSTATUS status = STATUS_SUCCESS;
    ntos_image_base = NTOS::get_ntos_base_address();
    if (ntos_image_base == NULL) {
        silence(E("[vmm] failed to capture ntos information\n"));
        return STATUS_FAIL_CHECK;
    }
    kernel_base = ntos_image_base;
    NTSTATUS response_kboot = bootstrap::kernelbootstrap();
    if (response_kboot != STATUS_SUCCESS) {
        silence(E("[vmm] KBOOT Response Failed.\n"));
        return STATUS_FAIL_CHECK;
    }
    printfx("Start Connection\n");
    if (!NT_SUCCESS(usermode::InitializeCommunication())) { return STATUS_ABANDONED; }
   
 	return main();

}
void UnloadDriver(PDRIVER_OBJECT DriverObject) {

    silence(E("Driver unloaded successfully\n"));
    silence(E("Driver unloaded\n"));
}
NTSTATUS DriverEntry(PDRIVER_OBJECT DriverObject, PUNICODE_STRING RegistryPath)
{
    DriverObject->DriverUnload = UnloadDriver;

    UNREFERENCED_PARAMETER(DriverObject); UNREFERENCED_PARAMETER(RegistryPath);
	printfx("Driver loaded\n");
	 
	 

  
    return EP();
}