#include "SwString.h"
#include "SwSet.h"
#include "SwFileInfo.h"
#include "SwIpcRpcResources.h"
#include <iostream>
#include <limits>
#include <stdexcept>
#include <type_traits>

static void require(bool condition, const char* error) {
    if (!condition) throw std::runtime_error(error);
}

static void numbers() {
    bool ok = false;
    require(SwString("4294967295").toUInt(&ok) == 4294967295U && ok, "uint32 maximum");
    require(SwString("18446744073709551615").toULongLong(&ok) ==
        std::numeric_limits<unsigned long long>::max() && ok, "uint64 maximum");
    require(SwString("\t +42\r\n").toUInt(&ok) == 42 && ok, "ASCII whitespace and plus");
    require(SwString("0").toUInt(&ok) == 0 && ok, "zero success flag");
    require(SwString("0xFF").toUInt(&ok, 0) == 255 && ok, "hex autodetection");
    require(SwString("0B101").toUInt(&ok, 2) == 5 && ok, "binary prefix");
    require(SwString("077").toUInt(&ok, 0) == 63 && ok, "octal autodetection");
    require(SwString("Zz").toUInt(&ok, 36) == 1295 && ok, "base 36");
    for (const auto* invalid : {"", " ", "-1", "+", "12x", "1 2", "1,000", "4.2", "4294967296"}) {
        ok = true;
        require(SwString(invalid).toUInt(&ok) == 0 && !ok, "invalid uint32 accepted");
    }
    require(SwString("18446744073709551616").toULongLong(&ok) == 0 && !ok, "uint64 overflow");
    require(SwString("12\0hidden", 9).toUInt(&ok) == 0 && !ok, "embedded NUL");
    require(SwString("0x").toUInt(&ok, 0) == 0 && !ok, "empty prefixed number");
    require(SwString("08").toUInt(&ok, 0) == 0 && !ok, "invalid octal");
    for (int base : {-1, 1, 37})
        require(SwString("1").toUInt(&ok, base) == 0 && !ok, "invalid base");
}

struct ConstantHash { size_t operator()(const SwString&) const { return 0; } };
static void sets() {
    SwSet<SwString, ConstantHash> values{"caméra", "video", "video"};
    static_assert(std::is_const<typename std::remove_reference<decltype(*values.begin())>::type>::value,
                  "A set key must not be mutable through an iterator");
    values.reserve(32);
    require(values.size() == 2 && values.contains("caméra"), "deduplication/hash collision");
    values.insert(SwString("communication"));
    require(values.remove("video") && !values.remove("missing"), "set removal");
    auto copy = values;
    copy.erase(copy.find("caméra"));
    require(values.contains("caméra") && !copy.contains("caméra"), "set copy independence");
    require(values != copy, "set inequality");
    values.clear(); require(values.isEmpty(), "set clear");
}

int main(int argc, char** argv) {
    try {
        numbers(); sets();
        require(argc > 0 && SwFileInfo(SwString(argv[0])).isFile(), "SwString file path");
        require(SwFileInfo(argv[0]).isFile(), "literal file path overload");
        require(!SwFileInfo(SwString()).exists(), "empty file path");
        using Resources = sw::ipc::detail::RpcResponseResources;
        for (const auto* malformed : {"", "__rpc__|1000|echo|1", "__rpc_ret__|", "__rpc_ret__|1000|echo|-1",
                "__rpc_ret__|1000|echo|0", "__rpc_ret__|1000|echo|4294967296", "__rpc_ret__|1000|echo|12x"})
            require(!Resources::deadClient(malformed), "malformed PID accepted");
        require(!Resources::deadClient("__rpc_ret__|1000|echo|" + SwString::number(sw::ipc::detail::currentPid())),
                "live client treated as dead");
        std::cout << "PASS SwString unsigned parsing, SwSet, native file paths and RPC PID validation\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n'; return 1;
    }
}
