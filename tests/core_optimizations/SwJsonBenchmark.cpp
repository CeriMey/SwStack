#include "SwJsonDocument.h"
#include <algorithm>
#include <chrono>
#include <iostream>
#include <vector>

// Keep the corpus outside the timed section. Five repeats, report median wall
// time and a checksum. Compare separate baseline/optimized Release binaries on
// the same idle machine; this does not measure application CPU consumption.
static volatile size_t resultSink;
static size_t observe(const SwString& value) {
    // An unpredictable byte read makes the produced contents observable; merely
    // asking for size could let the compiler discard some writes in short cases.
    const size_t size = value.size();
    return size + (size ? static_cast<unsigned char>(value[resultSink % size]) : 0);
}
template<class Work>
static void measure(const SwString& label, int iterations, Work work) {
    std::vector<double> samples;
    for (int repeat = 0; repeat < 5; ++repeat) {
        size_t sum = 0;
        const auto start = std::chrono::steady_clock::now();
        for (int i = 0; i < iterations; ++i) sum += work();
        resultSink = sum;
        samples.push_back(std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count());
    }
    std::sort(samples.begin(), samples.end());
    std::cout << label.constData() << " iterations=" << iterations << " median_ms=" << samples[2]
              << " checksum=" << resultSink << '\n';
}

int main() {
    for (size_t size : {8, 28, 128, 2048}) {
        const SwString input(size, 'a');
        measure(SwString("escape_") + SwString::number(size), 200000,
                [&] { return observe(SwJsonValue::escapeString(input)); });
    }
    SwJsonObject object;
    for (int i = 0; i < 24; ++i) {
        const auto prefix = SwString("camera_") + SwString::number(i);
        object[prefix + "/status"] = SwString("Tracking active: position valid");
        object[prefix + "/value"] = double(i) + .25;
        object[prefix + "/active"] = true;
    }
    const SwJsonDocument metadata(object);
    const auto text = metadata.toJson(SwJsonDocument::JsonFormat::Compact);
    std::cout << "metadata_bytes=" << text.size() << '\n';
    measure("metadata_serialize", 10000,
            [&] { return observe(metadata.toJson(SwJsonDocument::JsonFormat::Compact)); });
    measure("metadata_parse", 10000, [&] {
        SwJsonDocument document;
        SwString error;
        return document.loadFromJson(text, error) ? text.size() : 0;
    });
    SwJsonArray payload;
    payload.append(SwString(8192, 'x'));
    const SwJsonDocument strings(payload);
    const auto longText = strings.toJson(SwJsonDocument::JsonFormat::Compact);
    measure("long_serialize", 50000,
            [&] { return observe(strings.toJson(SwJsonDocument::JsonFormat::Compact)); });
    measure("long_parse", 50000, [&] {
        SwJsonDocument document;
        SwString error;
        return document.loadFromJson(longText, error) ? longText.size() : 0;
    });
}
