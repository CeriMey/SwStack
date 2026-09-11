#include <core/runtime/SwRuntimeLoadWindow.h>
#include <cmath>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <vector>

namespace {
void require(bool condition,const char* message) {if(!condition)throw std::runtime_error(message);}
void close(double actual,double expected) {require(std::abs(actual-expected)<1e-10,"rolling load differs from retained-record sum");}
using Clock=SwRuntimeLoadWindow::Clock;
Clock::time_point stamp(std::int64_t microseconds) {return Clock::time_point(std::chrono::microseconds(microseconds));}
struct Measurement {std::int64_t at;std::uint64_t busy,total;};
double reference(const std::vector<Measurement>& records,std::int64_t now) {
    std::uint64_t busy=0,total=0;
    for(const auto& record:records)if(record.at>=now-1000000) {busy+=record.busy;total+=record.total;}
    return total?100.0*static_cast<double>(busy)/static_cast<double>(total):0;
}
}
int main() {try {
    SwRuntimeLoadWindow window;
    close(window.loadPercentage(stamp(0)),0);
    require(window.lastTotalMicroseconds()==0,"empty window retained a last iteration");
    window.record(stamp(0),25,100);window.record(stamp(500000),50,100);
    close(window.loadPercentage(stamp(1000000)),37.5); // Exactly one second stays included.
    close(window.loadPercentage(stamp(1000001)),50);
    close(window.loadPercentage(stamp(1500000)),50);
    close(window.loadPercentage(stamp(1500001)),0); // Reads also expire an idle loop.
    require(window.lastTotalMicroseconds()==0,"idle expiration retained the old iteration");
    window.record(stamp(2000000),0,0);close(window.loadPercentage(stamp(2000000)),0);
    window.record(stamp(2000001),75,100);close(window.loadPercentage(stamp(2000001)),75);
    require(window.lastTotalMicroseconds()==100,"most recent iteration duration was lost");

    SwRuntimeLoadWindow dense;std::vector<Measurement> records;
    for(int i=0;i<25000;++i) {
        const Measurement item{static_cast<std::int64_t>(i)*100,
                               static_cast<std::uint64_t>((i*17)%101),100};
        records.push_back(item);dense.record(stamp(item.at),item.busy,item.total);
        if(i%37==0) {
            // Repeated profiler/telemetry getters must not alter the aggregate.
            const auto expected=reference(records,item.at);
            close(dense.loadPercentage(stamp(item.at)),expected);
            close(dense.loadPercentage(stamp(item.at)),expected);
        }
    }
    close(dense.loadPercentage(stamp(3000000)),reference(records,3000000));
    close(dense.loadPercentage(stamp(4000000)),0);
    dense.record(stamp(5000000),10,20);close(dense.loadPercentage(stamp(5000000)),50);

    SwRuntimeLoadWindow wrapped;
    const auto max=std::numeric_limits<std::uint64_t>::max();
    wrapped.record(stamp(0),max-5,max-10);wrapped.record(stamp(500000),15,30);
    close(wrapped.loadPercentage(stamp(500000)),100.0*9/19);
    close(wrapped.loadPercentage(stamp(1000001)),50); // Eviction reverses unsigned wrap.
    std::cout<<"PASS: exact one-second boundary, idle expiration, dense repeated reads, refill and unsigned sums\n";
    return 0;
} catch(const std::exception& error) {std::cerr<<error.what()<<'\n';return 1;}}
