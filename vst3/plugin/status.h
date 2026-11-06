#pragma once
#include <atomic>
#include <cstdint>
#include <memory>

namespace RebirthBridge {
enum Status : int32_t {
    Inactive = 0, Ready = 1, Playing = 2, MissingContext = 3,
    BadTempo = 4, UnsupportedSeek = 5, UnsupportedCycle = 6,
    Underrun = 7, BadProtocol = 8, BadRate = 9, OfflineUnsupported = 10,
    MissingWorker = 11, InstanceBusy = 12, WorkerFailed = 13, WaitingForBarOne = 14
};
}
