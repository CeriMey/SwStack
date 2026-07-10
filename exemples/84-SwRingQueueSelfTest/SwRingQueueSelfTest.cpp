#include "core/types/SwRingQueue.h"

#include <cassert>
#include <string>

int main() {
    SwRingQueue<int> queue(3);
    assert(queue.capacity() == 3);
    assert(queue.isEmpty());

    assert(queue.enqueue(1));
    assert(queue.enqueue(2));
    assert(queue.enqueue(3));
    assert(queue.isFull());
    assert(!queue.enqueue(4));
    assert(queue.dequeue() == 1);
    assert(queue.enqueue(4));
    assert(queue[0] == 2);
    assert(queue[1] == 3);
    assert(queue[2] == 4);

    int dropped = 0;
    assert(queue.enqueueOverwrite(5, &dropped));
    assert(dropped == 2);
    assert(queue.dequeue() == 3);
    assert(queue.dequeue() == 4);
    assert(queue.dequeue() == 5);
    assert(queue.isEmpty());

    SwRingQueue<std::string> words({"alpha", "beta", "gamma"});
    assert(words.size() == 3);
    assert(words.front() == "alpha");
    assert(words.back() == "gamma");
    assert(words.takeLast() == "gamma");
    assert(words.takeFirst() == "alpha");
    assert(words.takeFirst() == "beta");
    assert(words.isEmpty());

    SwRingQueue<int> shrink(5);
    for (int i = 1; i <= 5; ++i) {
        assert(shrink.enqueue(i));
    }
    assert(shrink.setCapacity(3) == 2);
    assert(shrink.size() == 3);
    assert(shrink.dequeue() == 3);
    assert(shrink.dequeue() == 4);
    assert(shrink.dequeue() == 5);

    SwRingQueue<std::string> copied({"one", "two"});
    SwRingQueue<std::string> assigned;
    assigned = copied;
    assert(assigned == copied);
    assert(assigned.dequeue() == "one");
    assert(copied.dequeue() == "one");

    return 0;
}
