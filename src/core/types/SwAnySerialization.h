#pragma once

#include "SwString.h"
#include "SwByteArray.h"
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <functional>
#include <limits>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

// Implementation of SwAny's typed binary API. No SwAny box is constructed:
// registered functions receive the original object by const reference.
namespace swAnyDetail {
struct BinaryWriter {
    uint8_t* p;
    size_t cap, pos;
    BinaryWriter(uint8_t* data, size_t capacity) : p(data), cap(capacity), pos(0) {}
    bool writeBytes(const void* data, size_t length) noexcept {
        if (pos > cap || length > cap - pos || (length && (!p || !data))) return false;
        if (length) std::memcpy(p + pos, data, length);
        pos += length;
        return true;
    }
    template<class T> bool writePOD(const T& value) noexcept {
        static_assert(std::is_trivially_copyable<T>::value, "writePOD requires a trivial value");
        return writeBytes(&value, sizeof(T));
    }
    size_t size() const { return pos; }
    bool writeSizedBytes(const void* data, size_t length) noexcept {
        if (length > std::numeric_limits<uint32_t>::max() || pos > cap ||
            cap - pos < sizeof(uint32_t) || length > cap - pos - sizeof(uint32_t) ||
            (length && !data)) return false;
        const auto size = static_cast<uint32_t>(length);
        return writePOD(size) && writeBytes(data, length);
    }
};
struct BinaryReader {
    const uint8_t* p;
    size_t cap, pos;
    BinaryReader(const uint8_t* data, size_t capacity) : p(data), cap(capacity), pos(0) {}
    bool readBytes(void* data, size_t length) {
        if (pos > cap || length > cap - pos || (length && (!p || !data))) return false;
        if (length) std::memcpy(data, p + pos, length);
        pos += length;
        return true;
    }
    template<class T> bool readPOD(T& value) {
        static_assert(std::is_trivially_copyable<T>::value, "readPOD requires a trivial value");
        return readBytes(&value, sizeof(T));
    }
    // Validate the complete field before allocation, including empty fields.
    bool readSizedBytes(const uint8_t*& data, uint32_t& length) {
        const auto start = pos;
        if (!readPOD(length) || pos > cap || length > cap - pos || !p) {
            pos = start;
            return false;
        }
        data = p + pos;
        pos += length;
        return true;
    }
};

template<class Function> struct FunctionSlot {
    static std::shared_ptr<const Function>& storage() {
        static std::shared_ptr<const Function> function;
        return function;
    }
    static std::shared_ptr<const Function> get() {
        return std::atomic_load_explicit(&storage(), std::memory_order_acquire);
    }
    static void set(Function function) {
        std::shared_ptr<const Function> owned = std::make_shared<const Function>(std::move(function));
        std::atomic_store_explicit(&storage(), std::move(owned), std::memory_order_release);
    }
};
template<class From, class To>
using TypedConversion = FunctionSlot<std::function<To(const From&)>>;
template<class T> struct BinaryFunctions {
    std::function<bool(BinaryWriter&, const T&)> write;
    std::function<bool(BinaryReader&, T&)> read;
};
template<class T> struct BinaryRegistration : FunctionSlot<BinaryFunctions<T>> {
    using Write = bool (*)(BinaryWriter&, const T&);
    using Read = bool (*)(BinaryReader&, T&);
    static std::atomic<Write>& writer() { static std::atomic<Write> value{nullptr}; return value; }
    static std::atomic<Read>& reader() { static std::atomic<Read> value{nullptr}; return value; }
    static void set(BinaryFunctions<T> functions, Write write, Read read) {
        static std::mutex mutex;
        std::lock_guard<std::mutex> lock(mutex);
        // During replacement readers can use the owned previous/new entry.
        // Stateless callbacks need only a function address on the hot path;
        // captured callbacks retain their immutable owner for the entire call.
        writer().store(nullptr, std::memory_order_release);
        reader().store(nullptr, std::memory_order_release);
        FunctionSlot<BinaryFunctions<T>>::set(std::move(functions));
        writer().store(write, std::memory_order_release);
        reader().store(read, std::memory_order_release);
    }
};

template<class T>
constexpr bool nativeScalar = std::is_arithmetic<T>::value || std::is_enum<T>::value;
template<class T>
constexpr bool nativeBinary = nativeScalar<T> || std::is_same<T, SwString>::value ||
    std::is_same<T, SwByteArray>::value || std::is_same<T, std::string>::value ||
    std::is_same<T, std::vector<uint8_t>>::value;

template<class T> bool writeBinary(BinaryWriter& writer, const T& value) noexcept(nativeBinary<T>) {
    if constexpr (nativeScalar<T>) {
        return writer.writePOD(value);
    } else if constexpr (std::is_same<T, SwString>::value || std::is_same<T, SwByteArray>::value) {
        return writer.writeSizedBytes(value.constData(), value.size());
    } else if constexpr (std::is_same<T, std::string>::value || std::is_same<T, std::vector<uint8_t>>::value) {
        return writer.writeSizedBytes(value.data(), value.size());
    } else {
        if (auto write = BinaryRegistration<T>::writer().load(std::memory_order_acquire))
            return write(writer, value);
        if (auto functions = BinaryRegistration<T>::get()) return functions->write(writer, value);
        if (auto convert = TypedConversion<T, SwString>::get()) {
            const auto text = (*convert)(value);
            return writeBinary(writer, text);
        }
        if (auto convert = TypedConversion<T, std::string>::get()) {
            const auto text = (*convert)(value);
            return writeBinary(writer, text);
        }
        return false;
    }
}
template<class T> bool readBinary(BinaryReader& reader, T& value) {
    if constexpr (std::is_same<T, bool>::value) {
        unsigned char bytes[sizeof(bool)];
        if (!reader.readBytes(bytes, sizeof(bytes))) return false;
        const bool yes = true, no = false;
        if (std::memcmp(bytes, &yes, sizeof(bool)) == 0) value = true;
        else if (std::memcmp(bytes, &no, sizeof(bool)) == 0) value = false;
        else return false;
        return true;
    } else if constexpr (nativeScalar<T>) {
        return reader.readPOD(value);
    } else if constexpr (std::is_same<T, SwString>::value || std::is_same<T, std::string>::value ||
                         std::is_same<T, SwByteArray>::value || std::is_same<T, std::vector<uint8_t>>::value) {
        const uint8_t* data = nullptr;
        uint32_t length = 0;
        if (!reader.readSizedBytes(data, length)) return false;
        if constexpr (std::is_same<T, std::vector<uint8_t>>::value)
            value.assign(data, data + length);
        else if constexpr (std::is_same<T, SwByteArray>::value)
            value = SwByteArray(reinterpret_cast<const char*>(data), static_cast<size_t>(length));
        else
            value = T(reinterpret_cast<const char*>(data), static_cast<size_t>(length));
        return true;
    } else {
        if (auto read = BinaryRegistration<T>::reader().load(std::memory_order_acquire))
            return read(reader, value);
        if (auto functions = BinaryRegistration<T>::get()) return functions->read(reader, value);
        if (auto convert = TypedConversion<SwString, T>::get()) {
            SwString text;
            if (!readBinary(reader, text)) return false;
            value = (*convert)(text);
            return true;
        }
        if (auto convert = TypedConversion<std::string, T>::get()) {
            std::string text;
            if (!readBinary(reader, text)) return false;
            value = (*convert)(text);
            return true;
        }
        return false;
    }
}
} // namespace swAnyDetail
