#include "StoreState.hpp"

#include <stdexcept>

SwJsonObject SwRealtimeDb::State::mutateMany(const SwJsonObject& request) {
    session(request);
    const auto operations = request["operations"].toArrayPtr();
    if (!operations || operations->isEmpty() || operations->size() > 64)
        throw std::runtime_error("mutate_many requires 1 to 64 operations");
    SwJsonArray results;
    int applied = 0, failedIndex = -1;
    SwString failure;
    for (std::size_t index = 0; index < operations->size(); ++index) {
        SwJsonObject result;
        result["index"] = static_cast<int>(index);
        if (failedIndex >= 0) {
            result["outcome"] = "not_run";
            results.append(std::move(result));
            continue;
        }
        auto outcome = MutationOutcome::Rejected;
        try {
            const auto& value = (*operations)[index];
            if (!value.isObject()) throw std::runtime_error("mutation must be an object");
            const SwJsonObject empty;
            const auto object = value.toObjectPtr();
            const auto& operation = object ? *object : empty;
            const auto op = swRealtimeDbDetail::requiredString(operation, "op");
            if (op != "write" && op != "erase")
                throw std::runtime_error("mutate_many only accepts write and erase");
            if (operation.contains("session"))
                throw std::runtime_error("mutation session belongs on the batch envelope");
            if (operation.contains("include_rows") && !operation["include_rows"].isBool())
                throw std::runtime_error("include_rows must be a boolean");
            // The envelope supplies credentials, revalidated by write() at
            // every operation. The immutable child needs no session-bearing copy.
            const auto reply = write(operation, op == "erase", &outcome, &request);
            result["outcome"] = "applied";
            result["revision"] = reply["revision"];
            result["cursor"] = reply["cursor"];
            result["row_count"] = reply["row_count"];
            result["evicted_count"] = static_cast<int>(reply["evicted_keys"].toArray().size());
            ++applied;
        } catch (const std::exception& error) {
            // Validation errors precede storage. Once storage starts, an error
            // cannot promise rollback (for example catalog persistence can fail
            // after rows commit). Confirmed commits survive later view errors.
            result["outcome"] = outcome == MutationOutcome::Rejected ? "failed" :
                outcome == MutationOutcome::Applied ? "applied" : "unknown";
            if (outcome == MutationOutcome::Applied) ++applied;
            failure = SwString(std::string(error.what()).substr(0, 1024));
            result["error"] = failure;
            failedIndex = static_cast<int>(index);
        }
        results.append(std::move(result));
    }
    SwJsonObject response;
    response["epoch"] = epoch;
    response["cursor"] = swRealtimeDbDetail::decimal(revision);
    response["complete"] = failedIndex < 0;
    response["applied"] = applied;
    response["results"] = std::move(results);
    if (failedIndex >= 0) {
        response["failed_index"] = failedIndex;
        response["error"] = failure;
    }
    return response;
}
