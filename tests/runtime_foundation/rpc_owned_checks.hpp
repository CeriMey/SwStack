#pragma once
#include <core/types/SwJsonObject.h>

namespace ownedRpcTest {
struct Value {
    int* copies;
    int number;
    Value(int& count, int value) : copies(&count), number(value) {}
    Value(const Value& other) : copies(other.copies), number(other.number) { ++*copies; }
    Value(Value&& other) noexcept : copies(other.copies), number(other.number) { other.number = -1; }
};
inline void run(const SwString& domain) {
    using Endpoint = sw::ipc::NativeRpcEndpoint<int, Value>;
    using Client = sw::ipc::RpcMethodClient<int, Value>;
    SwObject target;
    int copies=0,calls=0;
    auto registration=Endpoint::expose(domain,"owned","value",&target,
        [&](sw::ipc::RpcContext, Value value) { ++calls; return value.number; });
    Client client(domain,"owned","value","",{});
    Value owned(copies,42);Client::Result result;
    require(client.tryCallDirectOwned(result,std::move(owned)) && result.ok && result.value==42 && copies==0,
            "owned direct RPC copied its payload");
    Value borrowed(copies,43);copies=0;
    require(client.tryCallDirect(result,borrowed) && result.ok && result.value==43 && borrowed.number==43 && copies==1,
            "borrowed RPC must detach once and consume the detached tuple");
    Value queued(copies,44);bool completed=false;
    client.callAsyncResult(queued,[&](const Client::Result& reply) {
        require(reply.ok && reply.value==44,"queued owned payload changed");completed=true;
    });
    const int admissionCopies=copies;
    waitFor([&]{return completed;},"queued owned RPC did not finish");
    require(copies==admissionCopies && queued.number==44,"queued invocation copied the already owned tuple");
    registration.stop();Value retained(copies,45);
    require(!client.tryCallDirectOwned(result,std::move(retained)) && retained.number==45 && calls==3,
            "unavailable endpoint consumed the fallback request");

    using JsonEndpoint=sw::ipc::NativeRpcEndpoint<int,SwJsonObject>;
    using JsonClient=sw::ipc::RpcMethodClient<int,SwJsonObject>;
    SwJsonObject leaf;leaf["value"]=1;SwJsonObject input;input["nested"]=leaf;
    const SwJsonObject* original=input["nested"].toObjectPtr().get();
    const SwJsonObject* observed=nullptr;
    auto jsonRegistration=JsonEndpoint::expose(domain,"owned","json",&target,
        [&](sw::ipc::RpcContext,SwJsonObject request) {
            auto nested=request["nested"].toObjectPtr();observed=nested.get();(*nested)["value"]=2;return 2;
        });
    JsonClient json(domain,"owned","json","",{});JsonClient::Result reply;
    require(json.tryCallDirect(reply,input) && reply.ok && observed!=original && (*original)["value"].toInt()==1,
            "borrowed JSON aliases mutable children inside handler");
    require(json.tryCallDirectOwned(reply,std::move(input)) && reply.ok && observed==original,
            "owned JSON was detached instead of transferred");
}
}
