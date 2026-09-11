#include "JsValueBridge.hpp"
#include "ValueBridge.h"
#include "JsProfile.h"
#include <core/types/SwDebug.h>
#include <algorithm>
#include <array>
#include <charconv>
#include <cmath>
#include <exception>
#include <cstdlib>
#include <limits>
#include <map>
#include <mutex>
#include <stdexcept>
#include <vector>

namespace {
// Charge the equivalent compact JSON size without building a JSON document.
// In particular, control characters cannot bypass the 1 MiB transport bound.
class ValueBudget {
public:
    void add(std::size_t size) {
        if (size > limit - bytes_) throw std::runtime_error("JS view values exceed 1 MiB");
        bytes_ += size;
    }
    void string(const char* data, std::size_t size) {
        add(2);
        for (std::size_t i = 0; i < size; ++i) {
            const auto ch = static_cast<unsigned char>(data[i]);
            add(ch == '"' || ch == '\\' || ch == '\b' || ch == '\f' ||
                ch == '\n' || ch == '\r' || ch == '\t' ? 2 : (ch < 0x20 ? 6 : 1));
        }
    }
    void number(const SwJsonValue& value) {
        char buffer[64];
        std::to_chars_result result;
        if (value.type() == SwJsonValue::Type::Integer)
            result = std::to_chars(buffer, buffer + sizeof(buffer), value.toLongLong());
        else
            result = std::to_chars(buffer, buffer + sizeof(buffer), value.toDouble(),
                                   std::chars_format::general, std::numeric_limits<double>::max_digits10);
        if (result.ec != std::errc{}) throw std::runtime_error("JS view number size overflow");
        add(static_cast<std::size_t>(result.ptr - buffer));
        if (value.type() == SwJsonValue::Type::Double &&
            std::find(buffer, result.ptr, '.') == result.ptr &&
            std::find(buffer, result.ptr, 'e') == result.ptr) add(2); // SwJson's .0
    }
private:
    static constexpr std::size_t limit = 1024 * 1024;
    std::size_t bytes_{0};
};

// Walk borrowed immutable SwJson containers. Only one string is copied at a
// time; there is no serialized input, cloned tree or flattened token buffer.
class InputValues {
    struct Frame {
        const SwJsonObject* object{nullptr};
        const SwJsonArray* array{nullptr};
        SwJsonObject::Container::const_iterator next;
        std::size_t index{0};
    };
public:
    explicit InputValues(const SwJsonObject& tables) : tables_(tables) { stack_.reserve(64); }
    void next(SwRtDbValue& token) {
        token = {};
        if (!started_) {
            started_ = true;
            begin(&tables_, nullptr, token);
        } else if (pending_) {
            const auto* value = pending_;
            pending_ = nullptr;
            scalarOrContainer(*value, token);
        } else {
            if (stack_.empty()) throw std::runtime_error("unexpected end of typed input");
            auto& frame = stack_.back();
            if (frame.object && frame.next != frame.object->dataRef().end()) {
                const auto& entry = *frame.next++;
                if (frame.index++) budget_.add(1);
                budget_.string(entry.first.c_str(), entry.first.size()); budget_.add(1);
                token.type = SW_RTDB_KEY; token.text = entry.first.c_str(); token.length = entry.first.size();
                pending_ = &entry.second;
            } else if (frame.array && frame.index < frame.array->size()) {
                if (frame.index) budget_.add(1);
                const auto& value = (*frame.array)[frame.index++];
                scalarOrContainer(value, token);
            } else {
                budget_.add(1); token.type = SW_RTDB_END; stack_.pop_back();
            }
        }
    }
private:
    void begin(const SwJsonObject* object, const SwJsonArray* array, SwRtDbValue& token) {
        if ((!object && !array) || stack_.size() >= 64)
            throw std::runtime_error("invalid or excessively nested JS input container");
        for (const auto& frame : stack_)
            if ((object && frame.object == object) || (array && frame.array == array))
                throw std::runtime_error("cyclic JS input");
        budget_.add(1);
        Frame frame; frame.object = object; frame.array = array;
        if (object) frame.next = object->dataRef().begin();
        stack_.push_back(frame);
        token.type = object ? SW_RTDB_OBJECT : SW_RTDB_ARRAY;
    }
    void scalarOrContainer(const SwJsonValue& value, SwRtDbValue& token) {
        switch (value.type()) {
        case SwJsonValue::Type::Null: token.type = SW_RTDB_NULL; budget_.add(4); break;
        case SwJsonValue::Type::Boolean:
            token.type = SW_RTDB_BOOLEAN; token.number = value.toBool(); budget_.add(value.toBool() ? 4 : 5); break;
        case SwJsonValue::Type::Integer:
        case SwJsonValue::Type::Double:
            token.type = SW_RTDB_NUMBER; token.number = value.toDouble();
            if (!std::isfinite(token.number)) throw std::runtime_error("non-finite JS input number");
            budget_.number(value); break;
        case SwJsonValue::Type::String:
            text_ = value.toString(); token.type = SW_RTDB_STRING;
            token.text = text_.c_str(); token.length = text_.size();
            budget_.string(token.text, token.length); break;
        case SwJsonValue::Type::Object: begin(value.toObjectPtr().get(), nullptr, token); break;
        case SwJsonValue::Type::Array: begin(nullptr, value.toArrayPtr().get(), token); break;
        }
    }
    const SwJsonObject& tables_;
    bool started_{false};
    const SwJsonValue* pending_{nullptr};
    std::vector<Frame> stack_;
    SwString text_;
    ValueBudget budget_;
};

// Build each output container in its final location, avoiding deep copies when
// closing containers. Partial output stays owned here if the VM aborts.
class OutputValues {
    struct Frame {
        std::shared_ptr<SwJsonObject> object;
        std::shared_ptr<SwJsonArray> array;
        SwString key;
        bool hasKey{false};
    };
public:
    OutputValues() { stack_.reserve(64); }
    void put(const SwRtDbValue& token) {
        if (token.type == SW_RTDB_KEY) {
            auto& parent = stack_.back();
            budget_.string(token.text, token.length); budget_.add(1);
            parent.key = SwString(token.text, token.length); parent.hasKey = true;
            return;
        }
        if (token.type == SW_RTDB_END) {
            budget_.add(1); stack_.pop_back(); return;
        }
        SwJsonValue value;
        Frame container;
        switch (token.type) {
        case SW_RTDB_NULL: budget_.add(4); break;
        case SW_RTDB_BOOLEAN: value = SwJsonValue(token.number != 0); budget_.add(token.number ? 4 : 5); break;
        case SW_RTDB_NUMBER:
            // ECMAScript Numbers remain doubles; materialize exactly integral,
            // representable results as int64, as the previous parser did.
            if (!std::isfinite(token.number)) throw std::runtime_error("non-finite JS output number");
            if (std::trunc(token.number) == token.number && token.number >= -9223372036854775808.0 &&
                token.number < 9223372036854775808.0)
                value = SwJsonValue(static_cast<long long>(token.number));
            else value = SwJsonValue(token.number);
            budget_.number(value); break;
        case SW_RTDB_STRING:
            budget_.string(token.text, token.length); value = SwJsonValue(SwString(token.text, token.length)); break;
        case SW_RTDB_OBJECT:
            budget_.add(1); container.object = std::make_shared<SwJsonObject>(); value = SwJsonValue(container.object); break;
        case SW_RTDB_ARRAY:
            budget_.add(1); container.array = std::make_shared<SwJsonArray>(); value = SwJsonValue(container.array); break;
        default: throw std::runtime_error("unexpected typed JS output");
        }
        if (stack_.empty()) root_ = std::move(value);
        else {
            auto& parent = stack_.back();
            if (parent.object) {
                if (!parent.hasKey) throw std::runtime_error("missing JS property key");
                if (!parent.object->isEmpty()) budget_.add(1);
                (*parent.object)[parent.key] = std::move(value); parent.hasKey = false;
            } else {
                if (!parent.array->isEmpty()) budget_.add(1);
                parent.array->append(SwJsonValue());
                (*parent.array)[parent.array->size() - 1] = std::move(value);
            }
        }
        if (container.object || container.array) stack_.push_back(std::move(container));
    }
    SwJsonArray take() {
        if (!stack_.empty() || !root_.isArray()) throw std::runtime_error("incomplete typed JS output");
        return std::move(*root_.toArrayPtr());
    }
private:
    SwJsonValue root_;
    std::vector<Frame> stack_;
    ValueBudget budget_;
};

struct Bridge {
    explicit Bridge(const SwJsonObject& tables) : input(tables) {}
    InputValues input;
    OutputValues output;
    std::exception_ptr failure;
    std::vector<char> compiled;
    static int saveCompiled(void* context, const char* code, std::size_t length) noexcept {
        auto& self = *static_cast<Bridge*>(context);
        try { if (length <= 1024 * 1024) self.compiled.assign(code, code + length); return 1; }
        catch (...) { self.failure = std::current_exception(); return 0; }
    }
    static int read(void* context, SwRtDbValue* value) noexcept {
        auto& self = *static_cast<Bridge*>(context);
        try { self.input.next(*value); return 1; }
        catch (...) { self.failure = std::current_exception(); return 0; }
    }
    static int write(void* context, const SwRtDbValue* value) noexcept {
        auto& self = *static_cast<Bridge*>(context);
        try { self.output.put(*value); return 1; }
        catch (...) { self.failure = std::current_exception(); return 0; }
    }
};

// One initialized, isolated VM per calling thread. All evaluator instances on
// that thread share execution resources; application values are removed before
// returning from the C boundary, including on quota and conversion failures.
class Runtime {
public:
    ~Runtime() { sw_realtime_db_destroy_js_runtime(runtime_); }
    SwRtDbJsRuntime* get(std::size_t heapLimit) {
        if (runtime_ && heapLimit_ != heapLimit) {
            sw_realtime_db_destroy_js_runtime(runtime_); runtime_ = nullptr;
        }
        if (!runtime_) {
            runtime_ = sw_realtime_db_create_js_runtime(heapLimit);
            heapLimit_ = heapLimit;
        }
        if (!runtime_) throw std::runtime_error("JS view: unable to allocate JS heap within memory budget");
        return runtime_;
    }
private:
    SwRtDbJsRuntime* runtime_{nullptr};
    std::size_t heapLimit_{0};
};
}

extern "C" int sw_realtime_db_js_profile_enabled(void) {
    static const bool enabled = std::getenv("SW_RTDB_PROFILE") != nullptr;
    return enabled;
}

extern "C" void sw_realtime_db_js_profile_record(const uint64_t* phases, int success,
                                                  int bytecodeHit, uint64_t finishedNs) {
    // This function is called after restoring the VM. No exception may cross
    // the C boundary, and no reporting thread or file is introduced.
    try {
        struct Totals {
            std::array<uint64_t, SW_RTDB_JS_PHASE_COUNT> ns{};
            uint64_t evaluations{0}, failures{0}, hits{0};
        };
        struct Profile {
            std::mutex mutex;
            Totals totals;
            uint64_t lastNs{0};
        };
        static Profile profile;
        Totals report;uint64_t elapsed;
        {
            std::lock_guard<std::mutex> lock(profile.mutex);
            if (!profile.lastNs) profile.lastNs = finishedNs;
            for (size_t i = 0; i < report.ns.size(); ++i) profile.totals.ns[i] += phases[i];
            ++profile.totals.evaluations;
            profile.totals.failures += !success;
            profile.totals.hits += bytecodeHit != 0;
            // Worker completion callbacks may arrive out of timestamp order.
            if (finishedNs < profile.lastNs || finishedNs - profile.lastNs < 1000000000ULL) return;
            elapsed = finishedNs - profile.lastNs;
            report = profile.totals;profile.totals = {};profile.lastNs = finishedNs;
        }
        uint64_t total = 0;for (auto ns : report.ns) total += ns;
        swCDebug("sw.core.storage.realtimedb.js.profile")
            << "clock=monotonic_wall evals=" << report.evaluations
            << " failures=" << report.failures << " bytecode_hits=" << report.hits
            << " bytecode_misses=" << report.evaluations-report.hits << " window_ms=" << elapsed/1e6
            << " total_ms=" << total/1e6 << " input_ms=" << report.ns[SW_RTDB_JS_INPUT]/1e6
            << " load_compile_ms=" << report.ns[SW_RTDB_JS_LOAD]/1e6
            << " execute_ms=" << report.ns[SW_RTDB_JS_EXECUTE]/1e6
            << " output_ms=" << report.ns[SW_RTDB_JS_OUTPUT]/1e6
            << " restore_ms=" << report.ns[SW_RTDB_JS_RESTORE]/1e6;
    } catch (...) {}
}

SwJsonArray swRealtimeDbDetail::evaluateTypedValues(const SwString& function, const SwJsonObject& tables,
                                                  std::uint64_t deadlineNs, std::size_t heapLimitBytes, JsPrograms& programs, std::shared_ptr<const std::vector<char>>* pinned) {
    Bridge state(tables);
    // Bound both source and bytecode storage; the reusable VM itself contains
    // only pristine sandboxed builtins between evaluations.
    static thread_local Runtime runtime;
    std::unique_lock<std::mutex> lock(programs.mutex, std::defer_lock);
    auto code = pinned ? *pinned : nullptr;
    if (!code) {
        lock.lock();
        const auto found = programs.entries.find(function);
        code = found == programs.entries.end() ? nullptr : found->second;
        if (code) lock.unlock();
        else if (programs.entries.size() >= 512 || programs.bytes + function.size() + 1024 * 1024 > 8 * 1024 * 1024)
            throw std::runtime_error("JS compiled program cache capacity exceeded");
    }
    // The first compilation is serialized. Cache hits execute concurrently in
    // isolated per-thread VMs, using immutable process-internal bytecode.
    const SwRtDbValueBridge bridge{&state, &Bridge::read, &Bridge::write,
        code ? code->data() : nullptr, code ? code->size() : 0, &Bridge::saveCompiled};
    char error[512] = {};
    const int success = sw_realtime_db_evaluate_js(runtime.get(heapLimitBytes),
                                                  function.c_str(), function.size(), &bridge,
                                                  deadlineNs, error, sizeof(error));
    if (!state.compiled.empty()) {
        const auto size = function.size() + state.compiled.size();
        code = std::make_shared<const std::vector<char>>(std::move(state.compiled));
        programs.entries.emplace(function, code);
        programs.bytes += size;
    }
    if (pinned) *pinned = code;
    if (lock.owns_lock()) lock.unlock();
    // The C protected call has returned and restored its heap before exceptions
    // are rethrown, so neither longjmp nor C++ unwinding crosses the boundary.
    if (state.failure) std::rethrow_exception(state.failure);
    if (!success) throw std::runtime_error(std::string("JS view: ") + error);
    return state.output.take();
}
