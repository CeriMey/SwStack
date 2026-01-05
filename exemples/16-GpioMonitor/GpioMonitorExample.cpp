#include "SwGuiConsoleApplication.h"
#include <SwDebug.h>
#include <SwDir.h>
#include <SwFile.h>
#include <SwObject.h>
#include <SwPinOut.h>
#include <SwString.h>
#include <SwVector.h>
#include <Sw.h>
#include <SwMap.h>

#include <atomic>
#include <chrono>
#include <cstdio>
#include <iostream>
#include <limits>
#include <string>
#include <thread>
#include <iomanip>

namespace {

SwString execCommand(const SwString& command)
{
    SwString result;
    FILE* pipe = popen(command.toStdString().c_str(), "r");
    if (!pipe) {
        return result;
    }
    char buffer[256];
    while (fgets(buffer, sizeof(buffer), pipe)) {
        result += buffer;
    }
    pclose(pipe);
    return result.trimmed();
}

SwVector<int> parsePinListString(const SwString& csv)
{
    SwVector<int> pins;
    if (csv.isEmpty()) {
        return pins;
    }
    SwStringList entries = csv.split(',');
    for (const SwString& raw : entries) {
        SwString trimmed = raw.trimmed();
        if (trimmed.isEmpty()) {
            continue;
        }
        bool ok = false;
        int gpio = trimmed.toInt(&ok);
        if (ok) {
            pins.push_back(gpio);
        } else {
            swWarning() << "[GpioMonitorExample] Ignoring pin entry" << trimmed << "in list" << csv;
        }
    }
    return pins;
}

struct GpioWriteRequest {
    int gpio{0};
    SwString value;
};

SwString normalizeWriteValue(const SwString& token)
{
    SwString lower = token.toLower();
    if (lower == "1" || lower == "hi" || lower == "high" || lower == "true") {
        return SwString("1");
    }
    if (lower == "0" || lower == "lo" || lower == "low" || lower == "false") {
        return SwString("0");
    }
    return token;
}

SwVector<GpioWriteRequest> parseWriteSpecification(const SwString& spec)
{
    SwVector<GpioWriteRequest> requests;
    if (spec.isEmpty()) {
        return requests;
    }
    SwStringList entries = spec.split(',');
    for (const SwString& entry : entries) {
        SwString trimmed = entry.trimmed();
        if (trimmed.isEmpty()) {
            continue;
        }
        int sep = trimmed.indexOf('=');
        if (sep < 0) {
            sep = trimmed.indexOf(':');
        }
        if (sep <= 0) {
            swWarning() << "[GpioMonitorExample] Invalid write entry:" << trimmed << "(expected gpio=value)";
            continue;
        }
        SwString left = trimmed.left(sep).trimmed();
        SwString right = trimmed.mid(sep + 1).trimmed();
        bool ok = false;
        int gpio = left.toInt(&ok);
        if (!ok) {
            swWarning() << "[GpioMonitorExample] Invalid GPIO number in" << trimmed;
            continue;
        }
        GpioWriteRequest req;
        req.gpio = gpio;
        req.value = normalizeWriteValue(right);
        requests.push_back(req);
    }
    return requests;
}

SwString gpioDirectoryPath(int gpio)
{
    return SwString("/sys/class/gpio/gpio%1").arg(SwString::number(gpio));
}

SwString gpioValuePath(int gpio)
{
    return SwString("/sys/class/gpio/gpio%1/value").arg(SwString::number(gpio));
}

SwString readGpioValueFromSysfs(int globalNumber)
{
    SwString path = gpioValuePath(globalNumber);
    if (!SwFile::isFile(path)) {
        SwVector<SwPinOut::PinInfo> snapshot = SwPinOut::availablePins();
        for (const auto& info : snapshot) {
            if (info.globalNumber == globalNumber) {
                return info.value.isEmpty() ? SwString("n/a") : info.value;
            }
        }
        return SwString("n/a");
    }

    SwFile file(path);
    if (!file.open(SwFile::Read)) {
        swWarning() << "[GpioMonitorExample] Unable to open" << path << "to read GPIO value.";
        return SwString("n/a");
    }

    SwString value = file.readAll().trimmed();
    file.close();
    return value;
}

bool ensureGpioExported(int gpio)
{
    SwString dirPath = gpioDirectoryPath(gpio);
    if (SwDir::exists(dirPath)) {
        return true;
    }

    SwFile exportFile("/sys/class/gpio/export");
    if (!exportFile.open(SwFile::Write)) {
        swWarning() << "[GpioMonitorExample] Unable to open /sys/class/gpio/export to export GPIO" << gpio;
        return false;
    }
    if (!exportFile.write(SwString::number(gpio))) {
        swWarning() << "[GpioMonitorExample] Failed to write export value for GPIO" << gpio;
        exportFile.close();
        return false;
    }
    exportFile.close();
    return SwDir::exists(dirPath);
}

bool writeTextFile(const SwString& path, SwString value)
{
    SwFile file(path);
    if (!file.open(SwFile::Write)) {
        return false;
    }
    if (!value.endsWith("\n")) {
        value += SwString("\n");
    }
    bool ok = file.write(value);
    file.close();
    return ok;
}

bool setGpioDirection(int gpio, const SwString& direction)
{
    if (direction.isEmpty()) {
        return true;
    }
    if (!ensureGpioExported(gpio)) {
        return false;
    }
    SwString path = gpioDirectoryPath(gpio) + SwString("/direction");
    if (!writeTextFile(path, direction)) {
        swWarning() << "[GpioMonitorExample] Unable to set direction for GPIO" << gpio << "with value" << direction;
        return false;
    }
    return true;
}

bool writeGpioValueToSysfs(int gpio, const SwString& value)
{
    if (!ensureGpioExported(gpio)) {
        return false;
    }
    SwString path = gpioValuePath(gpio);
    if (!writeTextFile(path, value)) {
        swWarning() << "[GpioMonitorExample] Unable to write GPIO" << gpio << "value" << value;
        return false;
    }
    return true;
}

void dumpPinValues(const SwVector<int>& pins)
{
    if (pins.isEmpty()) {
        swWarning() << "[GpioMonitorExample] No GPIO numbers supplied for reading.";
        return;
    }
    for (int gpio : pins) {
        SwString value = readGpioValueFromSysfs(gpio);
        std::cout << "gpio" << gpio << " => " << value.toStdString() << "\n";
    }
}

bool performWriteRequests(const SwVector<GpioWriteRequest>& requests, const SwString& directionOverride)
{
    if (requests.isEmpty()) {
        swWarning() << "[GpioMonitorExample] No valid GPIO assignments to write.";
        return false;
    }
    bool allOk = true;
    for (const auto& req : requests) {
        if (!directionOverride.isEmpty() && !setGpioDirection(req.gpio, directionOverride)) {
            allOk = false;
            continue;
        }
        if (!writeGpioValueToSysfs(req.gpio, req.value)) {
            allOk = false;
        }
    }
    return allOk;
}

SwString normalizeState(const SwPinOut::PinInfo& pin)
{
    SwString dir = pin.direction.toLower().trimmed();
    if (dir.contains("unused")) {
        return SwString("off");
    }
    if (dir.contains("out")) {
        return SwString("out");
    }
    if (dir.contains("in")) {
        return SwString("in");
    }
    return dir.isEmpty() ? SwString("?") : pin.direction.trimmed();
}

SwString normalizeValue(const SwPinOut::PinInfo& pin, const SwString& state)
{
    if (state == "off") {
        return SwString("ko");
    }
    SwString value = pin.value.trimmed();
    return value.isEmpty() ? SwString("-") : value;
}

void dumpPinsToStdout(const SwVector<SwPinOut::PinInfo>& pins)
{
    std::cout << "==== GPIO Snapshot ====\n";
    if (pins.isEmpty()) {
        std::cout << "No GPIO information was exposed by the host OS.\n";
        return;
    }

    auto printHeader = []() {
        std::cout << std::left
                  << std::setw(8) << "local"
                  << std::setw(8) << "gpio"
                  << std::setw(6) << "state"
                  << std::setw(24) << "label/consumer"
                  << std::setw(6) << "value"
                  << "\n"
                  << std::string(52, '-') << "\n";
    };

    int currentChip = -1;
    int currentBase = -1;
    for (const auto& pin : pins) {
        if (currentChip != pin.chipIndex || currentBase != pin.chipBase) {
            currentChip = pin.chipIndex;
            currentBase = pin.chipBase;
            std::cout << "[chip:gpiochip" << currentChip
                      << " base:" << currentBase << "]\n";
            printHeader();
        }

        SwString state = normalizeState(pin);
        SwString value = normalizeValue(pin, state);
        SwString label = pin.consumer.trimmed();
        if (label.isEmpty()) {
            label = SwString("-");
        }

        std::cout << std::left
                  << std::setw(8) << pin.localNumber
                  << std::setw(8) << pin.globalNumber
                  << std::setw(6) << state.toStdString()
                  << std::setw(24) << label.toStdString()
                  << std::setw(6) << value.toStdString()
                  << "\n";
    }
}

void monitorPinBlocking(const SwString& chipName, int localNumber, int gpioNumber)
{
    std::atomic<bool> stop{false};
    std::thread inputThread([&]() {
        std::cin.get();
        stop.store(true);
    });

    while (!stop.load()) {
        SwString command = SwString("gpioget %1 %2")
                               .arg(chipName)
                               .arg(SwString::number(localNumber));
        SwString value = execCommand(command);
        if (value.isEmpty()) {
            value = readGpioValueFromSysfs(gpioNumber);
        }
        if (value.isEmpty()) {
            value = SwString("?");
        }
        std::cout << "\rgpio " << gpioNumber << " => "
                  << value.toStdString() << "   " << std::flush;
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

    if (inputThread.joinable()) {
        inputThread.join();
    }
    std::cout << "\n";
}

void showReadDialog()
{
    SwVector<SwPinOut::PinInfo> pins = SwPinOut::availablePins();
    dumpPinsToStdout(pins);
    std::cout << "\nEnter GPIO numbers (comma separated): ";
    std::string input;
    std::getline(std::cin, input);
    SwVector<int> toRead = parsePinListString(SwString(input));
    dumpPinValues(toRead);
}

void showWriteDialog()
{
    std::cout << "Enter gpio=value assignments (comma separated, e.g. 321=1,322=0): ";
    std::string assignmentLine;
    std::getline(std::cin, assignmentLine);
    SwVector<GpioWriteRequest> requests = parseWriteSpecification(SwString(assignmentLine));
    if (requests.isEmpty()) {
        std::cout << "No valid assignment entered.\n";
        return;
    }
    std::cout << "Optional direction (in/out, leave empty to skip): ";
    std::string directionInput;
    std::getline(std::cin, directionInput);
    SwString direction = SwString(directionInput).trimmed().toLower();

    if (performWriteRequests(requests, direction)) {
        std::cout << "Write commands applied.\n";
    } else {
        std::cout << "Some write commands failed.\n";
    }
}

} // namespace

int main(int argc, char* argv[])
{
    SW_UNUSED(argc);
    SW_UNUSED(argv);
    SwGuiConsoleApplication gui;
    gui.setTitle("SwCore GPIO Console");
    gui.setFooter("[number] Select  [b] Back  [q] Quit");

    gui.addEntry("gpio/list", "List GPIO table", [&]() {
        gui.showModal("GPIO Listing", []() {
            dumpPinsToStdout(SwPinOut::availablePins());
        });
    });

    gui.addEntry("gpio/read", "Read discrete inputs", [&]() {
        gui.showModal("Read GPIO", []() {
            showReadDialog();
            std::cout << "\nPress Enter to return...";
            std::string dummy;
            std::getline(std::cin, dummy);
        }, false);
    });

    gui.addEntry("gpio/write", "Drive GPIO values", [&]() {
        gui.showModal("Write GPIO", []() {
            showWriteDialog();
            std::cout << "\nPress Enter to return...";
            std::string dummy;
            std::getline(std::cin, dummy);
        }, false);
    });

    SwVector<SwPinOut::PinInfo> initialPins = SwPinOut::availablePins();
    SwMap<SwString, SwVector<SwPinOut::PinInfo>> chipToPins;
    SwMap<SwString, SwString> chipDisplay;
    for (const auto& pin : initialPins) {
        SwString key = SwString("chip%1_base%2").arg(SwString::number(pin.chipIndex)).arg(SwString::number(pin.chipBase));
        chipToPins[key].push_back(pin);
        if (!chipDisplay.contains(key)) {
            SwString display = SwString("[chip:gpiochip%1 base:%2]").arg(SwString::number(pin.chipIndex)).arg(SwString::number(pin.chipBase));
            chipDisplay.insert(key, display);
        }
    }

    for (auto it = chipToPins.begin(); it != chipToPins.end(); ++it) {
        SwString chipKey = it.key();
        SwString chipPath = SwString("gpio/monitor/") + chipKey;
        gui.addEntry(chipPath, chipDisplay.value(chipKey));
        const SwVector<SwPinOut::PinInfo>& pins = it.value();
        for (const auto& pin : pins) {
            SwString label = pin.consumer.isEmpty() ? SwString("gpio ") + SwString::number(pin.globalNumber)
                                                    : pin.consumer;
            SwString pinPath = chipPath + "/" + SwString("gpio%1").arg(SwString::number(pin.globalNumber));
            const SwString chipName = SwString("gpiochip%1").arg(SwString::number(pin.chipIndex));
            gui.addEntry(pinPath, label, [chipName, pin, &gui]() {
                gui.showModal(SwString("Monitoring gpio %1").arg(SwString::number(pin.globalNumber)), [chipName, pin]() {
                    std::cout << "Press Enter to stop monitoring.\n";
                    monitorPinBlocking(chipName, pin.localNumber, pin.globalNumber);
                }, false);
            });
        }
    }

    gui.exec();
    return 0;
}
