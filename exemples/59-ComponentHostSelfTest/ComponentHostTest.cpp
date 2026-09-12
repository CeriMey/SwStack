#include <core/remote/SwRemoteObjectComponentHost.h>
#include <core/runtime/SwCoreApplication.h>
#include <future>
#include <iostream>
#include <vector>
#include <chrono>
#include <cstdlib>
namespace {
std::vector<std::string> events;
void require(bool value,const char* message){if(!value)throw std::runtime_error(message);}
class Probe : public SwRemoteObject, public SwRemoteObjectComponentLifecycle {
public:
    Probe(const SwString& sys,const SwString& ns,const SwString& name,SwObject* parent)
        :SwRemoteObject(sys,ns,name,parent),name_(name.toStdString()){events.push_back("create:"+name_);}
    ~Probe() override {events.push_back("destroy:"+name_);}
    void configureComponent(const SwJsonObject& params) override {
        events.push_back("configure:"+name_);params_=params;
        if(params["fail_config"].toBool())throw std::runtime_error("configuration failure");
    }
    void startComponent() override {
        events.push_back("start:"+name_);
        if(params_["fail_start"].toBool())throw std::runtime_error("startup failure");
        ready_=!params_["not_ready"].toBool();
    }
    void stopComponent() noexcept override {ready_=false;events.push_back("stop:"+name_);}
    bool componentReady() const noexcept override{return ready_;}
private:std::string name_;SwJsonObject params_;bool ready_=false;
};
class Other final : public Probe {public:using Probe::Probe;};
using Host=SwRemoteObjectComponentHost;
Host::Definition definition(const char* name,const char* dependency="") {
    Host::Definition d;d.type="test/Probe";d.name=name;
    if(*dependency)d.dependsOn.append(dependency);return d;
}
template<class Fn> void rejects(Fn fn) {
    bool caught=false;try{fn();}catch(const std::exception&){caught=true;}
    require(caught,"invalid operation accepted");
}
void same(const std::vector<std::string>& expected) {
    if(events!=expected){for(auto& event:events)std::cerr<<event<<"\n";throw std::runtime_error("wrong lifecycle order");}
}
}
int main(int argc,char** argv){
    SwCoreApplication app(argc,argv);
    try{
        const auto run=SwString::number(std::chrono::steady_clock::now().time_since_epoch().count());
        const auto* testDomain=std::getenv("SW_TEST_DOMAIN");
        const SwString domain=testDomain ? SwString(testDomain) : "component_host_test_"+run;
        Host host(domain);
        host.registerType<Probe>("test/Probe");host.registerType<Probe>("test/Probe");
        rejects([&]{host.registerType<Other>("test/Probe");});
        auto a=definition("a"),b=definition("b","a");
        for(int i=0;i<3;++i){
            events.clear();host.configure({b,a});
            require(host.size()==2&&host.object("a")&&!host.ready("a"),"configured state");
            same({"create:a","configure:a","create:b","configure:b"});
            host.start();host.start();require(host.ready("a")&&host.ready("b"),"ready state");
            host.clear();host.clear();
            same({"create:a","configure:a","create:b","configure:b","start:a","start:b","stop:b","destroy:b","stop:a","destroy:a"});
        }
        auto rejectedGraph=[&](std::vector<Host::Definition> graph){
            events.clear();rejects([&]{host.configure(graph);});
            require(events.empty()&&host.size()==0,"invalid graph caused construction");
        };
        rejectedGraph({a,a});rejectedGraph({b});
        auto unknown=a;unknown.type="missing";rejectedGraph({unknown});
        auto cycle=a;cycle.dependsOn.append("b");rejectedGraph({cycle,b});
        auto invalid=a;invalid.name="../a";rejectedGraph({invalid});
        invalid=a;invalid.nameSpace="a//b";rejectedGraph({invalid});
        auto bad=a;bad.params["fail_config"]=true;
        events.clear();rejects([&]{host.configure({bad});});
        same({"create:a","configure:a","stop:a","destroy:a"});require(host.size()==0,"configuration rollback");
        bad=b;bad.params["fail_start"]=true;
        events.clear();host.configure({a,bad});rejects([&]{host.start();});
        same({"create:a","configure:a","create:b","configure:b","start:a","start:b","stop:b","destroy:b","stop:a","destroy:a"});
        bad=a;bad.params["not_ready"]=true;
        events.clear();host.configure({bad,b});rejects([&]{host.start();});
        require(std::find(events.begin(),events.end(),"start:b")==events.end(),"dependent started before readiness");
        auto wrongThread=std::async(std::launch::async,[&]{rejects([&]{host.types();});});wrongThread.get();
        SwJsonObject spec;spec["type"]="test/Probe";spec["name"]="a";spec["execution"]="thread_per_plugin";
        rejects([&]{Host::fromJson(spec);});spec["execution"]="same_thread";
        require(Host::fromJson(spec).name=="a","manifest parser");
        if(argc>3){
            Host loaded(domain);loaded.loadPlugin(argv[1]);
            rejects([&]{loaded.loadPlugin(argv[1]);});
            rejects([&]{loaded.loadPlugin(argv[3]);});
            loaded.loadPlugin(argv[2]);require(loaded.types().size()==2,"Plugin catalogues were merged");
            Host::Definition d;d.type="test/ManagedPlugin";d.name="managed";
            loaded.configure({d});loaded.start();require(loaded.ready("managed"),"plugin readiness");loaded.clear();
        }
        std::cout<<"PASS component lifecycle, graph validation, rollback, ownership, thread affinity and plugin\n";return 0;
    }catch(const std::exception& error){std::cerr<<error.what()<<"\n";return 1;}
}
