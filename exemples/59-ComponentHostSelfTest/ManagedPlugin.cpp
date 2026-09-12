#include <core/remote/SwRemoteObjectComponentLifecycle.h>
#include <core/remote/SwRemoteObjectComponent.h>
namespace test {
class ManagedPlugin final : public SwRemoteObject, public SwRemoteObjectComponentLifecycle {
public:
    ManagedPlugin(const SwString& sys,const SwString& ns,const SwString& name,SwObject* parent):SwRemoteObject(sys,ns,name,parent){}
    void configureComponent(const SwJsonObject& params) override { applyParameters(*this,params); }
    void startComponent() override {started_=true;}
    void stopComponent() noexcept override {started_=false;}
    bool componentReady() const noexcept override{return started_;}
private:bool started_=false;
};
}
#ifndef TEST_COMPONENT_TYPE
#define TEST_COMPONENT_TYPE "test/ManagedPlugin"
#endif
SW_REGISTER_COMPONENT_NODE_AS(test::ManagedPlugin,TEST_COMPONENT_TYPE)
