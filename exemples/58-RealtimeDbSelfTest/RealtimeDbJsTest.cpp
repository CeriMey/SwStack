#include "SwRealtimeDbJsEvaluator.h"
#include <core/types/SwJsonDocument.h>
#include <chrono>
#include <atomic>
#include <iostream>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

namespace {
void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

std::string rejects(const SwRealtimeDbJsEvaluator& evaluator,
                    const SwString& script, const char* reason) {
    try {
        (void)evaluator.evaluate(script, {});
    } catch (const std::runtime_error& error) {
        return error.what();
    }
    throw std::runtime_error(reason);
}

SwJsonObject inputs() {
    SwJsonDocument document;
    SwString error;
    require(document.loadFromJson(
        R"({"tracking":[{"camera":"front","x":0.25},{"camera":"rear","x":0.75}],"gimbal":[{"camera":"front","yaw":12},{"camera":"rear","yaw":-4}]})",
        error), "input fixture failed");
    return document.object();
}

void reusedRuntimeIsolation(const SwRealtimeDbJsEvaluator& evaluator) {
    // A list of properties to delete cannot recover non-configurable changes,
    // frozen builtins, modified native function objects, or poisoned getters.
    // Every invocation must still receive an entirely pristine JS environment.
    const SwString contaminate(R"(
        var global = Function('return this')();
        Object.defineProperty(global, 'retainedInput', {value:tables, configurable:false});
        Object.defineProperty(Array.prototype, 'retainedMarker', {value:42, configurable:false});
        String.prototype.split = function() { return ['corrupt']; };
        Object.defineProperty(Math.max, 'retainedMarker', {get:function(){throw new Error('leak');}});
        Object.freeze(Object.prototype);
        return [{ok:retainedInput.input[0]}];
    )");
    for (int value = 0; value < 100; ++value) {
        SwJsonArray input; input.append(value); SwJsonObject tables; tables["input"] = input;
        require(evaluator.evaluate(contaminate, tables)[0].toObject()["ok"].toInt() == value,
                "reused VM retained a non-configurable global or previous input");
        const auto clean = evaluator.evaluate(R"(
            Object.prototype.fresh = true;
            return [{clean:typeof retainedInput === 'undefined' &&
                           typeof Array.prototype.retainedMarker === 'undefined' &&
                           typeof Math.max.retainedMarker === 'undefined' &&
                           Object.prototype.fresh && 'a,b'.split(',').length === 2}];
        )", {})[0].toObject();
        require(clean["clean"].toBool(), "reused VM retained frozen or modified builtin state");
    }
    // Exercise string-table/value-stack resizing, GC and circular transient
    // structures repeatedly; all allocations are reset even after a throw.
    for (int pass = 0; pass < 10; ++pass) {
        rejects(evaluator, R"(
            var root = {}, values = [];
            root.self = root;
            for (var i=0; i<3000; ++i) { root['key_'+i]=i; values.push({value:i}); }
            throw new Error('discard all transient state');
        )", "throw after VM resizing accepted");
        require(evaluator.evaluate("return [{ok:!Object.prototype.fresh}];", {})[0].toObject()["ok"].toBool(),
                "VM resizing or thrown evaluation poisoned the next view");
    }
}

void reusedRuntimeRecovery() {
    const SwRealtimeDbJsEvaluator shortBudget(5, 512 * 1024);
    const SwRealtimeDbJsEvaluator boundedHeap(1000, 512 * 1024);
    for (int pass = 0; pass < 8; ++pass) {
        require(rejects(shortBudget, "Object.prototype.poison=1; while(true){}", "timeout accepted").find(
                    "execution budget exceeded") != std::string::npos, "reused VM lost execution deadline");
        require(boundedHeap.evaluate("return [{clean:!Object.prototype.poison}];", {})[0].toObject()["clean"].toBool(),
                "timeout poisoned reused VM");
        require(rejects(boundedHeap, "var x=[]; while(true){x.push(new Array(20000));}", "OOM accepted").find(
                    "memory budget exceeded") != std::string::npos, "reused VM lost heap quota");
        require(boundedHeap.evaluate("return [{ok:'a,b'.split(',').length}];", {})[0].toObject()["ok"].toInt() == 2,
                "OOM poisoned reused VM");
    }
    require(boundedHeap.evaluate(R"(
        for (var i=0; i<5000; ++i) { var cycle={}; cycle.self=cycle; }
        return [{ok:true}];
    )", {})[0].toObject()["ok"].toBool(), "transient cycles could not be garbage collected within quota");
    for (const auto heapLimit : {256 * 1024, 512 * 1024, 1024 * 1024, 4 * 1024 * 1024, 512 * 1024}) {
        const SwRealtimeDbJsEvaluator changedQuota(100, heapLimit);
        require(changedQuota.evaluate("return [{clean:!Object.prototype.poison, engine:typeof Duktape}];", {})[0]
                    .toObject()["clean"].toBool(), "heap quota change retained VM state");
        require(changedQuota.evaluate("return [{engine:typeof Duktape}];", {})[0]
                    .toObject()["engine"].toString() == "undefined", "sandbox exposed native finalizer registration");
    }
}

void independentWorkerRuntimes() {
    // Thread-local cache teardown must free each private heap exactly once;
    // the same evaluator and script are safe across independent view workers.
    const SwRealtimeDbJsEvaluator evaluator(1000);
    const SwString script(R"(
        var clean = !Object.prototype.workerInput;
        Object.prototype.workerInput = tables.input;
        return [{clean:clean, value:tables.input[0]}];
    )");
    std::atomic<bool> passed{true};
    std::vector<std::thread> workers;
    for (int worker = 0; worker < 4; ++worker) {
        workers.emplace_back([&, worker] {
            try {
                for (int pass = 0; pass < 100; ++pass) {
                    const int expected = worker * 100 + pass;
                    SwJsonArray input; input.append(expected); SwJsonObject tables; tables["input"] = input;
                    const auto row = evaluator.evaluate(script, tables)[0].toObject();
                    if (!row["clean"].toBool() || row["value"].toInt() != expected) passed = false;
                }
            } catch (...) { passed = false; }
        });
    }
    for (auto& worker : workers) worker.join();
    require(passed, "concurrent view workers shared JS globals or input values");
}

void deadlineDuringAllocation() {
    const SwRealtimeDbJsEvaluator warm(1000);
    const SwRealtimeDbJsEvaluator tight(5);
    (void)warm.evaluate("return [];", {});
    const auto start = std::chrono::steady_clock::now();
    for (int pass = 0; pass < 5; ++pass) {
        // Once the deadline expires, refusing every allocation must not send
        // the VM into repeated emergency-GC / error-construction attempts.
        for (const auto* script : {
                "var held=[];for(;;){var x={data:new Array(128)};x.self=x;held.push(x);if(held.length>32)held=[];}",
                "var x=[];for(var i=0;i<50000;++i)x.push(i);return [x];"}) {
            require(rejects(tight, script, "allocation-heavy view escaped deadline").find(
                        "execution budget exceeded") != std::string::npos,
                    "allocation-heavy view did not respect deadline");
            require(warm.evaluate("return [{ok:true}];", {})[0].toObject()["ok"].toBool(),
                    "interrupted allocation or GC poisoned the next evaluation");
        }
    }
    require(std::chrono::steady_clock::now() - start < std::chrono::seconds(2),
            "expired allocations caused runaway emergency GC");
}
}

int main() {
    try {
        using JsViewEvaluator = SwRealtimeDbJsEvaluator;
        // Use a generous ordinary-test budget for loaded CI machines; timeout
        // behavior is exercised below with its own small budget.
        const JsViewEvaluator evaluator(100);
        const auto tables = inputs();
        const auto rows = evaluator.evaluate(R"(
            return tables.tracking.map(function(target) {
                var pose = tables.gimbal.filter(function(gimbal) {
                    return gimbal.camera === target.camera;
                })[0];
                return {camera: target.camera, yaw: pose.yaw, error: target.x - 0.5};
            });
        )", tables);
        require(rows.size() == 2, "join row count");
        require(rows[0].toObject()["yaw"].toInt() == 12, "join yaw");
        require(rows[1].toObject()["error"].toDouble() == 0.25, "mapping value");
        rejects(evaluator, "return {x:1};", "non-array result accepted");
        rejects(evaluator, "return [;", "syntax error accepted");
        rejects(evaluator, "throw new Error('failure');", "JS exception accepted");
        rejects(evaluator, "return [{x:Infinity}];", "infinite number accepted");
        rejects(evaluator, "return [{x:NaN}];", "NaN accepted");
        rejects(evaluator, "return [{x:undefined}];", "undefined accepted");
        rejects(evaluator, "return [{x:function(){}}];", "function accepted");
        rejects(evaluator, "var a={}; a.self=a; return [a];", "cycle accepted");
        rejects(evaluator, "return new Array(1000000000);", "large sparse array accepted");
        rejects(evaluator, "return [{x: /a/.test('a')}];", "unbudgeted regular expression accepted");
        rejects(evaluator, "return [{x:'aaaa'.replace(/a/,'b')}];", "regular expression replacement accepted");
        const auto strings = evaluator.evaluate(
            "return [{parts:'a,b'.split(','), value:'a-b'.replace('-','+')}];", {});
        require(strings[0].toObject()["parts"].toArray().size() == 2, "string split unavailable");
        require(strings[0].toObject()["value"].toString() == "a+b", "string replacement unavailable");
        const auto noAmbientState = evaluator.evaluate(
            "return [{date:typeof Date,random:typeof Math.random,duktape:typeof Duktape}];", {});
        require(noAmbientState[0].toObject()["date"].toString() == "undefined", "ambient clock exposed");
        require(noAmbientState[0].toObject()["random"].toString() == "undefined", "ambient random exposed");
        require(noAmbientState[0].toObject()["duktape"].toString() == "undefined", "engine internals exposed");

        const auto original = SwJsonDocument(tables).toJson();
        (void)evaluator.evaluate("tables.tracking[0].x=99; return tables.tracking;", tables);
        require(SwJsonDocument(tables).toJson() == original, "script mutated caller snapshot");
        const auto serialized = evaluator.evaluate(R"(
            Object.prototype.toJSON=function(){return {corrupted:true};};
            return [{ok:true}];
        )", {});
        require(serialized[0].toObject()["ok"].toBool(), "prototype mutation changed serialized values");
        const SwString cachedScript("var clean=!Object.prototype.cachedFlag; Object.prototype.cachedFlag=true; return [{clean:clean,value:tables.input[0]}];");
        for (int value = 0; value < 3; ++value) {
            SwJsonArray input; input.append(value); SwJsonObject tables; tables["input"] = input;
            const auto result = evaluator.evaluate(cachedScript, tables)[0].toObject();
            require(result["clean"].toBool() && result["value"].toInt() == value,
                    "Cached bytecode retained globals or input values");
        }
        const auto isolated = evaluator.evaluate("return [{clean:!Object.prototype.toJSON}];", {});
        require(isolated[0].toObject()["clean"].toBool(), "heap state leaked across evaluations");
        reusedRuntimeIsolation(evaluator);
        reusedRuntimeRecovery();
        {
            const SwRealtimeDbJsEvaluator cached(1000);
            const SwString script = "return [{value:tables.value||0}];";
            const auto program = cached.prepare(script);
            require(cached.compiledProgramCount()==0, "preparing an unread view executed compilation");
            for (int i=0;i<10;++i) {
                SwJsonObject input; input["value"]=i;
                require(cached.evaluate(program,input)[0].toObject()["value"].toInt()==i, "bytecode reused stale inputs");
            }
            std::thread worker([&] { cached.evaluate(program, {}); }); worker.join();
            require(cached.compiledProgramCount()==1, "same script compiled again across worker threads");
            cached.evaluate(script+"\n// revision", {});
            require(cached.compiledProgramCount()==2, "revised script reused old bytecode");
        }
        independentWorkerRuntimes();
        deadlineDuringAllocation();

        const auto start = std::chrono::steady_clock::now();
        const JsViewEvaluator shortBudget(5);
        require(rejects(shortBudget, "while(true) {}", "infinite loop accepted").find(
                    "execution budget exceeded") != std::string::npos, "infinite loop did not reach execution limit");
        rejects(shortBudget, "try{while(true){}}catch(e){while(true){}}", "timeout catch evaded limit");
        rejects(shortBudget, "return [{get x(){while(true){}}}];", "getter evaded limit");
        require(std::chrono::steady_clock::now() - start < std::chrono::seconds(2), "timeouts failed to return promptly");
        // Repeated references make allocation exceed the heap limit without
        // requiring long CPU work or a huge source/transport payload.
        require(rejects(JsViewEvaluator(1000, 512 * 1024),
                        "var values=[]; while(true){values.push(new Array(20000));}",
                        "heap exhaustion accepted").find("memory budget exceeded") != std::string::npos,
                "allocation test did not reach memory limit");
        const auto recovered = evaluator.evaluate("return [{ok:true}];", {});
        require(recovered[0].toObject()["ok"].toBool(), "failed view poisoned later evaluation");
        std::cout << "JS view mapping, isolation and execution limits passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
