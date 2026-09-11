#include "SwRealtimeDbJsEvaluator.h"
#include <cmath>
#include <iostream>
#include <limits>
#include <memory>
#include <stdexcept>

namespace {
void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}
template<class Function> std::string rejects(Function call, const char* message) {
    try { call(); }
    catch (const std::runtime_error& error) { return error.what(); }
    throw std::runtime_error(message);
}

void typesAndStrings() {
    const SwRealtimeDbJsEvaluator evaluator(100);
    SwJsonObject row;
    row["id"] = "sensor";
    row["null"] = SwJsonValue(); row["yes"] = true; row["no"] = false;
    row["integer"] = static_cast<long long>(9007199254740991LL);
    row["fraction"] = 0.12345678901234568;
    row["text"] = SwString("a\0b\n\t\"\\", 7);
    row["unicode"] = SwString("caf\xc3\xa9 \xe6\xb8\xa9\xe5\xba\xa6 \xf0\x9f\x9a\x80");
    row[SwString("key\0tail", 8)] = "embedded key NUL";
    row["__proto__"] = "own property";
    row["constructor"] = "data";
    SwJsonArray nested; nested.append(false); nested.append(SwJsonValue()); nested.append(row["unicode"]);
    SwJsonObject object; object["array"] = nested; object["empty"] = SwJsonObject();
    row["nested"] = object; row["empty"] = SwJsonArray();
    SwJsonArray input; input.append(row);
    SwJsonObject tables; tables["sensors"] = input;
    const auto output = evaluator.evaluate("return tables.sensors;", tables);
    require(output == input, "typed roundtrip changed scalar/container types or string bytes");
    const auto properties = evaluator.evaluate(R"(
        return [{own:Object.prototype.hasOwnProperty.call(tables.sensors[0], '__proto__'),
                 prototype:Object.getPrototypeOf(tables.sensors[0]) === Object.prototype,
                 nul:tables.sensors[0].text.charCodeAt(1),
                 key:tables.sensors[0]['key\x00tail']}];
    )", tables)[0].toObject();
    require(properties["own"].toBool() && properties["prototype"].toBool(), "__proto__ changed input prototype");
    require(properties["nul"].toInt() == 0 && properties["key"].toString() == "embedded key NUL", "NUL was truncated");
    const auto unchanged = tables;
    (void)evaluator.evaluate("tables.sensors[0].nested.array.push(42); return tables.sensors;", tables);
    require(tables == unchanged, "JS modified borrowed C++ input");
    const auto created = evaluator.evaluate(R"(
        var result = Object.create(null);
        result['__proto__'] = 7;
        result['key\x00tail'] = 'a\x00b';
        result.text = '\u00e9\u6e29';
        return [result];
    )", {})[0].toObject();
    require(created["__proto__"].toInt() == 7 && created[SwString("key\0tail", 8)].toString() == SwString("a\0b", 3),
            "typed output changed special property names or NUL strings");
    require(created["text"].toString() == SwString("\xc3\xa9\xe6\xb8\xa9"), "JS Unicode escape changed bytes");
}

void numbers() {
    const SwRealtimeDbJsEvaluator evaluator(100);
    SwJsonObject numbers;
    numbers["max"] = std::numeric_limits<double>::max();
    numbers["tiny"] = std::numeric_limits<double>::denorm_min();
    numbers["large"] = 1e20;
    numbers["min_integer"] = std::numeric_limits<long long>::min();
    numbers["max_integer"] = std::numeric_limits<long long>::max();
    numbers["negative_zero"] = -0.0;
    SwJsonObject tables; tables["numbers"] = numbers;
    const auto result = evaluator.evaluate("return [tables.numbers];", tables)[0].toObject();
    require(result["max"].toDouble() == std::numeric_limits<double>::max(), "large finite double changed");
    require(result["tiny"].toDouble() == std::numeric_limits<double>::denorm_min(), "subnormal double changed");
    require(result["large"].toDouble() == 1e20, "large integral double changed");
    require(result["min_integer"].type() == SwJsonValue::Type::Integer &&
            result["min_integer"].toLongLong() == std::numeric_limits<long long>::min(), "int64 minimum changed");
    require(result["max_integer"].type() == SwJsonValue::Type::Double &&
            result["max_integer"].toDouble() == static_cast<double>(std::numeric_limits<long long>::max()),
            "JS rounded integer overflowed C++ int64");
    require(result["negative_zero"].type() == SwJsonValue::Type::Integer && result["negative_zero"].toInt() == 0,
            "result zero normalization changed");
    numbers["bad"] = std::numeric_limits<double>::quiet_NaN(); tables["numbers"] = numbers;
    rejects([&] { evaluator.evaluate("return [];", tables); }, "NaN input accepted");
    numbers["bad"] = std::numeric_limits<double>::infinity(); tables["numbers"] = numbers;
    rejects([&] { evaluator.evaluate("return [];", tables); }, "infinite input accepted");
}

void gettersAndFailures() {
    const SwRealtimeDbJsEvaluator evaluator(100);
    const auto result = evaluator.evaluate(R"(
        var calls = 0;
        var first = {get value() { calls++; return 42; }};
        Object.prototype.toJSON = function() { throw new Error('must never call toJSON'); };
        return [first, {get calls() { return calls; }}];
    )", {});
    require(result[0].toObject()["value"].toInt() == 42 && result[1].toObject()["calls"].toInt() == 1,
            "getter was evaluated more than once");
    auto shared = evaluator.evaluate("var x={value:1}; return [{a:x,b:x}];", {})[0].toObject();
    require(shared["a"] == shared["b"], "shared acyclic output was rejected");
    (*shared["a"].toObjectPtr())["value"] = 2;
    require(shared["b"].toObject()["value"].toInt() == 1, "separate output paths shared mutable C++ state");
    for (const auto* script : {
            "return [{ok:1}, {get broken(){throw new Error('getter failed');}}];",
            "var x={ok:1}; x.loop=x; return [x];",
            "return [{bad:function(){}}];", "return [undefined];", "return new Array(3);",
            "return [Object.create({inherited:true})];", "return [new Number(3)];",
            "return [new Uint8Array(4)];", "return [{bad:Symbol('x')}];",
            "var x={}; x[Symbol('key')]=1; return [x];"}) {
        rejects([&] { evaluator.evaluate(script, {}); }, "invalid typed output accepted");
        require(evaluator.evaluate("return [{clean:!Object.prototype.toJSON}];", {})[0].toObject()["clean"].toBool(),
                "failed output leaked JS state or partial C++ output");
    }
}

void bounds() {
    const SwRealtimeDbJsEvaluator evaluator(1000);
    SwJsonObject tables;
    tables["too_big"] = SwString(std::string(1024 * 1024, 'x'));
    require(rejects([&] { evaluator.evaluate("return [];", tables); }, "oversized input accepted").find("1 MiB") != std::string::npos,
            "input size cap was lost");
    // Escaped size matters even though there is no serialized JSON buffer now.
    tables["too_big"] = SwString(std::string(200000, '\0'));
    require(rejects([&] { evaluator.evaluate("return [];", tables); }, "oversized escaped input accepted").find("1 MiB") != std::string::npos,
            "escaped input size cap was lost");
    require(rejects([&] { evaluator.evaluate("var s='\\x00'; for(var i=0;i<18;i++)s+=s; return [{value:s}];", {}); },
                    "oversized output accepted").find("1 MiB") != std::string::npos, "output size cap was lost");
    rejects([&] { evaluator.evaluate("var x={}; var root=x; for(var i=0;i<80;i++){x.next={};x=x.next;} return [root];", {}); },
            "excessive output nesting accepted");
    SwJsonObject deep;
    auto level = std::make_shared<SwJsonObject>();
    deep["root"] = SwJsonValue(level);
    for (int i = 0; i < 80; ++i) {
        auto next = std::make_shared<SwJsonObject>();
        (*level)["next"] = SwJsonValue(next); level = std::move(next);
    }
    rejects([&] { evaluator.evaluate("return [];", deep); }, "excessive input nesting accepted");
    auto cycle = std::make_shared<SwJsonObject>();
    (*cycle)["self"] = SwJsonValue(cycle);
    SwJsonObject cyclic; cyclic["cycle"] = SwJsonValue(cycle);
    bool rejected = false;
    try { evaluator.evaluate("return [];", cyclic); }
    catch (const std::runtime_error&) { rejected = true; }
    cycle->remove("self");
    require(rejected, "cyclic C++ input accepted");
    require(evaluator.evaluate("return [{ok:1}];", {})[0].toObject()["ok"].toInt() == 1, "quota failure poisoned next evaluation");
}
}

int main() {
    try {
        typesAndStrings(); numbers(); gettersAndFailures(); bounds();
        std::cout << "Typed values, strings, numbers, ownership and failure boundaries passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "Typed value test: " << error.what() << '\n';
        return 1;
    }
}
