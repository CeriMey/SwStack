#pragma once
#include "SwRemoteObjectComponentRegistry.h"
#include "SwRemoteObjectComponentLifecycle.h"
#include "SwPluginLoader.h"
#include <algorithm>
#include <functional>
#include <memory>
#include <set>
#include <stdexcept>
#include <thread>
#include <vector>

// Owns one composition on its creating runtime thread. No polling, worker thread
// or per-message dispatch is introduced. Libraries outlive every component.
class SwRemoteObjectComponentHost {
public:
    struct Definition {
        SwString type, nameSpace, name, configRoot;
        SwJsonObject params;
        SwStringList dependsOn; // namespace/name, relative to this domain
        SwString identity() const { return nameSpace.isEmpty() ? name : nameSpace + "/" + name; }
    };
    explicit SwRemoteObjectComponentHost(SwString domain)
        : domain_(std::move(domain)), thread_(std::this_thread::get_id()) {}
    ~SwRemoteObjectComponentHost() { clear(); }
    SwRemoteObjectComponentHost(const SwRemoteObjectComponentHost&) = delete;
    SwRemoteObjectComponentHost& operator=(const SwRemoteObjectComponentHost&) = delete;

    template<class T> void registerType(const SwString& type) {
        checkThread();
        if (!registry_.registerComponent(type, &create<T>, &destroy<T>))
            throw std::invalid_argument("Duplicate component type: " + type.toStdString());
    }
    // The existing plugin registration ABI is preserved. Register in a temporary
    // catalogue first, so conflicting types cannot partially change this host.
    void loadPlugin(const SwString& path) {
        checkThread();
        if (!instances_.empty()) throw std::logic_error("Load plugins before creating components");
        auto loader = std::make_unique<SwPluginLoader>(path);
        if (!loader->load()) throw std::runtime_error(loader->errorString().toStdString());
        using Version = unsigned (*)();
        auto version = reinterpret_cast<Version>(loader->resolve("swRemoteObjectComponentAbiVersion"));
        if (!version || version()!=1) throw std::invalid_argument("Component plugin requires ABI version 1");
        using Register = bool (*)(SwRemoteObjectComponentRegistry*);
        auto registerTypes = reinterpret_cast<Register>(loader->resolve("swRegisterRemoteObjectComponents"));
        SwRemoteObjectComponentRegistry candidate;
        if (!registerTypes || !registerTypes(&candidate) || candidate.types().isEmpty())
            throw std::invalid_argument("Missing component registration: " + path.toStdString());
        for (const auto& type : candidate.types()) {
            const auto entry = candidate.entry(type);
            if (!entry.create || !entry.destroy || registry_.contains(type))
                throw std::invalid_argument("Conflicting/invalid component type: " + type.toStdString());
        }
        // Keep code loaded even if allocating a catalogue entry throws.
        plugins_.push_back(std::move(loader));
        for (const auto& type : candidate.types()) {
            const auto entry = candidate.entry(type);
            if (!registry_.registerComponent(type, entry.create, entry.destroy, path))
                throw std::logic_error("Component registration changed during load");
        }
    }
    SwStringList types() const { checkThread(); return registry_.types(); }

    static Definition fromJson(const SwJsonObject& value, const SwString& defaultRoot = {}) {
        Definition out;
        auto text = [&](const char* name, bool required = false) {
            if ((!value.contains(name) && !required)) return SwString();
            if (!value[name].isString()) throw std::invalid_argument(std::string("Component requires string: ") + name);
            return value[name].toString();
        };
        out.type=text("type",true);out.name=text("name",true);out.nameSpace=text("ns");
        out.configRoot=value.contains("config_root") ? text("config_root") : defaultRoot;
        if (value.contains("params") && !value["params"].isObject()) throw std::invalid_argument("Component params must be an object");
        out.params=value["params"].toObject();
        if (value.contains("execution") && text("execution")!="same_thread")
            throw std::invalid_argument("This component host requires execution=same_thread");
        if (value.contains("depends_on") && !value["depends_on"].isArray()) throw std::invalid_argument("depends_on must be an array");
        for (const auto& item:value["depends_on"].toArray()) {
            if (!item.isString() || item.toString().isEmpty()) throw std::invalid_argument("Invalid component dependency");
            out.dependsOn.append(item.toString());
        }
        return out;
    }

    void configure(const std::vector<Definition>& definitions) {
        checkThread();
        if (!instances_.empty() || changing_) throw std::logic_error("Composition already configured or changing");
        const auto order = validate(definitions); // no constructors before the complete graph is valid
        ChangeScope scope(changing_);
        try {
            for (auto index:order) {
                const auto& definition=definitions[index];
                const auto factory=registry_.entry(definition.type);
                SwRemoteObject::ConfigRootScope root(definition.configRoot);
                std::unique_ptr<SwRemoteObject, SwRemoteObjectComponentRegistry::DestroyFn>
                    object(factory.create(domain_,definition.nameSpace,definition.name,nullptr,definition.configRoot),factory.destroy);
                if (!object) throw std::runtime_error("Component factory returned null");
                auto* lifecycle=dynamic_cast<SwRemoteObjectComponentLifecycle*>(object.get());
                if (!lifecycle) throw std::invalid_argument("Component has no managed lifecycle: " + definition.type.toStdString());
                auto instance=std::make_unique<Instance>(definition,factory,object.get(),lifecycle);
                object.release();
                instances_.push_back(std::move(instance));
                if (!definition.configRoot.isEmpty()) instances_.back()->object->setConfigRootDirectory(definition.configRoot);
                lifecycle->configureComponent(definition.params);
            }
        } catch (...) { clearInstances(); throw; }
    }
    void start() {
        checkThread();
        if (changing_) throw std::logic_error("Component lifecycle re-entry");
        ChangeScope scope(changing_);
        try {
            for (auto& instance:instances_) {
                if (instance->started) continue;
                for (const auto& dependency:instance->definition.dependsOn) {
                    const auto* required=find(dependency);
                    if (!required || !required->started || !required->lifecycle->componentReady())
                        throw std::runtime_error("Component dependency is not ready: " + dependency.toStdString());
                }
                instance->lifecycle->startComponent();
                instance->started=true;
            }
        } catch (...) { clearInstances(); throw; }
    }
    void clear() noexcept {
        // Destruction and stop must occur on the owner's runtime thread.
        if (std::this_thread::get_id()!=thread_ || changing_) std::terminate();
        ChangeScope scope(changing_);
        clearInstances();
    }
    SwRemoteObject* object(const SwString& identity) const {
        checkThread();const auto* instance=find(identity);return instance ? instance->object : nullptr;
    }
    bool ready(const SwString& identity) const {
        checkThread();const auto* instance=find(identity);
        return instance && instance->started && instance->lifecycle->componentReady();
    }
    std::size_t size() const { checkThread();return instances_.size(); }
    SwJsonArray status() const {
        checkThread();SwJsonArray result;
        for (const auto& item:instances_) {
            SwJsonObject value;value["type"]=item->definition.type;value["identity"]=item->definition.identity();
            value["started"]=item->started;value["ready"]=item->started&&item->lifecycle->componentReady();
            result.append(value);
        }
        return result;
    }
private:
    template<class T> static SwRemoteObject* create(const SwString& sys,const SwString& ns,
            const SwString& name,SwObject* parent,const SwString& root) {
        SwRemoteObject::ConfigRootScope scope(root);return new T(sys,ns,name,parent);
    }
    template<class T> static void destroy(SwRemoteObject* object) { delete static_cast<T*>(object); }
    struct ChangeScope {
        bool& flag;
        explicit ChangeScope(bool& value):flag(value) { flag=true; }
        ~ChangeScope(){flag=false;}
    };
    struct Instance {
        Definition definition;
        SwRemoteObjectComponentRegistry::Entry factory;
        SwRemoteObject* object;
        SwRemoteObjectComponentLifecycle* lifecycle;
        bool started=false;
        Instance(const Definition& d,const SwRemoteObjectComponentRegistry::Entry& f,
                 SwRemoteObject* o,SwRemoteObjectComponentLifecycle* l):definition(d),factory(f),object(o),lifecycle(l){}
        ~Instance(){lifecycle->stopComponent();factory.destroy(object);}
    };
    const Instance* find(const SwString& identity) const {
        for(const auto& item:instances_)if(item->definition.identity()==identity)return item.get();
        return nullptr;
    }
    void checkThread() const {
        if(std::this_thread::get_id()!=thread_)throw std::logic_error("Component host used outside its runtime thread");
    }
    static bool validSegment(const SwString& value) {
        if(value.isEmpty() || value!=value.trimmed() || value=="." || value=="..")return false;
        for(const char ch:value.toStdString())
            if(!((ch>='a'&&ch<='z')||(ch>='A'&&ch<='Z')||(ch>='0'&&ch<='9')||ch=='_'||ch=='-'||ch=='.'))return false;
        return true;
    }
    std::vector<std::size_t> validate(const std::vector<Definition>& definitions) const {
        std::map<std::string,std::size_t> identities;
        for(std::size_t i=0;i<definitions.size();++i) {
            const auto& d=definitions[i];const auto entry=registry_.entry(d.type);
            if(!entry.create||!entry.destroy)throw std::invalid_argument("Unknown component type: "+d.type.toStdString());
            if(!validSegment(d.name))throw std::invalid_argument("Invalid component name");
            if(!d.nameSpace.isEmpty()) {
                if(d.nameSpace.startsWith("/") || d.nameSpace.endsWith("/") || d.nameSpace.contains("//"))
                    throw std::invalid_argument("Invalid component namespace");
                for(const auto& part:d.nameSpace.split('/'))if(!validSegment(part))throw std::invalid_argument("Invalid component namespace");
            }
            if(!identities.emplace(d.identity().toStdString(),i).second)throw std::invalid_argument("Duplicate component identity");
        }
        std::vector<int> visit(definitions.size());
        std::vector<std::size_t> order;
        std::function<void(std::size_t)> walk=[&](std::size_t index) {
            if(visit[index]==2)return;
            if(visit[index]==1)throw std::invalid_argument("Cyclic component dependencies");
            visit[index]=1;std::set<std::string> dependencies;
            for(const auto& name:definitions[index].dependsOn) {
                if(!dependencies.insert(name.toStdString()).second)throw std::invalid_argument("Duplicate dependency");
                const auto found=identities.find(name.toStdString());
                if(found==identities.end())throw std::invalid_argument("Missing component dependency: "+name.toStdString());
                walk(found->second);
            }
            visit[index]=2;order.push_back(index);
        };
        for(std::size_t i=0;i<definitions.size();++i)walk(i);
        return order;
    }
    void clearInstances() noexcept { while(!instances_.empty())instances_.pop_back(); }
    SwString domain_;
    std::thread::id thread_;
    SwRemoteObjectComponentRegistry registry_;
    std::vector<std::unique_ptr<SwPluginLoader>> plugins_;
    std::vector<std::unique_ptr<Instance>> instances_;
    bool changing_=false;
};
