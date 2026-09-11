#pragma once
#include <core/remote/SwRemoteObject.h>
#include "SwRealtimeDbOptions.h"
#include "SwRealtimeDbClient.h"
#include "realtimedb/ChangeNotice.hpp"
#include <memory>
class SwRealtimeDbNode : public SwRemoteObject {
    SW_OBJECT(SwRealtimeDbNode, SwRemoteObject)
public:
    SwRealtimeDbNode(const SwString& sys, const SwString& ns, const SwString& name, SwObject* parent = nullptr);
    SwRealtimeDbNode(const SwString& sys, const SwString& ns, const SwString& name,
                     const SwRealtimeDbOptions& options, SwObject* parent = nullptr);
    ~SwRealtimeDbNode() override;
    SwString query(SwString request);
    SwString readResult(SwString token, int offset);
    virtual SwRealtimeDbReply executeRequest(const SwJsonObject& request);
    SW_IPC_LATCH_SIZED(changed, swRealtimeDbDetail::changeNoticeCapacity, swRealtimeDbDetail::ChangeNotice);
private:
    class Impl;
    std::unique_ptr<Impl> impl_;
    size_t queryToken_{0}, readResultToken_{0};
};
