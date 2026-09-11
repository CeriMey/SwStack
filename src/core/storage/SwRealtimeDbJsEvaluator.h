#pragma once

#include <core/types/SwJsonArray.h>
#include <core/types/SwJsonObject.h>
#include <core/types/SwString.h>
#include <cstddef>
#include <memory>
namespace swRealtimeDbDetail { struct JsPrograms; struct JsProgram; }


// Each evaluation starts from a pristine, limited JS heap. One initialized VM
// is reused per calling thread and reset completely after every call. Tables
// are an immutable input snapshot; scripts cannot write back to RtDb.
class SwRealtimeDbJsEvaluator {
public:
    explicit SwRealtimeDbJsEvaluator(int budgetMs = 5,
                             std::size_t heapLimitBytes = 4 * 1024 * 1024);

    // script is an ES5 function body with a `tables` argument. It must return
    // an array containing finite JSON values. Throws std::runtime_error on
    // script, value-conversion, execution-budget or memory-budget failures.
    // Values cross a typed C boundary without JSON text or shared mutable state.
    // heapLimitBytes bounds retained VM allocations. A private image of the
    // sandboxed builtins adds at most the same amount outside the VM; it holds
    // no application values. Source/bytecode cache is shared by this evaluator across threads and capped
    // at 512 programs / 8 MiB. Full caches reject new scripts instead of
    // evicting and recompiling active programs. Compilation is lazy on first use.
    // Source is limited to 64 KiB; input/output to 1 MiB in equivalent compact
    // JSON size (counted without serialization). Date, random,
    // engine internals and RegExp matching are unavailable; string replace and
    // split accept string search/separator arguments. The time limit is a
    // cooperative elapsed-time budget, not a hard real-time scheduling guarantee.
    using Program = std::shared_ptr<swRealtimeDbDetail::JsProgram>;
    // The handle pins runtime bytecode on first evaluation and owns it thereafter.
    Program prepare(const SwString& script) const;
    SwJsonArray evaluate(const Program& program, const SwJsonObject& tables) const;
    std::size_t compiledProgramCount() const;
    SwJsonArray evaluate(const SwString& script, const SwJsonObject& tables) const;

private:
    std::shared_ptr<swRealtimeDbDetail::JsPrograms> programs_;
    int budgetMs_;
    std::size_t heapLimitBytes_;
};
