#include <iostream>
#include <string>
#include <vector>
#include <unordered_map>
#include <cstring>
#include <chrono>
#include <thread>
#include "MemGuardHook.hpp"

// ==========================================
// VortexKV: In-Memory Key-Value Storage Engine
// Real-World Target Application for MemGuard Pro
// ==========================================

struct KeyValueRecord {
    char* key;
    char* value;
    size_t valSize;
};

class VortexKVEngine {
private:
    std::unordered_map<std::string, KeyValueRecord*> store;
    std::vector<void*> activeClientSessions;
    std::vector<void*> transactionLogs;

public:
    ~VortexKVEngine() {
        // Normal shutdown cleanup
        for (auto& pair : store) {
            if (pair.second) {
                if (pair.second->key) mg_free(pair.second->key);
                if (pair.second->value) mg_free(pair.second->value);
                mg_free(pair.second);
            }
        }
        store.clear();
    }

    // --- Clean Operations ---
    void put(const std::string& k, const std::string& v) {
        // Allocate record struct
        KeyValueRecord* rec = (KeyValueRecord*)mg_malloc(sizeof(KeyValueRecord), "VortexKV::put_record");
        
        // Allocate key string
        rec->key = (char*)mg_malloc(k.size() + 1, "VortexKV::put_key");
        std::strcpy(rec->key, k.c_str());

        // Allocate value string
        rec->valSize = v.size();
        rec->value = (char*)mg_malloc(rec->valSize + 1, "VortexKV::put_val");
        std::strcpy(rec->value, v.c_str());

        // If key existed, free old record cleanly
        if (store.find(k) != store.end()) {
            KeyValueRecord* old = store[k];
            mg_free(old->key);
            mg_free(old->value);
            mg_free(old);
        }

        store[k] = rec;
    }

    std::string get(const std::string& k) {
        if (store.find(k) != store.end()) {
            return std::string(store[k]->value);
        }
        return "";
    }

    void remove(const std::string& k) {
        if (store.find(k) != store.end()) {
            KeyValueRecord* rec = store[k];
            mg_free(rec->key);
            mg_free(rec->value);
            mg_free(rec);
            store.erase(k);
        }
    }

    void expandValueBuffer(const std::string& k, const std::string& appendStr) {
        if (store.find(k) != store.end()) {
            KeyValueRecord* rec = store[k];
            size_t newSize = rec->valSize + appendStr.size() + 1;
            
            // Realloc buffer properly
            rec->value = (char*)mg_realloc(rec->value, newSize, "VortexKV::expandValueBuffer");
            std::strcat(rec->value, appendStr.c_str());
            rec->valSize = newSize - 1;
        }
    }

    // --- Simulated Memory Leak Scenarios ---

    // Scenario 1: Session Leak Bug
    // Simulates opening 50 client auth sessions but failing to clean up 15 disconnected buffers
    void simulateSessionLeak() {
        std::cout << "[VortexKV] Executing Scenario: Session Leak Bug..." << std::endl;
        for (int i = 0; i < 50; ++i) {
            // Allocate session auth token buffer (256 bytes each) via malloc
            char* sessionToken = (char*)mg_malloc(256, "SessionManager::authenticateClient");
            std::sprintf(sessionToken, "AUTH_TOKEN_SESS_%d_ACTIVE_USER", i);
            activeClientSessions.push_back(sessionToken);
        }

        // Simulate client disconnects: properly free 35 sessions, LEAK 15 sessions!
        for (size_t i = 0; i < activeClientSessions.size(); ++i) {
            if (i < 35) {
                mg_free(activeClientSessions[i]);
            } else {
                // BUG: Orphaned pointer! Missing free() on network drop
            }
        }
        activeClientSessions.clear(); // Vectors cleared but heap buffers remain leaked!
        std::cout << "[VortexKV] Completed 50 sessions. 15 sessions orphaned (Leaked ~3,840 bytes)." << std::endl;
    }

    // Scenario 2: Cache Expansion Leak Bug
    // Simulates improper realloc usage where temporary buffers are abandoned
    void simulateCacheLeak() {
        std::cout << "[VortexKV] Executing Scenario: Cache Expansion Leak..." << std::endl;
        for (int i = 0; i < 20; ++i) {
            // Initial cache block (1024 bytes) via calloc
            char* cacheBlock = (char*)mg_calloc(1, 1024, "CacheStore::allocateShard");
            
            // Simulate improper resize attempt
            if (i % 2 == 0) {
                // Properly resize and update pointer
                cacheBlock = (char*)mg_realloc(cacheBlock, 4096, "CacheStore::expandShard");
                mg_free(cacheBlock); // Clean cleanup
            } else {
                // BUG: Allocate a new expansion buffer but forget to free original cacheBlock!
                char* orphanedExpansion = (char*)mg_malloc(2048, "CacheStore::orphanedExpansion");
                std::strcpy(orphanedExpansion, "ERR_CACHE_CORRUPTED_FRAME");
                // Both cacheBlock and orphanedExpansion are leaked here!
            }
        }
        std::cout << "[VortexKV] Cache operations completed. Orphaned shard fragments left in memory." << std::endl;
    }

    // Scenario 3: Transaction Log Bug
    // Simulates uncommitted transaction logs allocated via calloc that skip cleanup on rollback
    void simulateTransactionLeak() {
        std::cout << "[VortexKV] Executing Scenario: Transaction Log Bug..." << std::endl;
        for (int i = 0; i < 30; ++i) {
            // Allocate transaction metadata frame (512 bytes)
            void* txFrame = mg_calloc(1, 512, "TxEngine::commitBatch");
            if (i < 20) {
                // Committed successfully
                mg_free(txFrame);
            } else {
                // Rollback path executed, but BUG: txFrame skipped mg_free()
                transactionLogs.push_back(txFrame);
            }
        }
        transactionLogs.clear();
        std::cout << "[VortexKV] Batch processing done. 10 uncommitted transaction frames leaked." << std::endl;
    }

    // Benchmark Run (100% Clean)
    void runCleanBenchmark() {
        std::cout << "[VortexKV] Running High-Throughput Clean Benchmark..." << std::endl;
        for (int i = 0; i < 200; ++i) {
            std::string key = "user:" + std::to_string(i);
            std::string val = "{ \"id\": " + std::to_string(i) + ", \"status\": \"active\", \"score\": 99.5 }";
            put(key, val);
            if (i % 3 == 0) {
                expandValueBuffer(key, ", \"verified\": true");
            }
        }
        for (int i = 0; i < 200; ++i) {
            std::string key = "user:" + std::to_string(i);
            remove(key);
        }
        std::cout << "[VortexKV] Clean Benchmark completed successfully. All records allocated & freed." << std::endl;
    }
};

int main(int argc, char* argv[]) {
    std::string scenario = "clean";
    if (argc > 2 && std::string(argv[1]) == "--scenario") {
        scenario = argv[2];
    }

    std::cout << "==================================================" << std::endl;
    std::cout << "   VortexKV Database Engine (MemGuard Target)     " << std::endl;
    std::cout << "   Selected Scenario: " << scenario << std::endl;
    std::cout << "==================================================" << std::endl;

    VortexKVEngine engine;

    if (scenario == "clean") {
        engine.runCleanBenchmark();
    } else if (scenario == "session_leak") {
        engine.runCleanBenchmark();
        engine.simulateSessionLeak();
    } else if (scenario == "cache_leak") {
        engine.runCleanBenchmark();
        engine.simulateCacheLeak();
    } else if (scenario == "transaction_leak") {
        engine.runCleanBenchmark();
        engine.simulateTransactionLeak();
    } else if (scenario == "all_leaks") {
        engine.runCleanBenchmark();
        engine.simulateSessionLeak();
        engine.simulateCacheLeak();
        engine.simulateTransactionLeak();
    } else {
        std::cout << "Unknown scenario. Defaulting to clean." << std::endl;
        engine.runCleanBenchmark();
    }

    std::cout << "[VortexKV] Shutting down engine. Generating MemGuard telemetry..." << std::endl;
    // Sentinel destructor automatically outputs memguard_report.json and memguard_report.txt
    return 0;
}
