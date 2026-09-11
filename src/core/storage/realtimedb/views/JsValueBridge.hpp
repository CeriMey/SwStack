#pragma once
#include <core/types/SwJsonArray.h>
#include <core/types/SwJsonObject.h>
#include <cstddef>
#include <cstdint>
#include <map>
#include <memory>
#include <mutex>
#include <vector>

namespace swRealtimeDbDetail {
struct JsProgram {
    SwString function;
    std::mutex mutex;
    std::shared_ptr<const std::vector<char>> bytecode;
};
struct JsPrograms {
    std::mutex mutex;
    std::map<SwString, std::shared_ptr<const std::vector<char>>> entries;
    std::size_t bytes{0};
};
SwJsonArray evaluateTypedValues(const SwString& function, const SwJsonObject& tables,
                               std::uint64_t deadlineNs, std::size_t heapLimitBytes, JsPrograms& programs, std::shared_ptr<const std::vector<char>>* pinned = nullptr);
}
