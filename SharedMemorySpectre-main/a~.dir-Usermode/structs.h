#include <Windows.h>
#include <cstdint> // For uint64_t
 


 
// this shit will handle what the usermode wants
typedef enum _req_op {
    op_r = 1,
    op_w = 2,
    op_a = 3,
    op_idle = 0,
    op_base = 51,
    op_cr3 = 4,
    op_peb = 31,
    op_exit = 420

} req_op;
//  req instance that will be use in the que 
typedef struct _req_Obj {
    volatile LONG Ready;       // 1 = request pending, 0 = request done
    volatile LONG uready;       // 1 = request pending, 0 = request done
    volatile LONG InUse;   // 0=free, 1=processing, 2=done
    int g_pid;
    req_op Op;
    uint64_t baseaddress;
    PVOID TargetAddress;
    ULONG Size;
    PVOID buffer;
    NTSTATUS Status;
} Requests, * PreqEntry;
 

typedef struct _req_Obj Requests;










