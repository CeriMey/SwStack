#include "core/types/SwDequeue.h"

#include <cassert>
#include <deque>
#include <string>

int main() {
    SwDequeue<int> queue;
    assert(queue.isEmpty());

    queue.append(2);
    queue.prepend(1);
    queue.enqueue(3);

    assert(queue.size() == 3);
    assert(queue.count() == 3);
    assert(queue.first() == 1);
    assert(queue.head() == 1);
    assert(queue.last() == 3);
    assert(queue.value(10, 42) == 42);

    assert(queue.dequeue() == 1);
    assert(queue.takeLast() == 3);
    assert(queue.takeFirst() == 2);
    assert(queue.isEmpty());

    int out = 0;
    assert(!queue.tryDequeue(out));

    queue << 4 << 5 << 4;
    assert(queue.count(4) == 2);
    assert(queue.contains(5));
    assert(queue.indexOf(5) == 1);
    assert(queue.lastIndexOf(4) == 2);

    queue.insert(1, 9);
    assert(queue[1] == 9);
    assert(queue.takeAt(1) == 9);
    assert(queue.removeOne(5));
    assert(queue.removeAll(4) == 2);
    assert(queue.isEmpty());

    SwDequeue<std::string> words({"alpha", "beta", "gamma"});
    assert(words.startsWith("alpha"));
    assert(words.endsWith("gamma"));
    assert(words.mid(1).size() == 2);
    assert(words.mid(1, 1).first() == "beta");
    assert(words.replace(1, "delta"));
    words.swapItemsAt(0, 2);
    assert(words.first() == "gamma");
    assert(words.last() == "alpha");

    const std::deque<int> stdValues = {7, 8, 9};
    SwDequeue<int> fromStd(stdValues);
    assert(fromStd.toStdDeque() == stdValues);

    SwDeque<int> alias;
    alias.enqueue(10);
    assert(alias.dequeue() == 10);

    return 0;
}
