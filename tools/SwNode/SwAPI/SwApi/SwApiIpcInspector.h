#pragma once

#include "SwJsonArray.h"
#include "SwJsonObject.h"
#include "SwMap.h"
#include "SwString.h"

#include <cstdint>

class SwApiIpcInspector {
public:
    struct Target {
        SwString domain;
        SwString object;

        SwString toString() const { return domain + "/" + object; }
    };

    SwJsonArray appsSnapshot() const;
    SwJsonArray registrySnapshot(const SwString& domain) const;
    SwJsonArray subscribersSnapshot(const SwString& domain) const;
    SwStringList domains() const;

    bool parseTarget(const SwString& input, const SwString& defaultDomain, Target& out, SwString& err) const;

    SwJsonArray nodesForDomain(const SwString& domain, bool includeStale) const;
    SwJsonArray nodesAllDomains(bool includeStale) const;
    bool nodeInfo(const Target& target, SwJsonObject& out, SwString& err, bool includeStale = false) const;

    SwJsonArray signalsForTarget(const Target& target, bool includeStale = false) const;
    SwJsonArray rpcsForTarget(const Target& target, bool includeStale = false) const;

    bool findConfigDocSignal(const Target& target, SwString& outSignalName) const;
    bool readConfigDocJson(const Target& target, SwString& outJson, uint64_t& outPubId, SwString& err) const;
    bool publishConfigValue(const Target& target, const SwString& configPath, const SwString& value, SwString& err) const;

private:
    static bool activePidsForDomain_(const SwString& domain, SwMap<uint32_t, bool>& out);
    static SwStringList parseTypeArgs_(const SwString& typeName);
    static SwJsonArray argTypesToJson_(const SwStringList& types);
    static SwString kindForSignal_(const SwString& signal);
};
