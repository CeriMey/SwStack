#pragma once

/**
 * @file src/core/object/SwObject.h
 * @ingroup core_object
 * @brief Declares the public interface exposed by SwObject in the CoreSw object model layer.
 *
 * This header belongs to the CoreSw object model layer. It defines parent and child ownership,
 * runtime typing, and the signal-slot machinery that many other modules build upon.
 *
 * Within that layer, this file focuses on the object interface. The declarations exposed here
 * define the stable surface that adjacent code can rely on while the implementation remains free
 * to evolve behind the header.
 *
 * The main declarations in this header are ConnectionType, ISlot, SlotMember,
 * SlotFunctionReceiver, SlotFunction, function_traits, slot_factory, and
 * slot_factory_with_receiver, plus related helper declarations.
 *
 * The declarations in this header are intended to make the subsystem boundary explicit: callers
 * interact with stable types and functions, while implementation details remain confined to
 * source files and private helpers.
 *
 * Object-model declarations here establish how instances are identified, connected, owned, and
 * moved across execution contexts.
 *
 */

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

#include "SwCoreApplication.h"
#include "atomic/thread.h"

#include <iostream>
#include <map>
#include <vector>
#include <functional>
#include "SwAny.h"
#include <tuple>
#include <type_traits>
#include <typeinfo>
#include <typeindex>
#include <utility>
#include <cstddef>
#include <cstdlib>
#include <condition_variable>
#include <mutex>
#include <memory>
#include <unordered_set>
#include <cstring>
#include <string>
static constexpr const char* kSwLogCategory_SwObject = "sw.core.object.swobject";

#if defined(__GNUG__)
#include <cxxabi.h>
#endif

class SwThread;

using ThreadHandle = sw::atomic::Thread;

namespace swcore_compat {
#if __cplusplus >= 201402L
template<typename T>
using decay_t = std::decay_t<T>;
template<std::size_t... I>
using index_sequence = std::index_sequence<I...>;
template<std::size_t N>
using make_index_sequence = std::make_index_sequence<N>;
#else
template<typename T>
using decay_t = typename std::decay<T>::type;

template<std::size_t... I>
struct index_sequence {};

template<std::size_t N, std::size_t... I>
struct make_index_sequence_impl : make_index_sequence_impl<N - 1, N - 1, I...> {};

template<std::size_t... I>
struct make_index_sequence_impl<0, I...> {
    using type = index_sequence<I...>;
};

template<std::size_t N>
using make_index_sequence = typename make_index_sequence_impl<N>::type;
#endif
} // namespace swcore_compat




#define VIRTUAL_PROPERTY(type, PROP_NAME) \
public: \
    /* Déclaration du setter virtuel pur */ \
    virtual void set##PROP_NAME(const type& value) = 0; \
    /* Déclaration du getter virtuel pur */ \
    virtual type get##PROP_NAME() const = 0;


#define CUSTOM_OVERRIDE_PROPERTY(__prop_type__, PROP_NAME, defautValue) \
private: \
    __prop_type__ m_##PROP_NAME = defautValue; \
public: \
    /* Setter with change check, user-defined change method, and signal emission */ \
    void set##PROP_NAME(const __prop_type__& value) override { \
        if (m_##PROP_NAME != value) { \
            m_##PROP_NAME = value; \
            on_##PROP_NAME##_changed(value); \
            emit PROP_NAME##Changed(value); \
        } \
    } \
    /* Getter for property */ \
    __prop_type__ get##PROP_NAME() const override { \
        return m_##PROP_NAME; \
    } \
    void register_##PROP_NAME##_setter() { \
        propertyArgumentTypeNameMap[#PROP_NAME] = typeid(__prop_type__).name(); \
        propertyOwnerClassMap[#PROP_NAME] = SwDemangleClassName(typeid(typename std::remove_reference<decltype(*this)>::type).name()); \
        propertySetterMap[#PROP_NAME] = [this](void* value) { \
        this->set##PROP_NAME(*static_cast<__prop_type__*>(value)); \
        }; \
        propertyGetterMap[#PROP_NAME] = [this]() -> void* { \
            return static_cast<void*>(&this->m_##PROP_NAME); \
        }; \
    } \
signals: \
    /* Signal declaration */ \
    DECLARE_SIGNAL(PROP_NAME##Changed, const __prop_type__&);\
protected: \
    virtual void on_##PROP_NAME##_changed(const __prop_type__& value)

#define CUSTOM_PROPERTY(__prop_type__, __prop_name__, __prop_default_value__) \
private: \
    __prop_type__ m_##__prop_name__ = __prop_default_value__; \
    public: \
    void set##__prop_name__(const __prop_type__& value) { \
        if (m_##__prop_name__ != value) { \
            m_##__prop_name__ = value; \
            on_##__prop_name__##_changed(value); \
            emit __prop_name__##Changed(value); \
    } \
} \
    __prop_type__ get##__prop_name__() const { \
        return m_##__prop_name__; \
} \
    template<typename T> \
    static bool register_##__prop_name__##_setter(T* instance) { \
        instance->propertySetterMap[#__prop_name__] = [instance](void* value) { \
                  instance->set##__prop_name__(*static_cast<__prop_type__*>(value)); \
          }; \
        instance->propertyGetterMap[#__prop_name__] = [instance]() -> void* { \
            return static_cast<void*>(&instance->m_##__prop_name__); \
        }; \
        instance->propertyArgumentTypeNameMap[#__prop_name__] = typeid(__prop_type__).name(); \
        instance->propertyOwnerClassMap[#__prop_name__] = SwDemangleClassName(typeid(T).name()); \
        return true; \
} \
    DECLARE_SIGNAL(__prop_name__##Changed, const __prop_type__&); \
    protected: \
    bool __##__prop_name__##__prop = register_##__prop_name__##_setter<typename std::remove_reference<decltype(*this)>::type>(this); \
    virtual void on_##__prop_name__##_changed(const __prop_type__& value)



#define PROPERTY(type, PROP_NAME, defautValue) \
    CUSTOM_PROPERTY(type, PROP_NAME, defautValue) { SW_UNUSED(value); }

#define SURCHARGE_ON_PROPERTY_CHANGED(type, PROP_NAME) \
protected: \
    virtual void on_##PROP_NAME##_changed(const type& value) override


//#ifndef signals
#define signals public
//#endif

//#ifndef slots
#define slots
//#endif

//#ifndef SIGNAL
#define SIGNAL(signalName) #signalName
//#endif

//#ifndef SLOT
#define SLOT(slotName) #slotName
//#endif

//#ifndef emit
#define emit
//#endif


#define SW_SIGNAL_REMOVE_PARENS(...) __VA_ARGS__

#define SW_SIGNAL_CONCAT(a, b) SW_SIGNAL_CONCAT_INNER(a, b)
#define SW_SIGNAL_CONCAT_INNER(a, b) a##b

#define SW_SIGNAL_ARG_N( \
    _1,_2,_3,_4,_5,_6,_7,_8,_9,_10, \
    _11,_12,_13,_14,_15,_16,_17,_18,_19,_20, \
    _21,_22,_23,_24,_25,_26,_27,_28,_29,_30, \
    _31,_32,_33,_34,_35,_36,_37,_38,_39,_40, \
    _41,_42,_43,_44,_45,_46,_47,_48,_49,_50, \
    _51,_52,_53,_54,_55,_56,_57,_58,_59,_60, \
    _61,_62,_63,N,...) N
#define SW_SIGNAL_RSEQ_N() \
    63,62,61,60,59,58,57,56,55,54,53,52,51,50,49,48,47,46,45,44,43,42,41,40,39,38,37,36,35,34,33,32,31,30,29,28,27,26,25,24,23,22,21,20,19,18,17,16,15,14,13,12,11,10,9,8,7,6,5,4,3,2,1,0
#define SW_SIGNAL_NARG_(...) SW_SIGNAL_ARG_N(__VA_ARGS__)
#define SW_SIGNAL_NARG(...) SW_SIGNAL_NARG_(__VA_ARGS__, SW_SIGNAL_RSEQ_N())

#define SW_SIGNAL_PARAMS_FROM_TYPES(...) \
    SW_SIGNAL_CONCAT(SW_SIGNAL_PARAMS_, SW_SIGNAL_NARG(__VA_ARGS__))(__VA_ARGS__)

#define SW_SIGNAL_PARAMS_0()
#define SW_SIGNAL_PARAMS_1(t1) t1 __sw_sig_arg0
#define SW_SIGNAL_PARAMS_2(t1, t2) t1 __sw_sig_arg0, t2 __sw_sig_arg1
#define SW_SIGNAL_PARAMS_3(t1, t2, t3) t1 __sw_sig_arg0, t2 __sw_sig_arg1, t3 __sw_sig_arg2
#define SW_SIGNAL_PARAMS_4(t1, t2, t3, t4) t1 __sw_sig_arg0, t2 __sw_sig_arg1, t3 __sw_sig_arg2, t4 __sw_sig_arg3
#define SW_SIGNAL_PARAMS_5(t1, t2, t3, t4, t5) t1 __sw_sig_arg0, t2 __sw_sig_arg1, t3 __sw_sig_arg2, t4 __sw_sig_arg3, t5 __sw_sig_arg4
#define SW_SIGNAL_PARAMS_6(t1, t2, t3, t4, t5, t6) t1 __sw_sig_arg0, t2 __sw_sig_arg1, t3 __sw_sig_arg2, t4 __sw_sig_arg3, t5 __sw_sig_arg4, t6 __sw_sig_arg5
#define SW_SIGNAL_PARAMS_7(t1, t2, t3, t4, t5, t6, t7) t1 __sw_sig_arg0, t2 __sw_sig_arg1, t3 __sw_sig_arg2, t4 __sw_sig_arg3, t5 __sw_sig_arg4, t6 __sw_sig_arg5, t7 __sw_sig_arg6
#define SW_SIGNAL_PARAMS_8(t1, t2, t3, t4, t5, t6, t7, t8) t1 __sw_sig_arg0, t2 __sw_sig_arg1, t3 __sw_sig_arg2, t4 __sw_sig_arg3, t5 __sw_sig_arg4, t6 __sw_sig_arg5, t7 __sw_sig_arg6, t8 __sw_sig_arg7

#define SW_SIGNAL_ARG_COMMA_0(...)
#define SW_SIGNAL_ARG_COMMA_1(...) , __sw_sig_arg0
#define SW_SIGNAL_ARG_COMMA_2(...) , __sw_sig_arg0, __sw_sig_arg1
#define SW_SIGNAL_ARG_COMMA_3(...) , __sw_sig_arg0, __sw_sig_arg1, __sw_sig_arg2
#define SW_SIGNAL_ARG_COMMA_4(...) , __sw_sig_arg0, __sw_sig_arg1, __sw_sig_arg2, __sw_sig_arg3
#define SW_SIGNAL_ARG_COMMA_5(...) , __sw_sig_arg0, __sw_sig_arg1, __sw_sig_arg2, __sw_sig_arg3, __sw_sig_arg4
#define SW_SIGNAL_ARG_COMMA_6(...) , __sw_sig_arg0, __sw_sig_arg1, __sw_sig_arg2, __sw_sig_arg3, __sw_sig_arg4, __sw_sig_arg5
#define SW_SIGNAL_ARG_COMMA_7(...) , __sw_sig_arg0, __sw_sig_arg1, __sw_sig_arg2, __sw_sig_arg3, __sw_sig_arg4, __sw_sig_arg5, __sw_sig_arg6
#define SW_SIGNAL_ARG_COMMA_8(...) , __sw_sig_arg0, __sw_sig_arg1, __sw_sig_arg2, __sw_sig_arg3, __sw_sig_arg4, __sw_sig_arg5, __sw_sig_arg6, __sw_sig_arg7

#define SW_SIGNAL_ARGS_WITH_COMMA_FROM_TYPES(...) \
    SW_SIGNAL_CONCAT(SW_SIGNAL_ARG_COMMA_, SW_SIGNAL_NARG(__VA_ARGS__))(__VA_ARGS__)

#define SW_SIGNAL_MEMBER_PTR(ClassType, ...) \
    SW_SIGNAL_CONCAT(SW_SIGNAL_MEMBER_PTR_, SW_SIGNAL_NARG(__VA_ARGS__))(ClassType, __VA_ARGS__)

#define SW_SIGNAL_MEMBER_PTR_0(ClassType, ...) void (ClassType::*)()
#define SW_SIGNAL_MEMBER_PTR_1(ClassType, t1) void (ClassType::*)(t1)
#define SW_SIGNAL_MEMBER_PTR_2(ClassType, t1, t2) void (ClassType::*)(t1, t2)
#define SW_SIGNAL_MEMBER_PTR_3(ClassType, t1, t2, t3) void (ClassType::*)(t1, t2, t3)
#define SW_SIGNAL_MEMBER_PTR_4(ClassType, t1, t2, t3, t4) void (ClassType::*)(t1, t2, t3, t4)
#define SW_SIGNAL_MEMBER_PTR_5(ClassType, t1, t2, t3, t4, t5) void (ClassType::*)(t1, t2, t3, t4, t5)
#define SW_SIGNAL_MEMBER_PTR_6(ClassType, t1, t2, t3, t4, t5, t6) void (ClassType::*)(t1, t2, t3, t4, t5, t6)
#define SW_SIGNAL_MEMBER_PTR_7(ClassType, t1, t2, t3, t4, t5, t6, t7) void (ClassType::*)(t1, t2, t3, t4, t5, t6, t7)
#define SW_SIGNAL_MEMBER_PTR_8(ClassType, t1, t2, t3, t4, t5, t6, t7, t8) void (ClassType::*)(t1, t2, t3, t4, t5, t6, t7, t8)

#define DECLARE_SIGNAL(signalName, ...) \
public: \
    using signalName##Signature = void(__VA_ARGS__); \
    static_assert(!std::is_same<signalName##Signature, void()>::value, "Use DECLARE_SIGNAL_VOID for zero-parameter signals."); \
    void signalName(SW_SIGNAL_PARAMS_FROM_TYPES(__VA_ARGS__)) { \
        using __SwSignalClass = typename std::decay<decltype(*this)>::type; \
        using __SwSignalPointer = SW_SIGNAL_MEMBER_PTR(__SwSignalClass, __VA_ARGS__); \
        emitSignal(#signalName SW_SIGNAL_ARGS_WITH_COMMA_FROM_TYPES(__VA_ARGS__)); \
        emitSignal(SwObject::createSignalKey(static_cast<__SwSignalPointer>(&__SwSignalClass::signalName)) \
            SW_SIGNAL_ARGS_WITH_COMMA_FROM_TYPES(__VA_ARGS__)); \
    } \
    template <typename... Args> \
    void invoke_##signalName(Args&&... args) { \
        signalName(std::forward<Args>(args)...); \
    }

#define DECLARE_SIGNAL_VOID(signalName) \
public: \
    using signalName##Signature = void(); \
    static const SwString& signalName##Signal() { \
        static const SwString s_signalNameStr(#signalName); \
        return s_signalNameStr; \
    } \
    void signalName() { \
        using __SwSignalClass = typename std::decay<decltype(*this)>::type; \
        using __SwSignalPointer = void (__SwSignalClass::*)(); \
        emitSignal(#signalName); \
        emitSignal(SwObject::createSignalKey(static_cast<__SwSignalPointer>(&__SwSignalClass::signalName))); \
    } \
    template <typename... Args> \
    void invoke_##signalName(Args&&... args) { \
        static_assert(sizeof...(Args) == 0, "This signal does not take parameters."); \
        signalName(); \
    }



inline SwString SwDemangleClassName(const char* name) {
    if (!name) {
        return {};
    }
#if defined(__GNUG__)
    int status = 0;
    char* demangled = abi::__cxa_demangle(name, nullptr, nullptr, &status);
    SwString result;
    if (status == 0 && demangled) {
        result = demangled;
    } else {
        result = name;
    }
    std::free(demangled);
    return result;
#else
    SwString fullName = name;
    int spaceIndex = fullName.indexOf(' ');
    if (spaceIndex != -1) {
        return fullName.mid(spaceIndex + 1);
    }
    return fullName;
#endif
}

// Macro SW_OBJECT pour générer className() et classHierarchy()
#define SW_OBJECT(DerivedClass, BaseClass)                                      \
public:                                                                         \
    static SwString staticClassName() {                                         \
        static SwString kClassName = SwDemangleClassName(typeid(DerivedClass).name()); \
        return kClassName;                                                      \
    }                                                                           \
    virtual SwString className() const override {                               \
        return DerivedClass::staticClassName();                                 \
    }                                                                           \
    virtual SwList<SwString> classHierarchy() const override {                  \
        SwList<SwString> hierarchy = BaseClass::classHierarchy();               \
        hierarchy.prepend(DerivedClass::staticClassName());                     \
        return hierarchy;                                                       \
    }



enum ConnectionType {
    AutoConnection,
    DirectConnection,
    QueuedConnection,
    BlockingQueuedConnection
};



template<typename T, typename... Args>
class ISlot {
public:
    /**
     * @brief Destroys the `ISlot` instance.
     *
     * @details Use this hook to release any resources that remain associated with the instance.
     */
    virtual ~ISlot() {}
    /**
     * @brief Performs the `invoke` operation.
     * @param instance Value passed to the method.
     * @param args Value passed to the method.
     * @return The requested invoke.
     */
    virtual void invoke(T* instance, Args... args) = 0;
    /**
     * @brief Returns the current receiveur.
     * @return The current receiveur.
     *
     * @details The returned value reflects the state currently stored by the instance.
     */
    virtual T* receiveur() = 0;
};

// Slot pour une méthode membre
template <typename T, typename... Args>
class SlotMember : public ISlot<T, Args...> {
public:
    /**
     * @brief Constructs a `SlotMember` instance.
     * @param instance Value passed to the method.
     * @param method HTTP method involved in the operation.
     *
     * @details The instance is initialized and prepared for immediate use.
     */
    SlotMember(T* instance, void (T::* method)(Args...)) : instance(instance), method(method) {}

    /**
     * @brief Performs the `invoke` operation.
     * @param instance Value passed to the method.
     * @param args Value passed to the method.
     */
    void invoke(T* instance, Args... args) override {
        (instance->*method)(args...);
    }

    /**
     * @brief Returns the current receiveur.
     * @return The current receiveur.
     *
     * @details The returned value reflects the state currently stored by the instance.
     */
    T* receiveur() override {
        return instance;
    }

    /**
     * @brief Performs the `void` operation.
     */
    void (T::*methodPtr() const)(Args...) {
        return method;
    }

private:
    T* instance;
    void (T::* method)(Args...);
};

// Slot fonctionnel avec receiver (this) et lambda
template <typename T, typename... Args>
class SlotFunctionReceiver : public ISlot<T, Args...> {
public:
    /**
     * @brief Constructs a `SlotFunctionReceiver` instance.
     * @param receiver Value passed to the method.
     *
     * @details The instance is initialized and prepared for immediate use.
     */
    SlotFunctionReceiver(T* receiver, std::function<void(Args...)> func)
        : receiver_(receiver), func_(std::move(func)) {}

    /**
     * @brief Performs the `invoke` operation.
     * @param args Value passed to the method.
     */
    void invoke(T*, Args... args) override {
        func_(args...);
    }

    /**
     * @brief Returns the current receiveur.
     * @return The current receiveur.
     *
     * @details The returned value reflects the state currently stored by the instance.
     */
    T* receiveur() override {
        return receiver_;
    }

private:
    T* receiver_;
    std::function<void(Args...)> func_;
};

// Slot fonctionnel (lambda, std::function)
template <typename... Args>
class SlotFunction : public ISlot<void, Args...> {
public:
    /**
     * @brief Constructs a `SlotFunction` instance.
     *
     * @details The instance is initialized and prepared for immediate use.
     */
    explicit SlotFunction(std::function<void(Args...)> func) : func(std::move(func)) {}

    /**
     * @brief Performs the `invoke` operation.
     * @param args Value passed to the method.
     */
    void invoke(void*, Args... args) override {
        func(args...);
    }

    /**
     * @brief Returns the current receiveur.
     * @return The current receiveur.
     *
     * @details The returned value reflects the state currently stored by the instance.
     */
    void* receiveur() override {
        return nullptr;
    }

private:
    std::function<void(Args...)> func;
};


// ============================================================================
//                          TRAITS DE FONCTION
// ============================================================================

// Extraction de la signature d'un callable (lambda, functor, fonction)
template <class F>
struct function_traits : function_traits<decltype(&F::operator())> {};

// Pour une fonction de la forme R(Args...)
template <class R, class... Args>
struct function_traits<R(Args...)> {
    using return_type = R;
    // On utilise std::decay_t pour éviter les références et const
    using args_tuple = std::tuple<swcore_compat::decay_t<Args>...>;
    using std_function_type = std::function<R(Args...)>;
};


template <class R, class... Args>
struct function_traits<R(*)(Args...)> : function_traits<R(Args...)> {};

template <class C, class R, class... Args>
struct function_traits<R(C::*)(Args...) const> : function_traits<R(Args...)> {};

template <class C, class R, class... Args>
struct function_traits<R(C::*)(Args...)> : function_traits<R(Args...)> {};


// ============================================================================
//               FONCTION AUXILIAIRE POUR DEPLIER UN TUPLE EN PACK
// ============================================================================

template<class Tuple, class F, std::size_t... I>
auto apply_tuple_impl(Tuple&&, F&& f, swcore_compat::index_sequence<I...>) -> decltype(f.template operator()<typename std::tuple_element<I, Tuple>::type...>()) {
    // On appelle f avec un pack de types déduit depuis Tuple
    // f est un foncteur template<...Args> ISlot<void, Args...>* operator()()
    return f.template operator()<typename std::tuple_element<I, Tuple>::type...>();
}

template<class Tuple, class F>
auto apply_tuple(Tuple&& t, F&& f) -> decltype(
    apply_tuple_impl(
        std::forward<Tuple>(t),
        std::forward<F>(f),
        swcore_compat::make_index_sequence<std::tuple_size<typename std::decay<Tuple>::type>::value>{}
    )
) {
    return apply_tuple_impl(
        std::forward<Tuple>(t),
        std::forward<F>(f),
        swcore_compat::make_index_sequence<std::tuple_size<typename std::decay<Tuple>::type>::value>{}
    );
}


// ============================================================================
//              FABRIQUE DE SLOT POUR LAMBDA/FONCTION GENERIQUE
// ============================================================================
template<class Func>
struct slot_factory {
    Func func;
    /**
     * @brief Constructs a `slot_factory` instance.
     *
     * @details The instance is initialized and prepared for immediate use.
     */
    slot_factory(Func&& f) : func(std::forward<Func>(f)) {}

    // Operator() template qui va être appelé par apply_tuple avec Args...
    template<typename... A>
    /**
     * @brief Performs the `operator` operation.
     * @return The requested operator.
     */
    ISlot<void, A...>* operator()() {
        using Fn = std::function<void(A...)>;
        Fn f(func); // Conversion implicite vers std::function
        return new SlotFunction<A...>(std::move(f));
    }
};

template<class ReceiverType, class Func>
struct slot_factory_with_receiver {
    ReceiverType* receiver;
    Func func;

    /**
     * @brief Constructs a `slot_factory_with_receiver` instance.
     * @param r Value passed to the method.
     *
     * @details The instance is initialized and prepared for immediate use.
     */
    slot_factory_with_receiver(ReceiverType* r, Func&& f) : receiver(r), func(std::forward<Func>(f)) {}

    template<typename... A>
    /**
     * @brief Performs the `operator` operation.
     * @return The requested operator.
     */
    ISlot<ReceiverType, A...>* operator()() {
        using Fn = std::function<void(A...)>;
        Fn f(func); // Conversion en std::function
        return new SlotFunctionReceiver<ReceiverType, A...>(receiver, std::move(f));
    }
};



class SwObject {
protected:
    friend bool swIsObjectLive(const SwObject* object);
    friend bool swDispatchEventToObject(SwObject* receiver, SwEvent* event);
    friend bool swDispatchEventFilter(SwObject* filter, SwObject* watched, SwEvent* event);
    friend bool swDispatchInstalledEventFilters(SwObject* watched, SwEvent* event);
    friend bool swForwardPostedEventToReceiverThread(SwObject* receiver, SwEvent* event, int priority);

    /**
     * @brief Returns whether the object reports same Thread Handle.
     * @param a Value passed to the method.
     * @param b Value passed to the method.
     * @return The requested same Thread Handle.
     *
     * @details This query does not modify the object state.
     */
    static bool isSameThreadHandle_(ThreadHandle* a, ThreadHandle* b) {
        if (a == b) return true;
        if (!a || !b) return false;
        const std::thread::id ida = a->threadId();
        const std::thread::id idb = b->threadId();
        if (ida == std::thread::id{} || idb == std::thread::id{}) return false;
        return ida == idb;
    }

protected:
    struct SignalKey {
        std::type_index typeIndex;
        std::string data;

        /**
         * @brief Constructs a `SignalKey` instance.
         *
         * @details The instance is initialized and prepared for immediate use.
         */
        SignalKey() : typeIndex(typeid(void)) {}
        /**
         * @brief Constructs a `SignalKey` instance.
         * @param idx Value passed to the method.
         *
         * @details The instance is initialized and prepared for immediate use.
         */
        SignalKey(std::type_index idx, std::string bytes)
            : typeIndex(idx), data(std::move(bytes)) {}

        /**
         * @brief Performs the `operator<` operation.
         * @param other Value passed to the method.
         * @return `true` on success; otherwise `false`.
         */
        bool operator<(const SignalKey& other) const {
            if (typeIndex != other.typeIndex) {
                return typeIndex < other.typeIndex;
            }
            return data < other.data;
        }
    };

    template <typename SignalPtr>
    /**
     * @brief Creates the requested signal Key.
     * @param signal Value passed to the method.
     * @return The resulting signal Key.
     */
    static SignalKey createSignalKey(SignalPtr signal) {
        static_assert(std::is_member_function_pointer<SignalPtr>::value, "Signal pointer attendu");
        return SignalKey(
            std::type_index(typeid(SignalPtr)),
            std::string(reinterpret_cast<const char*>(&signal), sizeof(SignalPtr))
        );
    }

    SwMap<SwString, void*> __nameToFunction__;
    /**
     * @brief Performs the `function<void` operation.
     * @return The requested function<void.
     */
    SwMap<SwString, std::function<void(void*)>> propertySetterMap;
    /**
     * @brief Returns the current function<void*.
     * @return The current function<void*.
     *
     * @details The returned value reflects the state currently stored by the instance.
     */
    SwMap<SwString, std::function<void*()>> propertyGetterMap;
    SwMap<SwString, SwString> propertyArgumentTypeNameMap;
    SwMap<SwString, SwString> propertyOwnerClassMap;

    // Dynamic properties for designer/tooling scenarios.
    SwMap<SwString, SwAny> dynamicPropertyMap;
    SwMap<SwString, SwString> dynamicPropertyTypeNameMap;
    SwList<SwObject*> m_eventFilters;

    PROPERTY(SwString, ObjectName, "")

public:

    /**
     * @brief Constructor that initializes the SwObject with an optional parent.
     *
     * Registers the `ObjectName` property and sets the parent of the current SwObject,
     * establishing its position in the SwObject hierarchy.
     *
     * @param parent Pointer to the parent Object. Defaults to nullptr if no parent is specified.
     */
    // Returns true if the given pointer refers to a live SwObject instance.
    // Safe to call with any pointer (even freed memory) — checks an external registry.
    /**
     * @brief Returns whether the object reports live.
     * @param ptr Value passed to the method.
     * @return The requested live.
     *
     * @details This query does not modify the object state.
     */
    static bool isLive(const void* ptr) {
        if (!ptr) return false;
        std::lock_guard<std::mutex> lk(s_liveObjectsMutex_());
        return s_liveObjects_().count(ptr) > 0;
    }

    /**
     * @brief Constructs a `SwObject` instance.
     * @param parent Optional parent object that owns this instance.
     *
     * @details The instance is initialized and can optionally be attached to a parent object for ownership management.
     */
    SwObject(SwObject* parent = nullptr) :
          /**
           * @brief Performs the `m_parent` operation.
           * @param nullptr Value passed to the method.
           */
          m_parent(nullptr)
    {
        { std::lock_guard<std::mutex> lk(s_liveObjectsMutex_()); s_liveObjects_().insert(this); }
        setParent(parent);
        ThreadHandle* currentThread = ThreadHandle::currentThread();
        if (!currentThread) {
            currentThread = ThreadHandle::sharedFallbackThread();
        }
        if (currentThread) {
            m_threadAffinity = currentThread;
            currentThread->attachObject(this);
        }
    }

    /**
     * @brief Virtual destructor for the SwObject class.
     *
     * Currently, this destructor does not perform any specific cleanup tasks.
     * It is designed to allow proper cleanup in derived classes, including disconnecting slots
     * and deleting child objects if necessary (commented out here for customization).
     */
    virtual ~SwObject() {
        { std::lock_guard<std::mutex> lk(s_liveObjectsMutex_()); s_liveObjects_().erase(this); }
        auto localChildren = m_children;
        m_children.clear();
        for (auto* child : localChildren) {
            if (!child) {
                continue;
            }
            if (child->m_parent == this) {
                child->m_parent = nullptr;
            }
            delete child;
        }

        if (m_parent) {
            SwObject* oldParent = m_parent;
            m_parent = nullptr;
            oldParent->removeChild(this);
        }

        if (m_threadAffinity) {
            m_threadAffinity->detachObject(this);
            m_threadAffinity = nullptr;
        }

        emit destroyed();
    }

    virtual bool event(SwEvent* event) {
        if (!event) {
            return false;
        }
        if (event->type() == EventType::ChildAdded || event->type() == EventType::ChildRemoved) {
            childEvent(static_cast<SwChildEvent*>(event));
            event->accept();
            return true;
        }
        if (event->type() == EventType::DeferredDelete) {
            delete this;
            return true;
        }
        return false;
    }

    virtual bool eventFilter(SwObject* watched, SwEvent* event) {
        SW_UNUSED(watched);
        SW_UNUSED(event);
        return false;
    }

    virtual void childEvent(SwChildEvent* event) {
        if (!event) {
            return;
        }
        switch (event->type()) {
        case EventType::ChildAdded:
            addChildEvent(event->child());
            break;
        case EventType::ChildRemoved:
            removedChildEvent(event->child());
            break;
        default:
            break;
        }
    }

    void installEventFilter(SwObject* filterObj) {
        if (!filterObj) {
            return;
        }
        m_eventFilters.removeAll(filterObj);
        m_eventFilters.push_back(filterObj);
    }

    void removeEventFilter(SwObject* filterObj) {
        if (!filterObj) {
            return;
        }
        m_eventFilters.removeAll(filterObj);
    }

    /**
     * @brief Checks if the given SwObject is derived from a specified base class.
     *
     * @tparam Base The base class to check against.
     * @tparam Derived The derived class type of the SwObject.
     * @param obj Pointer to the SwObject to check.
     * @return true if the SwObject is of type Base or derived from it, false otherwise.
     */
    template <typename Base, typename Derived>
    /**
     * @brief Performs the `inherits` operation.
     * @param obj Value passed to the method.
     * @return `true` on success; otherwise `false`.
     */
    bool inherits(const Derived* obj) {
        return dynamic_cast<const Base*>(obj) != nullptr;
    }

    /**
     * @brief Returns the current static Class Name.
     * @return The current static Class Name.
     *
     * @details The returned value reflects the state currently stored by the instance.
     */
    static SwString staticClassName() {
        static SwString kClassName = SwDemangleClassName(typeid(SwObject).name());
        return kClassName;
    }

    /**
     * @brief Returns the current class Name.
     * @return The current class Name.
     *
     * @details The returned value reflects the state currently stored by the instance.
     */
    virtual SwString className() const {
        return staticClassName();
    }

    /**
     * @brief Returns the internal thread hosting this object.
     */
    ThreadHandle* threadHandle() const {
        return m_threadAffinity;
    }

    /**
     * @brief Returns the current thread.
     * @return The current thread.
     *
     * @details The returned value reflects the state currently stored by the instance.
     */
    SwThread* thread() const;

    /**
     * @brief Moves this object to another thread.
     *
     * @param targetThread Destination thread. If nullptr, the current thread wrapper is used.
     */
    void moveToThread(ThreadHandle* targetThread) {
        if (!targetThread) {
            targetThread = ThreadHandle::currentThread();
            if (!targetThread) {
                targetThread = ThreadHandle::sharedFallbackThread();
            }
        }
        if (!targetThread || isSameThreadHandle_(targetThread, m_threadAffinity)) {
            return;
        }

        if (m_threadAffinity) {
            m_threadAffinity->detachObject(this);
        }

        m_threadAffinity = targetThread;
        m_threadAffinity->attachObject(this);
        for (auto* child : m_children) {
            if (child) child->moveToThread(targetThread);
        }
        onThreadChanged(targetThread);
    }

    /**
     * @brief Performs the `moveToThread` operation.
     * @param targetThread Value passed to the method.
     */
    void moveToThread(SwThread* targetThread);


    /**
     * @brief Retrieves the class name of the SwObject.
     *
     * This method uses `typeid` to get the full type name and extracts the class name.
     *
     * @return SwString The name of the class.
     */
    virtual SwList<SwString> classHierarchy() const {
        return { staticClassName() };
    }

    /**
     * @brief Marks the SwObject for deletion in the next event loop iteration.
     *
     * This method schedules the deletion of the SwObject using `SwCoreApplication::postEvent`.
     * The actual deletion occurs asynchronously, ensuring that the SwObject is safely
     * removed without disrupting the current execution flow.
     */
    void deleteLater() {
        SwObject* meAsDurtyToClean = this;
        ThreadHandle* targetThread = this->threadHandle();
        if (!targetThread) {
            targetThread = ThreadHandle::currentThread();
            if (!targetThread) {
                targetThread = ThreadHandle::sharedFallbackThread();
            }
        }
        if (targetThread) {
            targetThread->postTask([meAsDurtyToClean]() {
                if (!SwObject::isLive(meAsDurtyToClean)) {
                    return;
                }
                if (SwCoreApplication* app = SwCoreApplication::instance(false)) {
                    app->postEvent(meAsDurtyToClean, new SwDeferredDeleteEvent());
                } else {
                    delete meAsDurtyToClean;
                }
            });
            return;
        }

        if (SwCoreApplication* app = SwCoreApplication::instance(false)) {
            app->postEvent(meAsDurtyToClean, new SwDeferredDeleteEvent());
            return;
        }

        delete meAsDurtyToClean;
    }

    /**
     * @brief Safely deletes a pointer and sets it to nullptr.
     *
     * @tparam T The type of the pointer to delete.
     * @param _refPtr A reference to the pointer to be deleted. After deletion, it will be set to nullptr.
     */
    template <typename T>
    /**
     * @brief Performs the `safeDelete` operation.
     * @param _refPtr Value passed to the method.
     * @return The requested safe Delete.
     */
    static void safeDelete(T*& _refPtr) {
        if (_refPtr) {
            delete _refPtr;
            _refPtr = nullptr;
        }
    }

    /**
     * @brief Sets a new parent for the SwObject and updates the parent-child relationship.
     *
     * If the SwObject already has a parent, it removes itself from the previous parent's children list.
     * It then adds itself to the new parent's children list and triggers the `newParentEvent` to handle
     * any custom behavior related to the parent change.
     *
     * @param parent The new parent SwObject. Can be nullptr to detach the SwObject from its current parent.
     */
    void setParent(SwObject *parent)
    {
        if (m_parent) {
            m_parent->removeChild(this);
        }
        m_parent = parent;
        if (m_parent) {
            m_parent->addChild(this);
        }
        newParentEvent(parent);
    }

    /**
     * @brief Retrieves the parent of the current SwObject.
     *
     * @return A pointer to the parent SwObject, or nullptr if the SwObject has no parent.
     */
    SwObject *parent() { return m_parent; }

    /**
     * @brief Returns the current *parent.
     * @return The current *parent.
     *
     * @details The returned value reflects the state currently stored by the instance.
     */
    SwObject *parent() const { return m_parent; }

    /**
     * @brief Adds a child SwObject to the current SwObject.
     *
     * This method appends the specified child to the list of children, emits a `childAdded` signal,
     * and triggers the `addChildEvent` to allow derived classes to handle additional logic.
     *
     * @param child The child SwObject to add.
     */
    virtual void addChild(SwObject* child) {
        m_children.push_back(child);
        emit childAdded(child);
        SwChildEvent event(EventType::ChildAdded, child);
        SwCoreApplication::sendEvent(this, &event);
    }

    /**
     * @brief Removes a child SwObject from the current SwObject's children list.
     *
     * This method removes the specified child from the list of children, emits a `childRemoved` signal,
     * and triggers the `removedChildEvent` to allow derived classes to handle additional logic.
     *
     * @param child The child SwObject to remove.
     */
    virtual void removeChild(SwObject* child) {
        m_children.removeAll(child);
        emit childRemoved(child);
        SwChildEvent event(EventType::ChildRemoved, child);
        SwCoreApplication::sendEvent(this, &event);
    }

    /**
     * @brief Finds all child objects of a specific type, including nested children.
     *
     * This method searches for children of the current SwObject that are of the specified type `T`.
     * The search is recursive, meaning it also checks the children of each child SwObject.
     *
     * @tparam T The type of objects to find.
     * @return A vector of pointers to all child objects of type `T`.
     */
    template <typename T>
    /**
     * @brief Returns the current find Children.
     * @return The current find Children.
     *
     * @details The returned value reflects the state currently stored by the instance.
     */
    SwList<T*> findChildren() const {
        SwList<T*> result;
        for (auto child : m_children) {
            // Utiliser dynamic_cast pour vérifier si l'enfant est du type T
            if (T* castedChild = dynamic_cast<T*>(child)) {
                result.push_back(castedChild);
            }

            // Si l'enfant est un Object, on effectue la recherche récursive
            if (auto objectChild = dynamic_cast<SwObject*>(child)) {
                // Recherche récursive dans les enfants de l'enfant
                SwList<T*> nestedResults = objectChild->findChildren<T>();
                result.append(nestedResults);
            }
        }
        return result;
    }

    /**
     * @brief Retrieves all direct child objects of the current SwObject.
     *
     * This method returns a constant reference to the list containing the direct children
     * of the current SwObject. The list does not include nested children.
     *
     * @return A constant reference to the list of child objects.
     */
    const SwList<SwObject*>& children() const {
        return m_children;
    }

    /**
     * @brief Finds the first child object of a specific type, optionally filtered by name.
     *
     * Searches recursively through children. Returns nullptr if not found.
     *
     * @tparam T The type of object to find.
     * @param name Optional object name to match (empty = any name).
     * @return Pointer to the first matching child, or nullptr.
     */
    template <typename T>
    /**
     * @brief Performs the `findChild` operation.
     * @param name Value passed to the method.
     * @return The requested find Child.
     */
    T* findChild(const SwString& name = SwString()) const {
        for (auto* child : m_children) {
            if (T* casted = dynamic_cast<T*>(child)) {
                if (name.isEmpty() || casted->getObjectName() == name) {
                    return casted;
                }
            }
            if (auto* objChild = dynamic_cast<SwObject*>(child)) {
                if (T* found = objChild->findChild<T>(name)) {
                    return found;
                }
            }
        }
        return nullptr;
    }

    /**
     * @brief Handles the event triggered when a child SwObject is added.
     *
     * This virtual method is called whenever a child SwObject is added to the current SwObject.
     * It can be overridden in derived classes to perform specific actions when a child is added.
     *
     * @param child Pointer to the SwObject that was added as a child.
     */
    virtual void addChildEvent(SwObject* child) { SW_UNUSED(child) }

    /**
     * @brief Handles the event triggered when a child SwObject is removed.
     *
     * This virtual method is called whenever a child SwObject is removed from the current SwObject.
     * It can be overridden in derived classes to perform specific actions when a child is removed.
     *
     * @param child Pointer to the SwObject that was removed as a child.
     */
    virtual void removedChildEvent(SwObject* child) { SW_UNUSED(child)  }

    /**
     * @brief Connects a signal from a sender SwObject to a slot in a receiver SwObject.
     *
     * Establishes a connection between a signal emitted by the sender and a member function
     * (slot) of the receiver. The connection type determines how the signal is handled.
     *
     * @param sender Pointer to the sender SwObject emitting the signal.
     * @param signalName Name of the signal to connect.
     * @param receiver Pointer to the receiver SwObject receiving the signal.
     * @param slot Pointer to the receiver's member function (slot).
     * @param type Type of connection (e.g., DirectConnection, QueuedConnection, BlockingQueuedConnection). Default is DirectConnection.
     */
    template<typename Sender, typename Receiver, typename... Args>
    /**
     * @brief Performs the `connect` operation.
     * @param sender Value passed to the method.
     * @param signalName Value passed to the method.
     * @param receiver Value passed to the method.
     * @param type Value passed to the method.
     * @return The requested connect.
     */
    static void connect(Sender* sender, const SwString& signalName, Receiver* receiver, void (Receiver::* slot)(Args...), ConnectionType type = AutoConnection) {
        static_assert(std::is_base_of<SwObject, Sender>::value, "Sender must derive from SwObject");
        ISlot<Receiver, Args...>* newSlot = new SlotMember<Receiver, Args...>(receiver, slot);
        static_cast<SwObject*>(sender)->addConnection(signalName, newSlot, type);
    }


    /**
     * @brief Connects a signal from a sender SwObject to a standalone function or lambda.
     *
     * Establishes a connection between a signal emitted by the sender and a function or lambda.
     * The connection type determines how the signal is handled.
     *
     * @param sender Pointer to the sender SwObject emitting the signal.
     * @param signalName Name of the signal to connect.
     * @param func The function or lambda to be executed when the signal is emitted.
     * @param type Type of connection (e.g., DirectConnection, QueuedConnection, BlockingQueuedConnection). Default is DirectConnection.
     */
    template<typename Sender, typename... Args>
    /**
     * @brief Performs the `connect` operation.
     * @param sender Value passed to the method.
     * @param signalName Value passed to the method.
     * @param func Value passed to the method.
     * @param type Value passed to the method.
     * @return The requested connect.
     */
    static void connect(Sender* sender, const SwString& signalName, std::function<void(Args...)> func, ConnectionType type = AutoConnection) {
        static_assert(std::is_base_of<SwObject, Sender>::value, "Sender must derive from SwObject");
        ISlot<void, Args...>* newSlot = new SlotFunction<Args...>(func);
        static_cast<SwObject*>(sender)->addConnection(signalName, newSlot, type);
    }

    /**
     * @brief Connects a signal to a lambda function.
     *
     * Establishes a connection between a signal emitted by the sender and a lambda function.
     *
     * @tparam Sender The type of the sender SwObject emitting the signal.
     * @tparam Func The type of the lambda function to connect.
     * @param sender Pointer to the sender SwObject emitting the signal.
     * @param signalName The name of the signal to connect.
     * @param func The lambda to execute when the signal is emitted.
     * @param type Type of connection (e.g., DirectConnection, QueuedConnection, BlockingQueuedConnection).
     *             Default is DirectConnection.
     */
    template <typename SenderType, typename Func>
    /**
     * @brief Performs the `connect` operation.
     * @param sender Value passed to the method.
     * @param signalName Value passed to the method.
     * @param func Value passed to the method.
     * @param type Value passed to the method.
     * @return The requested connect.
     */
    static void connect(SenderType* sender, const SwString& signalName, Func&& func, ConnectionType type = AutoConnection) {
        static_assert(std::is_base_of<SwObject, SenderType>::value, "Sender must derive from SwObject");
        using traits = function_traits<typename std::decay<Func>::type>;
        using R = typename traits::return_type;
        using args_tuple = typename traits::args_tuple;

        static_assert(std::is_void<R>::value, "Seules les fonctions retournant void sont supportées.");

        // On utilise apply_tuple pour déplier le tuple d'arguments (Args...) de la fonction
        auto slot = apply_tuple(args_tuple{}, slot_factory<Func>(std::forward<Func>(func)));

        static_cast<SwObject*>(sender)->addConnection(signalName, slot, type);
    }

    template <typename SenderType, typename ReceiverType, typename Func>
    /**
     * @brief Performs the `connect` operation.
     * @param sender Value passed to the method.
     * @param signalName Value passed to the method.
     * @param receiver Value passed to the method.
     * @param func Value passed to the method.
     * @param type Value passed to the method.
     * @return The requested connect.
     */
    static void connect(SenderType* sender, const SwString& signalName, ReceiverType* receiver, Func&& func, ConnectionType type = AutoConnection) {
        static_assert(std::is_base_of<SwObject, SenderType>::value, "Sender must derive from SwObject");
        using traits = function_traits<typename std::decay<Func>::type>;
        using R = typename traits::return_type;
        using args_tuple = typename traits::args_tuple;

        static_assert(std::is_void<R>::value, "Seules les fonctions retournant void sont supportées.");

        // On utilise apply_tuple pour déplier les arguments de la lambda
        auto slot = apply_tuple(args_tuple{}, slot_factory_with_receiver<ReceiverType, Func>(receiver, std::forward<Func>(func)));

        static_cast<SwObject*>(sender)->addConnection(signalName, slot, type);
    }

    template <typename SenderType, typename ReceiverType, typename Func>
    /**
     * @brief Performs the `connect` operation.
     * @param sender Value passed to the method.
     * @param receiver Value passed to the method.
     * @param func Value passed to the method.
     * @param type Value passed to the method.
     * @return The requested connect.
     */
    static void connect(SenderType* sender, const SwString& (*signalAccessor)(), ReceiverType* receiver, Func&& func, ConnectionType type = AutoConnection) {
        connect(sender, signalAccessor(), receiver, std::forward<Func>(func), type);
    }

    /**
     * @brief Connects a signal to a slot using modern pointer-to-member function syntax.
     *
     * This method aims to establish a connection between a signal and a slot in a type-safe way.
     * It uses pointer-to-member functions to explicitly define the signal and slot connections.
     * Note: This implementation is under development and may not work as expected in all cases.
     *
     * @tparam Sender The type of the sender SwObject emitting the signal.
     * @tparam Receiver The type of the receiver SwObject handling the signal.
     * @tparam SignalArgs The argument types of the signal.
     * @tparam SlotArgs The argument types of the slot.
     * @param sender Pointer to the sender SwObject emitting the signal.
     * @param signal The pointer-to-member function representing the signal.
     * @param receiver Pointer to the receiver SwObject handling the signal.
     * @param slot The pointer-to-member function representing the slot.
     * @param type Type of connection (e.g., DirectConnection, QueuedConnection, BlockingQueuedConnection).
     *             Default is DirectConnection.
     *
     * @note This function is designed for modern signal-slot connections but requires further refinement to handle
     *       cases where Sender or Receiver are derived classes or when argument types do not match exactly.
     */
    template<typename Sender, typename SignalOwner, typename Receiver, typename... SignalArgs, typename... SlotArgs>
    /**
     * @brief Performs the `connect` operation.
     * @param sender Value passed to the method.
     * @param receiver Value passed to the method.
     * @param type Value passed to the method.
     * @return The requested connect.
     */
    static void connect(
        Sender* sender,
        void (SignalOwner::*signal)(SignalArgs...),
        Receiver* receiver,
        void (Receiver::*slot)(SlotArgs...),
        ConnectionType type = AutoConnection
        ) {
        static_assert(std::is_base_of<SignalOwner, Sender>::value, "Sender must derive from SignalOwner");
        static_assert(sizeof...(SignalArgs) == sizeof...(SlotArgs), "Signal and slot must share the same arity.");

        using Fn = std::function<void(swcore_compat::decay_t<SignalArgs>...)>;
        Fn wrapper = [receiver, slot](swcore_compat::decay_t<SignalArgs>... args) {
            (receiver->*slot)(args...);
        };

        ISlot<Receiver, swcore_compat::decay_t<SignalArgs>...>* newSlot =
            new SlotFunctionReceiver<Receiver, swcore_compat::decay_t<SignalArgs>...>(receiver, std::move(wrapper));
        static_cast<SwObject*>(sender)->addConnection(createSignalKey(signal), newSlot, type);
    }

    template<typename Sender, typename SignalOwner, typename... SignalArgs>
    /**
     * @brief Performs the `connect` operation.
     * @param sender Value passed to the method.
     * @param func Value passed to the method.
     * @param type Value passed to the method.
     * @return The requested connect.
     */
    static void connect(
        Sender* sender,
        void (SignalOwner::*signal)(SignalArgs...),
        std::function<void(SignalArgs...)> func,
        ConnectionType type = AutoConnection
        ) {
        static_assert(std::is_base_of<SignalOwner, Sender>::value, "Sender must derive from SignalOwner");

        using Fn = std::function<void(swcore_compat::decay_t<SignalArgs>...)>;
        Fn movedFunc = std::move(func);
        Fn wrapper = [movedFunc](swcore_compat::decay_t<SignalArgs>... args) mutable {
            movedFunc(args...);
        };

        ISlot<void, swcore_compat::decay_t<SignalArgs>...>* newSlot =
            new SlotFunction<swcore_compat::decay_t<SignalArgs>...>(std::move(wrapper));
        static_cast<SwObject*>(sender)->addConnection(createSignalKey(signal), newSlot, type);
    }

    template<typename Sender, typename SignalOwner, typename Func, typename... SignalArgs>
    /**
     * @brief Performs the `connect` operation.
     * @param sender Value passed to the method.
     * @param func Value passed to the method.
     * @param type Value passed to the method.
     * @return The requested connect.
     */
    static void connect(
        Sender* sender,
        void (SignalOwner::*signal)(SignalArgs...),
        Func&& func,
        ConnectionType type = AutoConnection
        ) {
        static_assert(std::is_base_of<SignalOwner, Sender>::value, "Sender must derive from SignalOwner");

        using Fn = std::function<void(swcore_compat::decay_t<SignalArgs>...)>;
        ISlot<void, swcore_compat::decay_t<SignalArgs>...>* newSlot =
            new SlotFunction<swcore_compat::decay_t<SignalArgs>...>(Fn(std::forward<Func>(func)));
        static_cast<SwObject*>(sender)->addConnection(createSignalKey(signal), newSlot, type);
    }

    template<typename Sender, typename SignalOwner, typename Receiver, typename Func, typename... SignalArgs>
    /**
     * @brief Performs the `connect` operation.
     * @param sender Value passed to the method.
     * @param receiver Value passed to the method.
     * @param func Value passed to the method.
     * @param type Value passed to the method.
     * @return The requested connect.
     */
    static void connect(
        Sender* sender,
        void (SignalOwner::*signal)(SignalArgs...),
        Receiver* receiver,
        Func&& func,
        ConnectionType type = AutoConnection
        ) {
        static_assert(std::is_base_of<SignalOwner, Sender>::value, "Sender must derive from SignalOwner");

        using Fn = std::function<void(swcore_compat::decay_t<SignalArgs>...)>;
        ISlot<Receiver, swcore_compat::decay_t<SignalArgs>...>* newSlot =
            new SlotFunctionReceiver<Receiver, swcore_compat::decay_t<SignalArgs>...>(receiver, Fn(std::forward<Func>(func)));
        static_cast<SwObject*>(sender)->addConnection(createSignalKey(signal), newSlot, type);
    }

    /**
     * @brief Disconnects a specific slot from a signal of a sender SwObject.
     *
     * Removes the connection between a signal emitted by the sender and a specific slot of the receiver.
     * If no slots remain connected to the signal, the signal is removed from the sender's connections.
     *
     * @param sender Pointer to the sender SwObject emitting the signal.
     * @param signalName Name of the signal to disconnect from.
     * @param receiver Pointer to the receiver SwObject whose slot is being disconnected.
     * @param slot The specific slot of the receiver to disconnect.
     */
    template<typename Sender, typename Receiver>
    /**
     * @brief Performs the `disconnect` operation.
     * @param sender Value passed to the method.
     * @param signalName Value passed to the method.
     * @param receiver Value passed to the method.
     * @return The requested disconnect.
     */
    static void disconnect(Sender* sender, const SwString& signalName, Receiver* receiver, void (Receiver::*slot)()) {
        // Vérifie si le signal existe
        if (sender->connections.find(signalName) != sender->connections.end()) {
            auto& slotsConnetion = sender->connections[signalName];
            slotsConnetion.erase(
                std::remove_if(slotsConnetion.begin(), slotsConnetion.end(),
                    [receiver, slot](const std::pair<void*, ConnectionType>& connection) {
                        // Vérifie si le slot correspond
                        auto* baseSlot = static_cast<ISlot<Receiver>*>(connection.first);
                        auto* memberSlot = dynamic_cast<SlotMember<Receiver>*>(baseSlot);
                        return memberSlot && memberSlot->receiveur() == receiver && memberSlot->methodPtr() == slot;
                    }),
                slotsConnetion.end()
            );

            // Si plus de slots, supprime l'entrée pour le signal
            if (slotsConnetion.empty()) {
                sender->connections.remove(signalName);
            }
        }
    }

    template<typename Sender, typename Receiver, typename... SignalArgs>
    /**
     * @brief Performs the `disconnect` operation.
     * @param sender Value passed to the method.
     * @param receiver Value passed to the method.
     * @return The requested disconnect.
     */
    static void disconnect(Sender* sender, void (Sender::*signal)(SignalArgs...), Receiver* receiver, void (Receiver::*slot)(SignalArgs...)) {
        auto key = createSignalKey(signal);
        auto it = sender->typedConnections.find(key);
        if (it == sender->typedConnections.end()) {
            return;
        }

        auto& slotsConnetion = it->second;
        slotsConnetion.erase(
            std::remove_if(slotsConnetion.begin(), slotsConnetion.end(),
                [receiver, slot](const std::pair<void*, ConnectionType>& connection) {
                    auto* baseSlot = static_cast<ISlot<Receiver, SignalArgs...>*>(connection.first);
                    auto* memberSlot = dynamic_cast<SlotMember<Receiver, SignalArgs...>*>(baseSlot);
                    return memberSlot && memberSlot->receiveur() == receiver && memberSlot->methodPtr() == slot;
                }),
            slotsConnetion.end()
        );

        if (slotsConnetion.empty()) {
            sender->typedConnections.erase(it);
        }
    }

    /**
     * @brief Disconnects all slots of a receiver from all signals of a sender SwObject.
     *
     * Iterates through all signals of the sender and removes any connections associated with the receiver.
     * If no slots remain connected to a signal, the signal is removed from the sender's connections.
     *
     * @param sender Pointer to the sender SwObject emitting the signals.
     * @param receiver Pointer to the receiver SwObject whose slots are being disconnected.
     */
    template<typename Sender, typename Receiver>
    /**
     * @brief Performs the `disconnect` operation.
     * @param sender Value passed to the method.
     * @param receiver Value passed to the method.
     * @return The requested disconnect.
     */
    static void disconnect(Sender* sender, Receiver* receiver) {
        // Parcourt tous les signaux et déconnecte ceux associés au receiver
        for (auto it = sender->connections.begin(); it != sender->connections.end(); ) {
            auto& slotsConnetion = it->second;
            slotsConnetion.erase(
                std::remove_if(slotsConnetion.begin(), slotsConnetion.end(),
                    [receiver](const std::pair<void*, ConnectionType>& connection) {
                        // Vérifie si le slot correspond
                        auto* memberSlot = dynamic_cast<SlotMember<Receiver>*>(static_cast<ISlot<Receiver>*>(connection.first));
                        return memberSlot && memberSlot->receiveur() == receiver;
                    }),
                slotsConnetion.end()
            );

            // Si plus de slots pour ce signal, supprime l'entrée
            if (slotsConnetion.empty()) {
                it = sender->connections.erase(it);
            } else {
                ++it;
            }
        }
        for (auto it = sender->typedConnections.begin(); it != sender->typedConnections.end(); ) {
            auto& slotsConnetion = it->second;
            slotsConnetion.erase(
                std::remove_if(slotsConnetion.begin(), slotsConnetion.end(),
                    [receiver](const std::pair<void*, ConnectionType>& connection) {
                        auto* memberSlot = dynamic_cast<SlotMember<Receiver>*>(static_cast<ISlot<Receiver>*>(connection.first));
                        return memberSlot && memberSlot->receiveur() == receiver;
                    }),
                slotsConnetion.end()
            );

            if (slotsConnetion.empty()) {
                it = sender->typedConnections.erase(it);
            } else {
                ++it;
            }
        }
    }

    /**
     * @brief Adds a new connection for a signal.
     *
     * Associates a given slot with a signal name and connection type. If the signal does not already exist,
     * it creates a new entry for it.
     *
     * @tparam Args Variadic template parameters representing the types of the arguments for the slot.
     * @param signalName The name of the signal to connect.
     * @param slot Pointer to the slot to be associated with the signal.
     * @param type The type of connection (e.g., DirectConnection, QueuedConnection).
     */
    template<typename... Args>
    /**
     * @brief Adds the specified connection.
     * @param signalName Value passed to the method.
     * @param slot Value passed to the method.
     * @param type Value passed to the method.
     */
    void addConnection(const SwString& signalName, ISlot<Args...>* slot, ConnectionType type) {
        if (connections.find(signalName) == connections.end()) {
            connections[signalName] = std::vector<std::pair<void*, ConnectionType>>();
        }
        connections[signalName].push_back(std::make_pair(static_cast<void*>(slot), type));
    }

    template<typename... Args>
    /**
     * @brief Adds the specified connection.
     * @param key Value passed to the method.
     * @param slot Value passed to the method.
     * @param type Value passed to the method.
     */
    void addConnection(const SignalKey& key, ISlot<Args...>* slot, ConnectionType type) {
        typedConnections[key].push_back(std::make_pair(static_cast<void*>(slot), type));
    }

    /**
     * @brief Retrieves the current sender of the signal.
     *
     * Returns the pointer to the `SwObject` that emitted the signal. Useful in slot functions
     * to determine which SwObject triggered the signal.
     *
     * @return Object* Pointer to the sender SwObject, or `nullptr` if no sender is set.
     */
    SwObject* sender() {
        return currentSender;
    }

    /**
     * @brief Sets the current sender of the signal.
     *
     * This function assigns the sender SwObject for the currently emitted signal.
     * It is used internally when emitting signals to track the originating SwObject.
     *
     * @param _sender Pointer to the `SwObject` that emits the signal.
     */
    void setSender(SwObject* _sender) {
        currentSender = _sender;
    }

    /**
     * @brief Disconnects all slots connected to this SwObject.
     *
     * This function clears all signal-slot connections associated with this SwObject.
     * It ensures that no slots remain connected to any of its signals.
     *
     * If any connections exist, they are removed, and a message is logged.
     */
    void disconnectAllSlots() {
        if (!connections.empty() || !typedConnections.empty()) {
            connections.clear();
            typedConnections.clear();
            swCDebug(kSwLogCategory_SwObject) << "Tous les slots ont été déconnectés pour cet objet.";
        }
    }

    /**
     * @brief Disconnects all slots associated with a specific receiver.
     *
     * This function iterates through all connections of this SwObject and removes any
     * slots that are associated with the provided receiver SwObject.
     *
     * @tparam Receiver The type of the receiver SwObject.
     * @param receiver A pointer to the receiver SwObject whose slots need to be disconnected.
     *
     * Logs a message indicating that all slots linked to the receiver have been disconnected.
     */
    template <typename Receiver>
    /**
     * @brief Performs the `disconnectReceiver` operation.
     * @param receiver Value passed to the method.
     */
    void disconnectReceiver(Receiver* receiver) {
        for (auto it = connections.begin(); it != connections.end(); ++it) {
            std::vector<std::pair<void*, ConnectionType>>& currentSlots = it->second;
            currentSlots.erase(std::remove_if(currentSlots.begin(), currentSlots.end(),
                [receiver](std::pair<void*, ConnectionType>& slotPair) {
                    SlotMember<Receiver>* slot = static_cast<SlotMember<Receiver>*>(slotPair.first);
                    return slot && slot->receiveur() == receiver;
                }),
                currentSlots.end());
        }
        for (auto it = typedConnections.begin(); it != typedConnections.end(); ++it) {
            std::vector<std::pair<void*, ConnectionType>>& currentSlots = it->second;
            currentSlots.erase(std::remove_if(currentSlots.begin(), currentSlots.end(),
                [receiver](std::pair<void*, ConnectionType>& slotPair) {
                    SlotMember<Receiver>* slot = static_cast<SlotMember<Receiver>*>(slotPair.first);
                    return slot && slot->receiveur() == receiver;
                }),
                currentSlots.end());
        }
        swCDebug(kSwLogCategory_SwObject) << "Tous les slots liés au receiver ont été déconnectés.";
    }

    /**
     * @brief Sets the value of a specified property.
     *
     * This function assigns a new value to a property identified by its name. If the property
     * is not initialized, it triggers the registration of all properties. It verifies if the
     * property exists and if the provided value matches the expected type before setting it.
     *
     * @param propertyName The name of the property to be updated.
     * @param value The new value to assign to the property, encapsulated in a SwAny SwObject.
     *
     * @note Logs a message if the property is not found or if the value type does not match the expected type.
     */
    void setProperty(const SwString& propertyName, SwAny value) {
        // Vérifier si la propriété existe dans la map
        if (propertySetterMap.find(propertyName) != propertySetterMap.end()) {
            // Appel du setter via SwAny
            if (propertyArgumentTypeNameMap[propertyName] == value.typeName()) {
                propertySetterMap[propertyName](value.data());
            } else if(value.canConvert(propertyArgumentTypeNameMap[propertyName])){
                swCDebug(kSwLogCategory_SwObject) << "from converted value";
                propertySetterMap[propertyName](value.convert(propertyArgumentTypeNameMap[propertyName]).data());
            } else {
                swCError(kSwLogCategory_SwObject) << "Whoa, hold on! The property you're trying to set doesn't match the expected type: "
                          << propertyArgumentTypeNameMap[propertyName]
                          << ". Received: " << value.typeName()
                          << ". You need to explicitly cast your value to the correct type.";
            }
        }
        else {
            // Unknown property -> treat as a dynamic property (designer/tooling use-case).
            auto it = dynamicPropertyMap.find(propertyName);
            if (it != dynamicPropertyMap.end()) {
                const SwString expectedTypeName = dynamicPropertyTypeNameMap[propertyName];
                if (expectedTypeName == value.typeName()) {
                    it->second = value;
                    return;
                }
                if (!expectedTypeName.isEmpty() && value.canConvert(expectedTypeName)) {
                    it->second = value.convert(expectedTypeName);
                    return;
                }
                it->second = value;
                dynamicPropertyTypeNameMap[propertyName] = SwString(value.typeName());
                return;
            }

            dynamicPropertyMap[propertyName] = value;
            dynamicPropertyTypeNameMap[propertyName] = SwString(value.typeName());
        }
    }

    /**
     * @brief Retrieves the value of a specified property.
     *
     * This function accesses a property by its name and returns its value encapsulated in a SwAny SwObject.
     * If the properties have not been initialized, it triggers their registration.
     * If the property exists, its getter function is called to obtain the value,
     * which is then converted to a SwAny SwObject.
     *
     * @param propertyName The name of the property to retrieve.
     * @return SwAny The value of the property if found, or an empty SwAny SwObject if the property does not exist.
     *
     * @note Logs a message if the property is not found.
     */
    SwAny property(const SwString& propertyName) {
        SwAny retValue;
        // Vérifier si la propriété existe dans la map
        auto it = propertyGetterMap.find(propertyName);
        if (it != propertyGetterMap.end()) {
            // Appeler la fonction getter pour récupérer la valeur
            void* valuePtr = it->second();  // Appelle la lambda pour obtenir un pointeur générique
            const SwString& typeName = propertyArgumentTypeNameMap[propertyName];  // Obtenir le nom du type

            // Créer un SwAny à partir du pointeur et du type
            retValue = SwAny::fromVoidPtr(valuePtr, typeName);
        }
        else {
            auto dyn = dynamicPropertyMap.find(propertyName);
            if (dyn != dynamicPropertyMap.end()) {
                return dyn->second;
            }
            swCWarning(kSwLogCategory_SwObject) << "Property not found: " << propertyName;
        }

        return retValue;
    }

    /**
     * @brief Checks whether a specified property exists.
     *
     * This function verifies the existence of a property by its name.
     * If the properties have not been initialized, it triggers their registration.
     *
     * @param propertyName The name of the property to check.
     * @return bool `true` if the property exists, otherwise `false`.
     */
    bool propertyExist(const SwString& propertyName) {
        auto it = propertyGetterMap.find(propertyName);
        if (it != propertyGetterMap.end()) {
            return true;
        }
        return dynamicPropertyMap.find(propertyName) != dynamicPropertyMap.end();
    }

    /**
     * @brief Returns the list of registered properties for this object.
     *
     * Properties are registered by the PROPERTY/CUSTOM_PROPERTY macros and stored in the internal
     * getter/setter maps. This is useful for tooling (UI designers, inspectors, serialization).
     */
    SwStringList propertyNames() const {
        SwStringList names;
        names.reserve(propertyGetterMap.size() + dynamicPropertyMap.size());
        for (const auto& it : propertyGetterMap) {
            names.append(it.first);
        }
        for (const auto& it : dynamicPropertyMap) {
            if (!propertyGetterMap.count(it.first)) {
                names.append(it.first);
            }
        }
        return names;
    }

    /**
     * @brief Returns the registered C++ type name for a property (typeid name string).
     */
    SwString propertyTypeName(const SwString& propertyName) const {
        auto it = propertyArgumentTypeNameMap.find(propertyName);
        if (it != propertyArgumentTypeNameMap.end()) {
            return it->second;
        }
        auto dyn = dynamicPropertyTypeNameMap.find(propertyName);
        return dyn == dynamicPropertyTypeNameMap.end() ? SwString() : dyn->second;
    }

    /**
     * @brief Returns the class name that originally registered a property (base grouping).
     *
     * For dynamic properties, returns "Dynamic".
     */
    SwString propertyOwnerClass(const SwString& propertyName) const {
        auto it = propertyOwnerClassMap.find(propertyName);
        if (it != propertyOwnerClassMap.end()) {
            return it->second;
        }
        if (dynamicPropertyMap.find(propertyName) != dynamicPropertyMap.end()) {
            return SwString("Dynamic");
        }
        return SwString();
    }

    /**
     * @brief Returns whether the object reports dynamic Property.
     * @param propertyName Value passed to the method.
     * @return `true` when the object reports dynamic Property; otherwise `false`.
     *
     * @details This query does not modify the object state.
     */
    bool isDynamicProperty(const SwString& propertyName) const {
        return dynamicPropertyMap.find(propertyName) != dynamicPropertyMap.end();
    }

    /**
     * @brief Sets the dynamic Property.
     * @param propertyName Value passed to the method.
     * @param value Value passed to the method.
     *
     * @details Call this method to replace the currently stored value with the caller-provided one.
     */
    void setDynamicProperty(const SwString& propertyName, SwAny value) {
        if (propertySetterMap.find(propertyName) != propertySetterMap.end()) {
            setProperty(propertyName, value);
            return;
        }
        dynamicPropertyMap[propertyName] = value;
        dynamicPropertyTypeNameMap[propertyName] = SwString(value.typeName());
    }

    /**
     * @brief Removes the specified dynamic Property.
     * @param propertyName Value passed to the method.
     * @return `true` on success; otherwise `false`.
     */
    bool removeDynamicProperty(const SwString& propertyName) {
        auto it = dynamicPropertyMap.find(propertyName);
        if (it == dynamicPropertyMap.end()) {
            return false;
        }
        dynamicPropertyMap.erase(it);
        dynamicPropertyTypeNameMap.remove(propertyName);
        return true;
    }

    /**
     * @brief Returns the current dynamic Property Names.
     * @return The current dynamic Property Names.
     *
     * @details The returned value reflects the state currently stored by the instance.
     */
    SwStringList dynamicPropertyNames() const {
        SwStringList names;
        names.reserve(dynamicPropertyMap.size());
        for (const auto& it : dynamicPropertyMap) {
            names.append(it.first);
        }
        return names;
    }

protected:

    /**
     * @brief Handles events triggered when a new parent is assigned.
     *
     * This function is called whenever the SwObject is assigned a new parent.
     * It can be overridden to implement custom behavior for handling parent changes.
     *
     * @param parent The new parent SwObject assigned to this SwObject.
     */
    virtual void newParentEvent(SwObject* parent) { SW_UNUSED(parent) }

    /**
     * @brief Called when the object changes threads.
     */
    virtual void onThreadChanged(ThreadHandle* thread) { SW_UNUSED(thread) }


    /**
     * @brief Attempts to extract the name of a signal from a pointer-to-member function.
     *
     * This function uses `typeid` to deduce the type information of the signal. It aims to
     * return the signal's name as a string. However, this implementation does not provide
     * a reliable or accurate signal name in its current state.
     *
     * @tparam Signal The type of the signal pointer.
     * @param signal The pointer-to-member function representing the signal.
     * @return SwString The deduced signal name (currently unreliable).
     *
     * @note This implementation is incomplete. The returned name is derived from
     *       `typeid`, which includes mangled type information and does not
     *       correspond to the actual signal name. Further refinement is needed to
     *       handle demangling or mapping to human-readable names.
     */
    template <typename Signal>
    /**
     * @brief Performs the `signalNameFromPointer` operation.
     * @param signal Value passed to the method.
     * @return The requested signal Name From Pointer.
     */
    static SwString signalNameFromPointer(Signal signal) {
        // Extraire le nom de la méthode membre
        std::string fullName = typeid(signal).name();
        // Nettoyer ou adapter si nécessaire (selon vos conventions)
        return SwString(fullName.c_str());
    }  

protected:
    /**
     * @brief Emits a signal to invoke all connected slots.
     *
     * Invokes all slots connected to the specified signal. Handles different connection types such as
     * DirectConnection, QueuedConnection, and BlockingQueuedConnection.
     *
     * @tparam Args Variadic template parameters representing the types of the arguments for the signal.
     * @param signalName The name of the signal to emit.
     * @param args Arguments to pass to the connected slots.
     *
     * - DirectConnection: The slot is invoked immediately in the current thread.
     * - QueuedConnection: The slot is added to the event queue for later execution.
     * - BlockingQueuedConnection: The current thread is blocked until the slot is executed.
     */
    template<typename... Args>
    /**
     * @brief Performs the `emitSignal` operation.
     * @param signalName Value passed to the method.
     * @param args Value passed to the method.
     */
    void emitSignal(const SwString& signalName, Args... args) {
        auto it = connections.find(signalName);
        if (it != connections.end()) {
            dispatchSlots(it->second, args...);
        }
    }

    template<typename... Args>
    /**
     * @brief Performs the `emitSignal` operation.
     * @param key Value passed to the method.
     * @param args Value passed to the method.
     */
    void emitSignal(const SignalKey& key, Args... args) {
        auto it = typedConnections.find(key);
        if (it != typedConnections.end()) {
            dispatchSlots(it->second, args...);
        }
    }

    template<typename... Args>
    /**
     * @brief Performs the `dispatchSlots` operation.
     * @param slotList Value passed to the method.
     * @param args Value passed to the method.
     */
    void dispatchSlots(std::vector<std::pair<void*, ConnectionType>>& slotList, Args... args) {
        SwObject* senderObject = this;
        ThreadHandle* senderThread = this->threadHandle();
        if (!senderThread) {
            senderThread = ThreadHandle::currentThread();
        }

        for (auto& connection : slotList) {
            auto slotPtr = connection.first;
            auto type = connection.second;
            ISlot<void, Args...>* slot = static_cast<ISlot<void, Args...>*>(slotPtr);
            void* receiverRaw = slot ? slot->receiveur() : nullptr;
            // Guard against a receiver that was destroyed after connect() but before this dispatch.
            // isLive() checks an external registry — safe to call on freed pointers.
            if (receiverRaw && !isLive(receiverRaw)) {
                continue;
            }
            SwObject* receiverObject = receiverRaw ? static_cast<SwObject*>(receiverRaw) : nullptr;
            ThreadHandle* receiverThread = receiverObject ? receiverObject->threadHandle() : nullptr;

            std::function<void()> task = [slot, receiverRaw, receiverObject, senderObject, args...]() mutable {
                if (receiverRaw && !SwObject::isLive(receiverRaw)) {
                    return;
                }
                SwObject* liveSender = senderObject && SwObject::isLive(senderObject) ? senderObject : nullptr;
                if (receiverObject) {
                    receiverObject->setSender(liveSender);
                }
                slot->invoke(receiverRaw, args...);
            };

            switch (type) {
            case DirectConnection:
                task();
                break;
            case QueuedConnection:
                postTaskToThread(receiverThread, std::move(task));
                break;
            case BlockingQueuedConnection:
                executeBlockingOnThread(receiverThread, std::move(task));
                break;
            case AutoConnection:
            default:
                if (!receiverThread || isSameThreadHandle_(receiverThread, senderThread)) {
                    task();
                } else {
                    postTaskToThread(receiverThread, std::move(task));
                }
                break;
            }
        }
    }

    /**
     * @brief Performs the `postTaskToThread` operation.
     * @param targetThread Value passed to the method.
     * @param task Value passed to the method.
     */
    void postTaskToThread(ThreadHandle* targetThread, std::function<void()> task) {
        if (!task) {
            return;
        }
        if (targetThread) {
            targetThread->postTask(std::move(task));
        } else {
            ThreadHandle* fallbackThread = ThreadHandle::currentThread();
            if (!fallbackThread) {
                fallbackThread = ThreadHandle::sharedFallbackThread();
            }
            if (fallbackThread) {
                fallbackThread->postTask(std::move(task));
                return;
            }

            SwCoreApplication::instance()->postEvent(std::move(task));
        }
    }

    /**
     * @brief Performs the `executeBlockingOnThread` operation.
     * @param targetThread Value passed to the method.
     * @param task Value passed to the method.
     */
    void executeBlockingOnThread(ThreadHandle* targetThread, std::function<void()> task) {
        ThreadHandle* senderThread = this->threadHandle();
        if (!senderThread) {
            senderThread = ThreadHandle::currentThread();
        }
        if (!targetThread || isSameThreadHandle_(targetThread, senderThread)) {
            task();
            return;
        }

        struct BlockingContext {
            std::mutex mutex;
            std::condition_variable cv;
            bool completed = false;
        };

        auto ctx = std::make_shared<BlockingContext>();
        std::function<void()> ownedTask = std::move(task);
        auto wrapper = [ctx, ownedTask]() mutable {
            ownedTask();
            {
                std::lock_guard<std::mutex> lock(ctx->mutex);
                ctx->completed = true;
            }
            ctx->cv.notify_one();
        };

        postTaskToThread(targetThread, wrapper);

        std::unique_lock<std::mutex> lock(ctx->mutex);
        ctx->cv.wait(lock, [ctx]() { return ctx->completed; });
    }

    // Conversion générique pointeur de fonction membre -> void*
    template<typename T>
    /**
     * @brief Performs the `toVoidPtr` operation.
     * @param func Value passed to the method.
     * @return The requested to Void Ptr.
     */
    static void* toVoidPtr(T func) {
        // static_assert(sizeof(T) == sizeof(void*), "Pointeur de fonction membre et void* tailles différentes.");
        void* ptr = nullptr;
        std::memcpy(&ptr, &func, sizeof(ptr));
        return ptr;
    }

    // Conversion inverse void* -> pointeur de fonction membre
    template<typename T>
    /**
     * @brief Performs the `fromVoidPtr` operation.
     * @param ptr Value passed to the method.
     * @return The requested from Void Ptr.
     */
    static T fromVoidPtr(void* ptr) {
        static_assert(sizeof(T) == sizeof(void*), "Pointeur de fonction membre et void* tailles différentes.");
        T func;
        std::memcpy(&func, &ptr, sizeof(func));
        return func;
    }

signals:
    DECLARE_SIGNAL_VOID(destroyed)
    DECLARE_SIGNAL(childRemoved, SwObject*)
    DECLARE_SIGNAL(childAdded, SwObject*)

private:
    SwObject* m_parent = nullptr;
    SwList<SwObject*> m_children;
    SwString objectName;

    static std::unordered_set<const void*>& s_liveObjects_() {
        static std::unordered_set<const void*> s;
        return s;
    }
    static std::mutex& s_liveObjectsMutex_() {
        static std::mutex m;
        return m;
    }
    SwMap<SwString, SwString> properties;
    SwMap<SwString, std::vector<std::pair<void*, ConnectionType>>> connections;
    SwMap<SignalKey, std::vector<std::pair<void*, ConnectionType>>> typedConnections;
    SwObject* currentSender = nullptr;
    ThreadHandle* m_threadAffinity = nullptr;
};

inline bool swIsObjectLive(const SwObject* object) {
    return SwObject::isLive(object);
}

inline bool swDispatchEventToObject(SwObject* receiver, SwEvent* event) {
    if (!receiver || !event || !SwObject::isLive(receiver)) {
        return false;
    }
    return receiver->event(event);
}

inline bool swDispatchEventFilter(SwObject* filter, SwObject* watched, SwEvent* event) {
    if (!filter || !watched || !event) {
        return false;
    }
    if (!SwObject::isLive(filter) || !SwObject::isLive(watched)) {
        return false;
    }

    ThreadHandle* filterThread = filter->threadHandle();
    ThreadHandle* watchedThread = watched->threadHandle();
    if (filterThread && watchedThread && !SwObject::isSameThreadHandle_(filterThread, watchedThread)) {
        return false;
    }

    return filter->eventFilter(watched, event);
}

inline bool swDispatchInstalledEventFilters(SwObject* watched, SwEvent* event) {
    if (!watched || !event || !SwObject::isLive(watched)) {
        return false;
    }

    for (int i = static_cast<int>(watched->m_eventFilters.size()) - 1; i >= 0; --i) {
        SwObject* filter = watched->m_eventFilters[static_cast<size_t>(i)];
        if (swDispatchEventFilter(filter, watched, event)) {
            return true;
        }
    }

    return false;
}

inline bool swForwardPostedEventToReceiverThread(SwObject* receiver, SwEvent* event, int priority) {
    if (!receiver || !event || !SwObject::isLive(receiver)) {
        return false;
    }

    ThreadHandle* targetThread = receiver->threadHandle();
    if (!targetThread) {
        return false;
    }

    const std::thread::id currentThreadId = std::this_thread::get_id();
    const std::thread::id targetThreadId = targetThread->threadId();
    if (targetThreadId != std::thread::id{} && currentThreadId == targetThreadId) {
        return false;
    }

    targetThread->postTask([receiver, event, priority]() {
        if (!SwObject::isLive(receiver)) {
            delete event;
            return;
        }
        if (SwCoreApplication* app = SwCoreApplication::instance(false)) {
            app->postEvent(receiver, event, priority);
        } else {
            delete event;
        }
    });
    return true;
}
