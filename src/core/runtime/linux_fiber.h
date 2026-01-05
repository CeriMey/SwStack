#pragma once
/***************************************************************************************************
 * This file is part of a project developed by Eymeric O'Neill.
 *
 * Copyright (C) 2025 Ariya Consulting
 * Author/Creator: Eymeric O'Neill
 * Contact: +33 6 52 83 83 31
 * Email: eymeric.oneill@gmail.com
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 ***************************************************************************************************/

static constexpr const char* kSwLogCategory_linux_fiber = "sw.core.runtime.linux_fiber";

#if !defined(_WIN32)

#include <ucontext.h>

#include <cstddef>
#include <cstdint>
#include <iostream>
#include <memory>
#include <new>

#include "SwDebug.h"

#ifndef WINAPI
#define WINAPI
#endif

#ifndef VOID
#define VOID void
#endif

#ifndef LPVOID
using LPVOID = void *;
#endif

#ifndef SIZE_T
using SIZE_T = std::size_t;
#endif

#ifndef LPFIBER_START_ROUTINE
using LPFIBER_START_ROUTINE = VOID(WINAPI *)(LPVOID);
#endif

namespace swcore::linux_fiber
{
constexpr std::size_t kDefaultStackSize = 64 * 1024;

struct Fiber
{
    ucontext_t context;
    LPFIBER_START_ROUTINE startRoutine{nullptr};
    LPVOID parameter{nullptr};
    std::unique_ptr<char[]> stack;
    std::size_t stackSize{kDefaultStackSize};
    bool isMain{false};
};

inline Fiber *&currentFiberRef()
{
    static thread_local Fiber *current = nullptr;
    return current;
}

inline void fiber_entry(uintptr_t fiberPtr)
{
    Fiber *fiber = reinterpret_cast<Fiber *>(fiberPtr);
    if (fiber && fiber->startRoutine)
    {
        fiber->startRoutine(fiber->parameter);
    }
}
} // namespace swcore::linux_fiber

inline LPVOID ConvertThreadToFiber(LPVOID)
{
    using namespace swcore::linux_fiber;
    Fiber *fiber = new (std::nothrow) Fiber();
    if (!fiber)
    {
        swCError(kSwLogCategory_linux_fiber) << "[linux_fiber] Failed to allocate main fiber";
        return nullptr;
    }

    fiber->isMain = true;
    if (getcontext(&fiber->context) == -1)
    {
        swCError(kSwLogCategory_linux_fiber) << "[linux_fiber] getcontext failed while converting thread to fiber";
        delete fiber;
        return nullptr;
    }

    fiber->stack.reset();
    currentFiberRef() = fiber;
    return fiber;
}

inline LPVOID CreateFiber(SIZE_T stackSize, LPFIBER_START_ROUTINE routine, LPVOID parameter)
{
    using namespace swcore::linux_fiber;
    Fiber *fiber = new (std::nothrow) Fiber();
    if (!fiber)
    {
        swCError(kSwLogCategory_linux_fiber) << "[linux_fiber] Failed to allocate fiber structure";
        return nullptr;
    }

    fiber->stackSize = stackSize ? stackSize : kDefaultStackSize;
    fiber->stack = std::unique_ptr<char[]>(new (std::nothrow) char[fiber->stackSize]);
    if (!fiber->stack)
    {
        swCError(kSwLogCategory_linux_fiber) << "[linux_fiber] Failed to allocate fiber stack";
        delete fiber;
        return nullptr;
    }

    fiber->startRoutine = routine;
    fiber->parameter = parameter;

    if (getcontext(&fiber->context) == -1)
    {
        swCError(kSwLogCategory_linux_fiber) << "[linux_fiber] getcontext failed while creating fiber";
        delete fiber;
        return nullptr;
    }

    fiber->context.uc_stack.ss_sp = fiber->stack.get();
    fiber->context.uc_stack.ss_size = fiber->stackSize;
    fiber->context.uc_link = nullptr;

    makecontext(&fiber->context, (void (*)())swcore::linux_fiber::fiber_entry, 1, reinterpret_cast<uintptr_t>(fiber));
    return fiber;
}

inline LPVOID GetCurrentFiber()
{
    return swcore::linux_fiber::currentFiberRef();
}

inline void SwitchToFiber(LPVOID fiberPtr)
{
    using namespace swcore::linux_fiber;
    Fiber *target = reinterpret_cast<Fiber *>(fiberPtr);
    if (!target)
    {
        return;
    }

    Fiber *previous = currentFiberRef();
    if (!previous)
    {
        previous = reinterpret_cast<Fiber *>(ConvertThreadToFiber(nullptr));
        if (!previous)
        {
            swCError(kSwLogCategory_linux_fiber) << "[linux_fiber] Unable to switch fibers: no current fiber";
            return;
        }
    }

    currentFiberRef() = target;
    if (swapcontext(&previous->context, &target->context) == -1)
    {
        swCError(kSwLogCategory_linux_fiber) << "[linux_fiber] swapcontext failed while switching fibers";
        currentFiberRef() = previous;
    }
}

inline void DeleteFiber(LPVOID fiberPtr)
{
    using namespace swcore::linux_fiber;
    Fiber *fiber = reinterpret_cast<Fiber *>(fiberPtr);
    if (!fiber || fiber->isMain)
    {
        return;
    }

    if (fiber == currentFiberRef())
    {
        currentFiberRef() = nullptr;
    }

    delete fiber;
}

#endif // !_WIN32
