#include "core/types/SwByteRingBuffer.h"

#include <cassert>
#include <cstring>

int main() {
    SwByteRingBuffer buffer(8);
    assert(buffer.isEmpty());
    assert(buffer.capacity() == 8);

    buffer.append("abcdef", 6);
    assert(buffer.size() == 6);
    assert(buffer.contiguousSize() == 6);
    assert(std::memcmp(buffer.contiguousData(), "abcdef", 6) == 0);

    buffer.consume(4);
    assert(buffer.size() == 2);
    assert(std::memcmp(buffer.contiguousData(), "ef", 2) == 0);

    buffer.append("ghijkl", 6);
    assert(buffer.size() == 8);
    assert(buffer.toByteArray() == SwByteArray("efghijkl", 8));
    assert(buffer.contiguousSize() == 4);
    assert(buffer.indexOf("ghi") == 2);
    assert(buffer.indexOf(SwByteArray("jkl", 3)) == 5);
    assert(buffer.startsWith(SwByteArray("ef", 2)));
    assert(buffer.mid(2, 3) == SwByteArray("ghi", 3));

    SwByteArray part = buffer.read(3);
    assert(part == SwByteArray("efg", 3));
    assert(buffer.toByteArray() == SwByteArray("hijkl", 5));

    char direct[4] = {};
    const std::size_t directBytes = buffer.readInto(direct, sizeof(direct));
    assert(directBytes == 4);
    assert(std::memcmp(direct, "hijk", 4) == 0);
    assert(buffer.toByteArray() == SwByteArray("l", 1));

    buffer.append("mnopqrstuvwxyz", 14);
    assert(buffer.toByteArray() == SwByteArray("lmnopqrstuvwxyz", 15));
    assert(buffer.capacity() >= 15);

    buffer.clear();
    assert(buffer.isEmpty());
    assert(buffer.contiguousData() == nullptr);

    return 0;
}
