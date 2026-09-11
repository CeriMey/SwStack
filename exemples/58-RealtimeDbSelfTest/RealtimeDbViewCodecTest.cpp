#include "SwRealtimeDb.h"
#include "SwRealtimeDbJsEvaluator.h"
#include <iostream>
#include <stdexcept>
#include <filesystem>
#include <chrono>

namespace {
SwJsonObject obj(std::initializer_list<std::pair<const char*,SwJsonValue>> values) {
    SwJsonObject result; for(const auto& v:values) result[v.first]=v.second; return result;
}
SwJsonArray arr(std::initializer_list<SwJsonValue> values) {
    SwJsonArray result; for(const auto& v:values) result.append(v); return result;
}
void require(bool ok,const char* why) {if(!ok)throw std::runtime_error(why);}
template<class F> void rejects(F f) {try{f();}catch(const std::exception&){return;}throw std::runtime_error("invalid write accepted");}
SwString hello(SwRealtimeDb& db,const char* actor) {
    return db.execute(obj({{"op","hello"},{"actor",actor}}))["session"].toString();
}
SwJsonObject view(const char* name,const SwString& session) {
    return obj({{"op","register_view"},{"table",name},{"session",session},{"key","id"},{"max_rows",1},{"schema_version",1},
        {"columns",obj({{"id","string"},{"degrees","number"}})}, {"dependencies",arr({"gimbal.Pose.command"})},
        {"write_target","gimbal.Pose.command"},
        {"encode","return tables.rows.map(function(r){return {id:r.id,units:r.degrees*100};});"},
        {"decode","return tables['gimbal.Pose.command'].map(function(r){return {id:r.id,degrees:r.units/100};});"}});
}
void run() {
    SwRealtimeDbOptions options;options.storage.persistent=false;
    SwRealtimeDbJsEvaluator js(50);
    unsigned prepared=0;
    SwRealtimeDb db({},options,[&](const SwString& source){
        ++prepared;const auto program=js.prepare(source);
        return [program,&js](const SwJsonObject& input){return js.evaluate(program,input);};
    });
    const auto hardware=hello(db,"gimbal"),gcs=hello(db,"gcs"),mav=hello(db,"mavlink"),other=hello(db,"other");
    auto gcsView=view("gcs.Pose.command",gcs);
    require(!db.execute(gcsView)["valid"].toBool(),"view must wait for its target");
    auto read=[&](const char* name){return db.execute(obj({{"op","read"},{"table",name}}));};
    auto write=[&](const char* name,const SwString& actor,double degrees){
        return db.execute(obj({{"op","write"},{"table",name},{"session",actor},
            {"rows",arr({obj({{"id","current"},{"degrees",degrees}})})}}));
    };
    rejects([&]{write("gcs.Pose.command",gcs,1);});
    db.execute(obj({{"op","register_table"},{"session",hardware},{"table","gimbal.Pose.command"},
        {"key","id"},{"max_rows",1},{"schema_version",1},{"columns",obj({{"id","string"},{"units","number"}})},
        {"writers",arr({"gcs","mavlink"})}}));
    db.execute(obj({{"op","write"},{"session",hardware},{"table","gimbal.Pose.command"},{"rows",SwJsonArray()}}));
    db.execute(view("mavlink.Pose.command",mav));
    const auto initialPrograms=prepared;
    for(int i=0;i<100;++i) {
        write(i%2?"gcs.Pose.command":"mavlink.Pose.command",i%2?gcs:mav,i);
        const auto stored=read("gimbal.Pose.command")["rows"].toArray();
        require(stored.size()==1 && stored[0].toObject()["units"].toDouble()==i*100,"last writer must replace the one row");
    }
    require(read("gcs.Pose.command")["rows"].toArray()[0].toObject()["degrees"].toDouble()==99,"decode did not read canonical value");
    require(prepared==initialPrograms,"encode/decode must retain their program handles");
    require(js.compiledProgramCount()==2,"shared encode/decode bytecode must compile once");
    const auto cursor=db.execute(obj({{"op","introspect"}}));
    write("gcs.Pose.command",gcs,99);
    const auto events=db.execute(obj({{"op","changes"},{"after",cursor["revision"]},{"epoch",cursor["epoch"]}}))["events"].toArray();
    require(events.size()==1 && events[0].toObject()["kind"].toString()=="write" && !events[0].toObject()["changed"].toBool(),
        "equal encoded value must be a write without a change");
    rejects([&]{write("gcs.Pose.command",other,2);});
    // Owning a view cannot bypass the hardware writer policy.
    db.execute(view("other.Pose.command",other));
    rejects([&]{write("other.Pose.command",other,2);});
    auto bad=gcsView;bad["replace"]=true;bad["encode"]="throw Error('bad value');";db.execute(bad);
    rejects([&]{write("gcs.Pose.command",gcs,3);});
    require(read("gimbal.Pose.command")["rows"].toArray()[0].toObject()["units"].toDouble()==9900,"failed encode changed stored data");
    bad["encode"]="return [{id:'a',units:1},{id:'b',units:2}];";db.execute(bad);
    rejects([&]{write("gcs.Pose.command",gcs,3);});
    require(read("gimbal.Pose.command")["rows"].toArray().size()==1,"invalid encode bypassed row limit");
    gcsView["replace"]=true;db.execute(gcsView);
    // Ordered mutations preserve original credentials and target outcomes.
    auto mutation=obj({{"op","write"},{"table","gcs.Pose.command"},{"rows",arr({obj({{"id","current"},{"degrees",0}})})}});
    const auto batch=db.execute(obj({{"op","mutate_many"},{"session",gcs},{"operations",arr({mutation})}}));
    require(batch["complete"].toBool(),"write through view failed inside mutation batch");
    require(read("gimbal.Pose.command")["rows"].toArray()[0].toObject()["units"].toDouble()==0,"stop was not the final canonical value");
    auto readonly=view("gcs.Pose.readonly",gcs);readonly.remove("encode");readonly.remove("write_target");db.execute(readonly);
    rejects([&]{write("gcs.Pose.readonly",gcs,9);});
    const auto info=db.execute(obj({{"op","introspect"},{"table","gcs.Pose.command"},{"include_scripts",true}}))["tables"].toArray()[0].toObject();
    require(info["writable"].toBool() && info["encode"].isString() && info["decode"].isString(),"codecs must be introspectable");
    rejects([&]{auto invalid=view("invalid",gcs);invalid["script"]="return [];";db.execute(invalid);});
    rejects([&]{auto invalid=view("invalid",gcs);invalid["write_mode"]=5;db.execute(invalid);});
    rejects([&]{auto invalid=view("invalid",gcs);invalid["write_target"]="unrelated";db.execute(invalid);});
    rejects([&]{auto invalid=view("invalid",gcs);invalid.remove("encode");db.execute(invalid);});
}

void persistedMerges() {
    struct Folder {
        std::filesystem::path path=std::filesystem::temp_directory_path()/
            ("sw-view-codecs-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
        ~Folder(){std::error_code ignored;std::filesystem::remove_all(path,ignored);}
    } folder;
    SwRealtimeDbOptions options;options.storage.dbPath=folder.path.string();
    SwRealtimeDbJsEvaluator js(50);
    auto transform=[&](const SwString& script,const SwJsonObject& inputs){return js.evaluate(script,inputs);};
    auto target=[](const SwString& session) {
        return obj({{"op","register_table"},{"session",session},{"table","device.command"},
            {"schema_version",1},{"key","id"},{"max_rows",1},{"writers",arr({"adapter","client"})},
            {"columns",obj({{"id","string"},{"value","number"},{"keep","string"}})}});
    };
    auto adapter=[](const SwString& session) {
        return obj({{"op","register_view"},{"session",session},{"table","adapter.command"},
            {"schema_version",1},{"key","id"},{"max_rows",1},{"writers",arr({"client"})},
            {"columns",obj({{"id","string"},{"value","number"}})},
            {"dependencies",arr({"device.command"})},{"write_target","device.command"},{"write_mode","merge"},
            {"decode","return tables['device.command'].map(function(r){return {id:r.id,value:r.value};});"},
            {"encode","return tables.rows;"}});
    };
    auto send=[&](SwRealtimeDb& db,const SwString& session,int value) {
        db.execute(obj({{"op","write"},{"session",session},{"table","adapter.command"},
            {"rows",arr({obj({{"id","current"},{"value",value}})})}}));
    };
    for(int pass=0;pass<2;++pass) {
        SwRealtimeDb db(transform,options);
        if(pass) {
            const auto stored=db.execute(obj({{"op","introspect"},{"table","adapter.command"},{"include_scripts",true}}))["tables"].toArray()[0].toObject();
            require(stored["writable"].toBool() && stored["write_mode"].toString()=="merge" &&
                stored["writers"].toArray()==arr({"client"}) && stored["encode"].toString()=="return tables.rows;",
                "persistence lost a codec, target merge mode or writer policy");
        }
        const auto owner=hello(db,"device"),writer=hello(db,"adapter"),client=hello(db,"client");
        db.execute(target(owner));db.execute(adapter(writer));
        rejects([&]{send(db,client,8);}); // A new lease does not freshen the retained command.
        db.execute(obj({{"op","write"},{"session",owner},{"table","device.command"},
            {"rows",arr({obj({{"id","current"},{"value",1},{"keep","unchanged"}})})}}));
        send(db,client,7+pass);
        const auto row=db.execute(obj({{"op","read"},{"table","device.command"}}))["rows"].toArray()[0].toObject();
        require(row["value"].toInt()==7+pass && row["keep"].toString()=="unchanged","encoded merge lost other fields");
    }
}
}
int main(){try{run();persistedMerges();std::cout<<"PASS: compiled view codecs, one-row replacement, notifications, persistence and writer identity\n";return 0;}
catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
