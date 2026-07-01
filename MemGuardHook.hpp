#ifndef MEMGUARD_HOOK_HPP
#define MEMGUARD_HOOK_HPP

#include <iostream>
#include <fstream>
#include <unordered_map>
#include <string>
#include <sstream>
#include <cstdlib>
#include <vector>
#include <iomanip>

namespace MemGuard {

struct AllocNode {
    size_t size;
    std::string funcName;
    std::string allocType; // malloc, calloc, realloc, new
};

class Sentinel {
private:
    std::unordered_map<void*, AllocNode> activeAllocations;
    std::unordered_map<std::string, size_t> allocationsPerFunction;
    size_t totalAllocatedBytes = 0;
    size_t totalFreedBytes = 0;
    size_t totalAllocationCount = 0;
    bool active = true;

public:
    static Sentinel& getInstance() {
        static Sentinel instance;
        return instance;
    }

    ~Sentinel() {
        generateReport();
    }

    void recordAllocation(void* ptr, size_t size, const std::string& type, const std::string& origin) {
        if (!ptr || !active) return;
        activeAllocations[ptr] = {size, origin, type};
        allocationsPerFunction[origin] += size;
        totalAllocatedBytes += size;
        totalAllocationCount++;
    }

    void recordFree(void* ptr) {
        if (!ptr || !active) return;
        auto it = activeAllocations.find(ptr);
        if (it != activeAllocations.end()) {
            totalFreedBytes += it->second.size;
            activeAllocations.erase(it);
        }
    }

    void recordRealloc(void* oldPtr, void* newPtr, size_t newSize, const std::string& origin) {
        if (!active) return;
        if (oldPtr && activeAllocations.find(oldPtr) != activeAllocations.end()) {
            totalFreedBytes += activeAllocations[oldPtr].size;
            activeAllocations.erase(oldPtr);
        }
        if (newPtr) {
            activeAllocations[newPtr] = {newSize, origin, "realloc"};
            allocationsPerFunction[origin] += newSize;
            totalAllocatedBytes += newSize;
            totalAllocationCount++;
        }
    }

    void generateReport() {
        if (!active) return;
        active = false; // Prevent recursive calls during teardown

        // --- Text Report ---
        std::ofstream outFile("memguard_report.txt");
        outFile << "========================================\n";
        outFile << "   MemGuard Pro Diagnostic Report       \n";
        outFile << "========================================\n\n";

        outFile << "--- Memory Allocation by Function ---\n";
        for (auto const& pair : allocationsPerFunction) {
            outFile << "Function: " << pair.first << " -> Total Allocated: " << pair.second << " bytes\n";
        }

        outFile << "\n--- Leak Detection ---\n";
        size_t totalLeakedBytes = 0;
        if (activeAllocations.empty()) {
            outFile << "Status: Clean. No leaks detected.\n";
        } else {
            for (auto const& pair : activeAllocations) {
                const AllocNode& node = pair.second;
                outFile << "[LEAK] " << node.size << " bytes (" << node.allocType << ") at 0x" 
                        << std::hex << reinterpret_cast<uintptr_t>(pair.first) << std::dec 
                        << " (Origin: " << node.funcName << ")\n";
                totalLeakedBytes += node.size;
            }
        }
        
        outFile << "----------------------------------------\n";
        outFile << "Total Un-freed Memory: " << totalLeakedBytes << " bytes\n";
        outFile << "========================================\n";
        outFile.close();

        // --- JSON Report ---
        std::ofstream jsonFile("memguard_report.json");
        jsonFile << "{\n";
        jsonFile << "  \"summary\": {\n";
        jsonFile << "    \"totalAllocatedBytes\": " << totalAllocatedBytes << ",\n";
        jsonFile << "    \"totalFreedBytes\": " << totalFreedBytes << ",\n";
        jsonFile << "    \"totalLeakedBytes\": " << totalLeakedBytes << ",\n";
        jsonFile << "    \"totalAllocationCount\": " << totalAllocationCount << ",\n";
        jsonFile << "    \"leakCount\": " << activeAllocations.size() << "\n";
        jsonFile << "  },\n";

        jsonFile << "  \"functions\": [\n";
        size_t fCount = 0;
        for (auto const& pair : allocationsPerFunction) {
            jsonFile << "    { \"name\": \"" << pair.first << "\", \"allocatedBytes\": " << pair.second << " }";
            if (++fCount < allocationsPerFunction.size()) jsonFile << ",";
            jsonFile << "\n";
        }
        jsonFile << "  ],\n";

        jsonFile << "  \"leaks\": [\n";
        size_t lCount = 0;
        for (auto const& pair : activeAllocations) {
            const AllocNode& node = pair.second;
            jsonFile << "    {\n";
            jsonFile << "      \"address\": \"0x" << std::hex << reinterpret_cast<uintptr_t>(pair.first) << std::dec << "\",\n";
            jsonFile << "      \"size\": " << node.size << ",\n";
            jsonFile << "      \"type\": \"" << node.allocType << "\",\n";
            jsonFile << "      \"origin\": \"" << node.funcName << "\"\n";
            jsonFile << "    }";
            if (++lCount < activeAllocations.size()) jsonFile << ",";
            jsonFile << "\n";
        }
        jsonFile << "  ]\n";
        jsonFile << "}\n";
        jsonFile.close();
    }
};

} // namespace MemGuard

// Intercept Macros for Application
inline void* mg_malloc(size_t size, const char* origin = "malloc_caller") {
    void* ptr = std::malloc(size);
    MemGuard::Sentinel::getInstance().recordAllocation(ptr, size, "malloc", origin);
    return ptr;
}

inline void* mg_calloc(size_t num, size_t size, const char* origin = "calloc_caller") {
    void* ptr = std::calloc(num, size);
    MemGuard::Sentinel::getInstance().recordAllocation(ptr, num * size, "calloc", origin);
    return ptr;
}

inline void* mg_realloc(void* oldPtr, size_t newSize, const char* origin = "realloc_caller") {
    void* ptr = std::realloc(oldPtr, newSize);
    MemGuard::Sentinel::getInstance().recordRealloc(oldPtr, ptr, newSize, origin);
    return ptr;
}

inline void mg_free(void* ptr) {
    MemGuard::Sentinel::getInstance().recordFree(ptr);
    std::free(ptr);
}

#endif // MEMGUARD_HOOK_HPP
