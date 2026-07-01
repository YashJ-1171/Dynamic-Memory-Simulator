#include "pin.H"
#include <iostream>
#include <fstream>
#include <unordered_map>
#include <string>
#include <sstream>

// Output files for diagnostic reporting
std::ofstream OutFile;
std::ofstream JsonFile;

// Structure to track active allocations and their origins
struct AllocNode {
    size_t size;
    ADDRINT callerIp;
    std::string funcName;
    std::string allocType; // malloc, calloc, realloc
};

// Hash map to track active memory allocations for leak detection (Address -> AllocNode)
std::unordered_map<ADDRINT, AllocNode> activeAllocations;

// Hash map to track total allocations per function (Function Name -> Total Bytes)
std::unordered_map<std::string, size_t> allocationsPerFunction;

// Stats tracking
size_t totalAllocatedBytes = 0;
size_t totalFreedBytes = 0;
size_t totalAllocationCount = 0;

// Thread Local Storage (TLS) to pass requested params safely across calls
struct ThreadState {
    size_t pendingSize;
    ADDRINT pendingIp;
    ADDRINT pendingOldPtr; // For realloc
    std::string pendingType;
};
std::unordered_map<THREADID, ThreadState> tlsState;

PIN_LOCK pinLock;

// Helper to resolve IP to function name
std::string ResolveFuncName(ADDRINT ip) {
    std::string funcName = "Unknown_Function";
    PIN_LockClient();
    RTN rtn = RTN_FindByAddress(ip);
    if (RTN_Valid(rtn)) {
        funcName = RTN_Name(rtn);
    }
    PIN_UnlockClient();
    return funcName;
}

// --- Malloc Hooks ---
VOID BeforeMalloc(ADDRINT size, ADDRINT ip, THREADID tid) {
    PIN_GetLock(&pinLock, tid + 1);
    tlsState[tid].pendingSize = size;
    tlsState[tid].pendingIp = ip;
    tlsState[tid].pendingType = "malloc";
    PIN_ReleaseLock(&pinLock);
}

VOID AfterMalloc(ADDRINT retVal, THREADID tid) {
    PIN_GetLock(&pinLock, tid + 1);
    if (retVal != 0) {
        size_t size = tlsState[tid].pendingSize;
        ADDRINT ip = tlsState[tid].pendingIp;
        std::string allocType = tlsState[tid].pendingType;
        std::string funcName = ResolveFuncName(ip);

        activeAllocations[retVal] = {size, ip, funcName, allocType};
        allocationsPerFunction[funcName] += size;
        totalAllocatedBytes += size;
        totalAllocationCount++;
    }
    PIN_ReleaseLock(&pinLock);
}

// --- Calloc Hooks ---
VOID BeforeCalloc(ADDRINT num, ADDRINT size, ADDRINT ip, THREADID tid) {
    PIN_GetLock(&pinLock, tid + 1);
    tlsState[tid].pendingSize = num * size;
    tlsState[tid].pendingIp = ip;
    tlsState[tid].pendingType = "calloc";
    PIN_ReleaseLock(&pinLock);
}

// --- Realloc Hooks ---
VOID BeforeRealloc(ADDRINT oldPtr, ADDRINT newSize, ADDRINT ip, THREADID tid) {
    PIN_GetLock(&pinLock, tid + 1);
    tlsState[tid].pendingSize = newSize;
    tlsState[tid].pendingIp = ip;
    tlsState[tid].pendingOldPtr = oldPtr;
    tlsState[tid].pendingType = "realloc";
    PIN_ReleaseLock(&pinLock);
}

VOID AfterRealloc(ADDRINT retVal, THREADID tid) {
    PIN_GetLock(&pinLock, tid + 1);
    if (retVal != 0) {
        size_t newSize = tlsState[tid].pendingSize;
        ADDRINT ip = tlsState[tid].pendingIp;
        ADDRINT oldPtr = tlsState[tid].pendingOldPtr;
        std::string funcName = ResolveFuncName(ip);

        // If oldPtr existed and was tracked, remove it
        if (oldPtr != 0 && activeAllocations.find(oldPtr) != activeAllocations.end()) {
            totalFreedBytes += activeAllocations[oldPtr].size;
            activeAllocations.erase(oldPtr);
        }

        activeAllocations[retVal] = {newSize, ip, funcName, "realloc"};
        allocationsPerFunction[funcName] += newSize;
        totalAllocatedBytes += newSize;
        totalAllocationCount++;
    }
    PIN_ReleaseLock(&pinLock);
}

// --- Free Hook ---
VOID BeforeFree(ADDRINT addr) {
    if (addr == 0) return;

    PIN_GetLock(&pinLock, 1);
    auto it = activeAllocations.find(addr);
    if (it != activeAllocations.end()) {
        totalFreedBytes += it->second.size;
        activeAllocations.erase(it);
    }
    PIN_ReleaseLock(&pinLock);
}

// Instrumentation routine to hook image loads
VOID ImageLoad(IMG img, VOID *v) {
    // Hook malloc
    RTN mallocRtn = RTN_FindByName(img, "malloc");
    if (RTN_Valid(mallocRtn)) {
        RTN_Open(mallocRtn);
        RTN_InsertCall(mallocRtn, IPOINT_BEFORE, (AFUNPTR)BeforeMalloc,
                       IARG_FUNCARG_ENTRYPOINT_VALUE, 0,
                       IARG_RETURN_IP, IARG_THREAD_ID, IARG_END);
        RTN_InsertCall(mallocRtn, IPOINT_AFTER, (AFUNPTR)AfterMalloc,
                       IARG_FUNCRET_EXITPOINT_VALUE, IARG_THREAD_ID, IARG_END);
        RTN_Close(mallocRtn);
    }

    // Hook calloc
    RTN callocRtn = RTN_FindByName(img, "calloc");
    if (RTN_Valid(callocRtn)) {
        RTN_Open(callocRtn);
        RTN_InsertCall(callocRtn, IPOINT_BEFORE, (AFUNPTR)BeforeCalloc,
                       IARG_FUNCARG_ENTRYPOINT_VALUE, 0,
                       IARG_FUNCARG_ENTRYPOINT_VALUE, 1,
                       IARG_RETURN_IP, IARG_THREAD_ID, IARG_END);
        RTN_InsertCall(callocRtn, IPOINT_AFTER, (AFUNPTR)AfterMalloc,
                       IARG_FUNCRET_EXITPOINT_VALUE, IARG_THREAD_ID, IARG_END);
        RTN_Close(callocRtn);
    }

    // Hook realloc
    RTN reallocRtn = RTN_FindByName(img, "realloc");
    if (RTN_Valid(reallocRtn)) {
        RTN_Open(reallocRtn);
        RTN_InsertCall(reallocRtn, IPOINT_BEFORE, (AFUNPTR)BeforeRealloc,
                       IARG_FUNCARG_ENTRYPOINT_VALUE, 0,
                       IARG_FUNCARG_ENTRYPOINT_VALUE, 1,
                       IARG_RETURN_IP, IARG_THREAD_ID, IARG_END);
        RTN_InsertCall(reallocRtn, IPOINT_AFTER, (AFUNPTR)AfterRealloc,
                       IARG_FUNCRET_EXITPOINT_VALUE, IARG_THREAD_ID, IARG_END);
        RTN_Close(reallocRtn);
    }

    // Hook free
    RTN freeRtn = RTN_FindByName(img, "free");
    if (RTN_Valid(freeRtn)) {
        RTN_Open(freeRtn);
        RTN_InsertCall(freeRtn, IPOINT_BEFORE, (AFUNPTR)BeforeFree,
                       IARG_FUNCARG_ENTRYPOINT_VALUE, 0, IARG_END);
        RTN_Close(freeRtn);
    }
}

// Termination routine: Generates Text and JSON reports
VOID Fini(INT32 code, VOID *v) {
    // --- Text Report ---
    OutFile << "========================================\n";
    OutFile << "   MemGuard Pro Diagnostic Report       \n";
    OutFile << "========================================\n\n";

    OutFile << "--- Memory Allocation by Function ---\n";
    for (auto const& pair : allocationsPerFunction) {
        OutFile << "Function: " << pair.first << " -> Total Allocated: " << pair.second << " bytes\n";
    }

    OutFile << "\n--- Leak Detection ---\n";
    size_t totalLeakedBytes = 0;
    if (activeAllocations.empty()) {
        OutFile << "Status: Clean. No leaks detected.\n";
    } else {
        for (auto const& pair : activeAllocations) {
            const AllocNode& node = pair.second;
            OutFile << "[LEAK] " << node.size << " bytes (" << node.allocType << ") at 0x" 
                    << std::hex << pair.first << std::dec << " (Origin: " << node.funcName << ")\n";
            totalLeakedBytes += node.size;
        }
    }
    
    OutFile << "----------------------------------------\n";
    OutFile << "Total Un-freed Memory: " << totalLeakedBytes << " bytes\n";
    OutFile << "========================================\n";
    OutFile.close();

    // --- JSON Report ---
    JsonFile << "{\n";
    JsonFile << "  \"summary\": {\n";
    JsonFile << "    \"totalAllocatedBytes\": " << totalAllocatedBytes << ",\n";
    JsonFile << "    \"totalFreedBytes\": " << totalFreedBytes << ",\n";
    JsonFile << "    \"totalLeakedBytes\": " << totalLeakedBytes << ",\n";
    JsonFile << "    \"totalAllocationCount\": " << totalAllocationCount << ",\n";
    JsonFile << "    \"leakCount\": " << activeAllocations.size() << "\n";
    JsonFile << "  },\n";

    JsonFile << "  \"functions\": [\n";
    size_t fCount = 0;
    for (auto const& pair : allocationsPerFunction) {
        JsonFile << "    { \"name\": \"" << pair.first << "\", \"allocatedBytes\": " << pair.second << " }";
        if (++fCount < allocationsPerFunction.size()) JsonFile << ",";
        JsonFile << "\n";
    }
    JsonFile << "  ],\n";

    JsonFile << "  \"leaks\": [\n";
    size_t lCount = 0;
    for (auto const& pair : activeAllocations) {
        const AllocNode& node = pair.second;
        JsonFile << "    {\n";
        JsonFile << "      \"address\": \"0x" << std::hex << pair.first << std::dec << "\",\n";
        JsonFile << "      \"size\": " << node.size << ",\n";
        JsonFile << "      \"type\": \"" << node.allocType << "\",\n";
        JsonFile << "      \"origin\": \"" << node.funcName << "\"\n";
        JsonFile << "    }";
        if (++lCount < activeAllocations.size()) JsonFile << ",";
        JsonFile << "\n";
    }
    JsonFile << "  ]\n";
    JsonFile << "}\n";
    JsonFile.close();
}

int main(int argc, char *argv[]) {
    PIN_InitSymbols();
    
    if (PIN_Init(argc, argv)) {
        std::cerr << "Initialization failed" << std::endl;
        return -1;
    }
    
    OutFile.open("memguard_report.txt");
    JsonFile.open("memguard_report.json");
    PIN_InitLock(&pinLock);

    IMG_AddInstrumentFunction(ImageLoad, 0);
    PIN_AddFiniFunction(Fini, 0);

    PIN_StartProgram();
    return 0;
}