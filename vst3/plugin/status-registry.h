#pragma once
#include "status.h"
#include <windows.h>
#include <map>

namespace RebirthBridge {
/* Controller-to-processor status wiring runs only on VST UI/control threads.
 * IMessage carries a registry key, never a process pointer. A proxy in another
 * process cannot accidentally resolve that key because it includes the PID.
 * The processor owns the resolved atomic throughout audio processing. */
struct StatusRegistry {
    SRWLOCK lock = SRWLOCK_INIT;
    uint32_t next = 0;
    std::map<uint64_t, std::weak_ptr<std::atomic<int32_t>>> entries;
};
inline StatusRegistry& statusRegistry() { static StatusRegistry registry; return registry; }
struct StatusRegistryLock {
    explicit StatusRegistryLock(StatusRegistry& value) : registry(value) {
        AcquireSRWLockExclusive(&registry.lock);
    }
    ~StatusRegistryLock() { ReleaseSRWLockExclusive(&registry.lock); }
    StatusRegistry& registry;
};
inline uint64_t registerStatus(const std::shared_ptr<std::atomic<int32_t>>& status) {
    auto& registry = statusRegistry();
    StatusRegistryLock lock(registry);
    if (registry.next == UINT32_MAX) return 0;
    const uint64_t key = (uint64_t(GetCurrentProcessId()) << 32) | ++registry.next;
    registry.entries.emplace(key, status);
    return key;
}
inline void unregisterStatus(uint64_t key) {
    auto& registry = statusRegistry();
    StatusRegistryLock lock(registry);
    registry.entries.erase(key);
}
inline std::shared_ptr<std::atomic<int32_t>> findStatus(uint64_t key) {
    auto& registry = statusRegistry();
    StatusRegistryLock lock(registry);
    const auto found = registry.entries.find(key);
    return found == registry.entries.end() ? nullptr : found->second.lock();
}
}
