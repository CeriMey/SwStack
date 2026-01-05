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

#include "SwString.h"
#include "SwVector.h"
#include "SwDebug.h"
#include "SwFile.h"
#include "SwDir.h"
#include "SwMap.h"

#include <cctype>
#include <cstdio>
static constexpr const char* kSwLogCategory_SwPinOut = "sw.core.hw.swpinout";


/**
 * @brief Utility class able to enumerate the GPIO/pin capabilities of the host platform.
 *
 * Under Linux (Jetson, Raspberry Pi, etc.) this class introspects either
 * /sys/kernel/debug/gpio (preferred because it provides direction + logical level)
 * or, as a fallback, /sys/class/gpio/gpiochip* to at least expose the available
 * GPIO chips and their ranges.
 *
 * On unsupported platforms the helpers simply return an empty list.
 */
class SwPinOut
{
public:
    struct PinInfo {
        SwString chipLabel;      ///< Friendly name, e.g. "tegra-gpio"
        int      chipIndex{0};   ///< gpiochip index
        int      chipBase{0};    ///< first GPIO number handled by the chip
        int      chipEnd{0};     ///< last GPIO handled by the chip
        int      globalNumber{0};///< Absolute GPIO number (as seen in sysfs/debugfs)
        int      localNumber{0}; ///< Relative index inside the chip
        SwString direction;      ///< "in", "out", "hi", etc.
        SwString value;          ///< "hi"/"lo"/"1"/"0"
        SwString consumer;       ///< Optional consumer label
    };

    /**
     * @brief Enumerate the best knowledge the host can provide about available pins.
     */
    static SwVector<PinInfo> availablePins()
    {
#if defined(__linux__)
        SwVector<PinInfo> pins = readFromDebugGpio();
        SwVector<PinInfo> sysPins = readFromSysClassGpio();
        if (pins.isEmpty()) {
            pins = sysPins;
        } else {
            mergeWithSysPins(pins, sysPins);
        }
        enrichWithGpioInfo(pins);
        return pins;
#else
        swCWarning(kSwLogCategory_SwPinOut) << "[SwPinOut] GPIO enumeration not supported on this platform.";
        return {};
#endif
    }

    /**
     * @brief Convenience helper returning a formatted textual dump.
     */
    static SwString dumpAvailablePins()
    {
        SwVector<PinInfo> pins = availablePins();
        SwString dump;
        dump.reserve(pins.size() * 64);
        for (const PinInfo &pin : pins) {
            dump += SwString("[%1/chip%2 base=%3] gpio%4 (local %5) dir=%6 val=%7 consumer=%8\n")
                        .arg(pin.chipLabel.isEmpty() ? SwString("gpiochip") + SwString::number(pin.chipIndex)
                                                     : pin.chipLabel)
                        .arg(SwString::number(pin.chipIndex))
                        .arg(SwString::number(pin.chipBase))
                        .arg(SwString::number(pin.globalNumber))
                        .arg(SwString::number(pin.localNumber))
                        .arg(pin.direction)
                        .arg(pin.value)
                        .arg(pin.consumer.isEmpty() ? SwString("-") : pin.consumer);
        }
        return dump;
    }

private:
#if defined(__linux__)
    static PinInfo* findPin(SwVector<PinInfo>& pins, int globalNumber)
    {
        for (PinInfo& pin : pins) {
            if (pin.globalNumber == globalNumber) {
                return &pin;
            }
        }
        return nullptr;
    }

    static void mergeWithSysPins(SwVector<PinInfo>& pins, const SwVector<PinInfo>& sysPins)
    {
        for (const PinInfo& sysPin : sysPins) {
            if (!findPin(pins, sysPin.globalNumber)) {
                pins.push_back(sysPin);
            }
        }
    }

    static SwString runCommand(const char* command)
    {
        SwString result;
        FILE* pipe = popen(command, "r");
        if (!pipe) {
            return result;
        }
        char buffer[512];
        while (fgets(buffer, sizeof(buffer), pipe)) {
            result += buffer;
        }
        pclose(pipe);
        return result;
    }

    static void enrichWithGpioInfo(SwVector<PinInfo>& pins)
    {
        SwString gpioInfoOutput = runCommand("gpioinfo");
        if (gpioInfoOutput.isEmpty()) {
            return;
        }

        SwMap<int, int> chipBaseMap;
        for (const PinInfo& pin : pins) {
            if (!chipBaseMap.contains(pin.chipIndex)) {
                chipBaseMap.insert(pin.chipIndex, pin.chipBase);
            }
        }

        int currentChip = -1;
        SwString currentChipLabel;
        SwStringList lines = gpioInfoOutput.split('\n');
        for (SwString rawLine : lines) {
            SwString line = rawLine.trimmed();
            if (line.isEmpty()) {
                continue;
            }
            if (line.startsWith("gpiochip")) {
                SwString header = line.split(' ')[0];
                header.replace("gpiochip", "");
                currentChip = header.toInt();
                currentChipLabel = line;
                continue;
            }
            if (!line.startsWith("line") || currentChip < 0) {
                continue;
            }

            int colon = line.indexOf(':');
            if (colon < 0) {
                continue;
            }
            SwString header = line.mid(4, colon - 4).trimmed();
            int localNumber = header.toInt();
            SwString rest = line.mid(colon + 1).trimmed();

            SwString lineLabel;
            SwString consumer;
            int firstQuote = rest.indexOf('"');
            if (firstQuote >= 0) {
                int secondQuote = rest.indexOf('"', firstQuote + 1);
                if (secondQuote > firstQuote) {
                    lineLabel = rest.mid(firstQuote + 1, secondQuote - firstQuote - 1).trimmed();
                    rest = rest.mid(secondQuote + 1).trimmed();
                }
            }
            if (rest.startsWith("\"")) {
                int secondQuote = rest.indexOf('"', 1);
                if (secondQuote > 0) {
                    consumer = rest.mid(1, secondQuote - 1).trimmed();
                    rest = rest.mid(secondQuote + 1).trimmed();
                }
            }

            SwString status = rest.trimmed();
            int base = chipBaseMap.contains(currentChip) ? chipBaseMap[currentChip] : -1;
            int globalNumber = base >= 0 ? base + localNumber : -1;
            if (globalNumber < 0) {
                continue;
            }
            PinInfo* info = findPin(pins, globalNumber);
            if (!info) {
                PinInfo newInfo;
                newInfo.chipIndex = currentChip;
                newInfo.chipBase = base >= 0 ? base : 0;
                newInfo.chipEnd = base >= 0 ? base : 0;
                newInfo.globalNumber = globalNumber;
                newInfo.localNumber = localNumber;
                newInfo.chipLabel = currentChipLabel;
                pins.push_back(newInfo);
                info = &pins.back();
            }
            if (!lineLabel.isEmpty()) {
                info->consumer = lineLabel;
            }
            if (!consumer.isEmpty()) {
                info->consumer = consumer;
            }
            if (!status.isEmpty()) {
                info->direction = status;
            }
        }
    }

    static SwVector<PinInfo> readFromDebugGpio()
    {
        SwVector<PinInfo> pins;
        const SwString debugPath("/sys/kernel/debug/gpio");
        if (!SwFile::isFile(debugPath)) {
            return pins;
        }

        SwFile file(debugPath);
        if (!file.open(SwFile::Read)) {
            swCWarning(kSwLogCategory_SwPinOut) << "[SwPinOut] Cannot open" << debugPath;
            return pins;
        }
        const SwString content = file.readAll();
        swCDebug(kSwLogCategory_SwPinOut) << "[SwPinOut] Raw /sys/kernel/debug/gpio dump:";
        swCDebug(kSwLogCategory_SwPinOut) << content;
        file.close();

        SwStringList lines = content.split('\n');
        SwString currentLabel;
        int currentChip = 0;
        int chipBase = 0;
        int chipEnd = 0;

        for (const SwString &rawLine : lines) {
            SwString line = rawLine.trimmed();
            if (line.isEmpty()) {
                continue;
            }
            if (line.startsWith("gpiochip")) {
                parseDebugChipLine(line, currentLabel, currentChip, chipBase, chipEnd);
                continue;
            }
            if (line.startsWith("gpio-")) {
                PinInfo info = parseDebugPinLine(line, currentLabel, currentChip, chipBase, chipEnd);
                if (info.globalNumber >= 0) {
                    pins.push_back(info);
                }
            }
        }
        return pins;
    }

    static void parseDebugChipLine(const SwString &line, SwString &label, int &chipIndex, int &base, int &end)
    {
        chipIndex = 0;
        base = 0;
        end = 0;
        label.clear();

        const int colon = line.indexOf(':');
        if (colon > 0) {
            SwString token = line.left(colon);
            token.replace("gpiochip", "");
            chipIndex = token.toInt();
        }
        const int rangePos = line.indexOf("GPIOs");
        if (rangePos >= 0) {
            int startPos = rangePos + 5;
            while (startPos < line.size() && std::isspace(line[startPos])) {
                ++startPos;
            }
            int dash = line.indexOf('-', startPos);
            int comma = line.indexOf(',', dash);
            if (dash > startPos) {
                base = line.mid(startPos, dash - startPos).toInt();
            }
            if (comma > dash) {
                end = line.mid(dash + 1, comma - dash - 1).toInt();
            }
        }

        int labelPos = line.indexOf("label:");
        if (labelPos >= 0) {
            SwString rest = line.mid(labelPos + 6).trimmed();
            int comma = rest.indexOf(',');
            if (comma > 0) {
                label = rest.left(comma).trimmed();
            } else {
                label = rest.trimmed();
            }
        } else {
            label = SwString("gpiochip") + SwString::number(chipIndex);
        }
    }

    static PinInfo parseDebugPinLine(const SwString &line,
                                     const SwString &chipLabel,
                                     int chipIndex,
                                     int chipBase,
                                     int chipEnd)
    {
        PinInfo info;
        info.chipLabel = chipLabel;
        info.chipIndex = chipIndex;
        info.chipBase = chipBase;
        info.chipEnd = chipEnd;

        int space = line.indexOf(' ');
        SwString gpioToken = space > 0 ? line.mid(5, space - 5) : SwString();
        info.globalNumber = gpioToken.toInt();
        info.localNumber = info.globalNumber - chipBase;

        int parenStart = line.indexOf('(');
        int parenEnd = line.indexOf(')', parenStart + 1);
        if (parenStart >= 0 && parenEnd > parenStart) {
            info.consumer = line.mid(parenStart + 1, parenEnd - parenStart - 1).trimmed();
        }

        SwString remainder;
        if (parenEnd > 0 && parenEnd + 1 < line.size()) {
            remainder = line.mid(parenEnd + 1).trimmed();
        }
        SwStringList parts = remainder.split(' ');
        SwString* lastTarget = nullptr;
        auto appendToken = [](SwString& target, const SwString& token) {
            if (token.isEmpty()) {
                return;
            }
            if (!target.isEmpty()) {
                target += SwString(" ");
            }
            target += token;
        };
        bool expectActiveQualifier = false;
        for (SwString token : parts) {
            SwString trimmed = token.trimmed();
            if (trimmed.isEmpty()) {
                continue;
            }
            int eq = trimmed.indexOf('=');
            if (eq > 0) {
                SwString key = trimmed.left(eq).toLower();
                SwString value = trimmed.mid(eq + 1).trimmed();
                if (key == "dir" || key == "direction") {
                    info.direction = value.toLower();
                    lastTarget = &info.direction;
                } else if (key == "value" || key == "val") {
                    info.value = value.toLower();
                    lastTarget = &info.value;
                } else if (key == "consumer") {
                    if (!value.isEmpty()) {
                        info.consumer = value;
                    }
                    lastTarget = &info.consumer;
                } else {
                    lastTarget = nullptr;
                }
                expectActiveQualifier = false;
                continue;
            }

            SwString lower = trimmed.toLower();
            if (lower == "active") {
                expectActiveQualifier = true;
                continue;
            }
            if ((lower == "high" || lower == "low") && expectActiveQualifier) {
                appendToken(info.direction, SwString("active-") + lower);
                expectActiveQualifier = false;
                continue;
            }
            expectActiveQualifier = false;

            bool looksDirectionToken =
                lower == "in" || lower == "input" ||
                lower == "out" || lower == "output" ||
                lower == "unused" || lower == "[used]" ||
                lower == "[unused]" || lower == "irq" ||
                lower == "pull-up" || lower == "pull-down" ||
                lower == "active-high" || lower == "active-low" ||
                trimmed.startsWith("[") || trimmed.endsWith("]");
            bool looksValueToken =
                lower == "hi" || lower == "lo" ||
                lower == "high" || lower == "low" ||
                lower == "1" || lower == "0";

            if (lastTarget) {
                appendToken(*lastTarget, lower);
                continue;
            }

            if (looksDirectionToken) {
                appendToken(info.direction, lower);
                continue;
            }
            if (looksValueToken) {
                appendToken(info.value, lower);
                continue;
            }

            if (info.direction.isEmpty()) {
                info.direction = lower;
            } else if (info.value.isEmpty()) {
                info.value = lower;
            } else {
                appendToken(info.consumer, trimmed);
            }
        }
        return info;
    }

    static SwVector<PinInfo> readFromSysClassGpio()
    {
        SwVector<PinInfo> pins;
        const SwString baseDir("/sys/class/gpio");
        if (!SwDir::exists(baseDir)) {
            return pins;
        }

        SwDir dir(baseDir);
        SwStringList entries = dir.entryList(EntryType::Directories);
        swCDebug(kSwLogCategory_SwPinOut) << "[SwPinOut] Enumerating sysfs chips under" << baseDir;
        for (const SwString &entry : entries) {
            if (!entry.startsWith("gpiochip")) {
                continue;
            }
            SwString chipPath = baseDir + "/" + entry;
            swCDebug(kSwLogCategory_SwPinOut) << "[SwPinOut] Inspecting" << chipPath;
            SwFile labelFile(chipPath + "/label");
            SwFile baseFile(chipPath + "/base");
            SwFile ngpioFile(chipPath + "/ngpio");

            SwString label;
            int baseIndex = 0;
            int count = 0;

            if (labelFile.open(SwFile::Read)) {
                label = labelFile.readAll().trimmed();
                labelFile.close();
            }
            if (baseFile.open(SwFile::Read)) {
                baseIndex = baseFile.readAll().trimmed().toInt();
                baseFile.close();
            }
            if (ngpioFile.open(SwFile::Read)) {
                count = ngpioFile.readAll().trimmed().toInt();
                ngpioFile.close();
            }

            for (int i = 0; i < count; ++i) {
                PinInfo info;
                info.chipLabel = label;
                info.chipIndex = entry.mid(8).toInt();
                info.chipBase = baseIndex;
                info.chipEnd = baseIndex + count - 1;
                info.globalNumber = baseIndex + i;
                info.localNumber = i;
                info.direction = "unknown";
                info.value = "unknown";
                pins.push_back(info);
            }
        }
        return pins;
    }
#endif
};
