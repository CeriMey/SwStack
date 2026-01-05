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

#pragma once

#include <ctime>
#include <string>
#include <stdexcept>
#include <sstream>
#include <iomanip>
#include <math.h>
class SwDateTime {
public:
    // Constructeurs
    SwDateTime() : time_(std::time(nullptr)) {} // Date/heure actuelle

    SwDateTime(std::time_t time) : time_(time) {} // Constructeur avec std::time_t

    SwDateTime(int year, int month, int day, int hour = 0, int minute = 0, int second = 0) {
        setDateTime(year, month, day, hour, minute, second);
    }

    // Constructeur de copie
    SwDateTime(const SwDateTime& other) = default;

    // Constructeur de déplacement
    SwDateTime(SwDateTime&& other) noexcept = default;

    // Opérateur d'affectation (copie)
    SwDateTime& operator=(const SwDateTime& other) = default;

    // Opérateur d'affectation (déplacement)
    SwDateTime& operator=(SwDateTime&& other) noexcept = default;

    // Destructeur
    ~SwDateTime() = default;


    operator std::time_t() const {
        return time_;
    }

    operator std::time_t&() {
        return time_;
    }

    operator const std::time_t&() const {
        return time_;
    }

    // Setter avec des champs spécifiques
    void setDateTime(int year, int month, int day, int hour = 0, int minute = 0, int second = 0) {
        if ((year < 1601) || (month < 1) || (month > 12) || (day < 1) || (day > 31)) {
            throw std::invalid_argument("Invalid date values!");
        }

        std::tm timeInfo = {};
        timeInfo.tm_year = year - 1900; // tm_year est l'année depuis 1900
        timeInfo.tm_mon = month - 1;   // tm_mon est de 0 à 11
        timeInfo.tm_mday = day;
        timeInfo.tm_hour = hour;
        timeInfo.tm_min = minute;
        timeInfo.tm_sec = second;

        std::time_t newTime = std::mktime(&timeInfo);
        if (newTime == -1) {
            throw std::runtime_error("Failed to convert to std::time_t");
        }

        time_ = newTime;
    }

    // Conversion vers std::time_t
    std::time_t toTimeT() const {
        return time_;
    }

    // Accès aux composants
    int year() const { return localTime().tm_year + 1900; }
    int month() const { return localTime().tm_mon + 1; }
    int day() const { return localTime().tm_mday; }
    int hour() const { return localTime().tm_hour; }
    int minute() const { return localTime().tm_min; }
    int second() const { return localTime().tm_sec; }

    // Représentation sous forme de chaîne (ISO 8601)
    std::string toString() const {
        const std::tm& timeInfo = localTime();
        std::ostringstream oss;
        oss << std::setfill('0') << std::setw(4) << (timeInfo.tm_year + 1900) << '-'
            << std::setw(2) << (timeInfo.tm_mon + 1) << '-'
            << std::setw(2) << timeInfo.tm_mday << ' '
            << std::setw(2) << timeInfo.tm_hour << ':'
            << std::setw(2) << timeInfo.tm_min << ':'
            << std::setw(2) << timeInfo.tm_sec;
        return oss.str();
    }

    // Opérateurs de comparaison
    bool operator==(const SwDateTime& other) const {
        return time_ == other.time_;
    }

    bool operator!=(const SwDateTime& other) const {
        return !(*this == other);
    }

    bool operator<(const SwDateTime& other) const {
        return time_ < other.time_;
    }

    bool operator<=(const SwDateTime& other) const {
        return time_ <= other.time_;
    }

    bool operator>(const SwDateTime& other) const {
        return time_ > other.time_;
    }

    bool operator>=(const SwDateTime& other) const {
        return time_ >= other.time_;
    }

    // Ajouter ou retirer des jours
    SwDateTime addDays(int days) const {
        return SwDateTime(time_ + days * 24 * 60 * 60);
    }

    SwDateTime subtractDays(int days) const {
        return addDays(-days);
    }

    // Ajouter ou retirer des secondes
    SwDateTime addSeconds(int seconds) const {
        return SwDateTime(time_ + seconds);
    }

    SwDateTime subtractSeconds(int seconds) const {
        return addSeconds(-seconds);
    }

    // Ajouter ou retirer des minutes
    SwDateTime addMinutes(int minutes) const {
        return addSeconds(minutes * 60);
    }

    SwDateTime subtractMinutes(int minutes) const {
        return addMinutes(-minutes);
    }

    // Ajouter ou retirer des mois
    SwDateTime addMonths(int months) const {
        const std::tm& local = localTime();
        int newYear = local.tm_year + 1900 + (local.tm_mon + months) / 12;
        int newMonth = (local.tm_mon + months) % 12;
        if (newMonth < 0) {
            newMonth += 12;
            newYear -= 1;
        }
        int day = (local.tm_mday < daysInMonth(newYear, newMonth + 1))?local.tm_mday : daysInMonth(newYear, newMonth + 1);
        return SwDateTime(newYear, newMonth + 1, day, local.tm_hour, local.tm_min, local.tm_sec);
    }

    SwDateTime subtractMonths(int months) const {
        return addMonths(-months);
    }

    // Ajouter ou retirer des années
    SwDateTime addYears(int years) const {
        return addMonths(years * 12);
    }

    SwDateTime subtractYears(int years) const {
        return addYears(-years);
    }

    static int daysInMonth(int year, int month) {
        static const int daysInMonths[] = { 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31 };

        if ((month < 1) || (month > 12)) {
            throw std::invalid_argument("Invalid month value");
        }

        // Si le mois est février et que l'année est bissextile, renvoyer 29
        if (month == 2 && isLeapYear(year)) {
            return 29;
        }

        // Sinon, renvoyer le nombre standard de jours
        return daysInMonths[month - 1];
    }

    // Vérifie si une année est bissextile
    static bool isLeapYear(int year) {
        return (year % 4 == 0 && year % 100 != 0) || (year % 400 == 0);
    }
private:
    std::time_t time_; // Représentation interne en std::time_t

    // Renvoie un std::tm correspondant au temps local
    const std::tm& localTime() const {
        static thread_local std::tm timeInfo;
#if defined(_WIN32)
        if (localtime_s(&timeInfo, &time_) != 0) {
            throw std::runtime_error("Failed to convert to local time");
        }
#else
        if (!localtime_r(&time_, &timeInfo)) {
            throw std::runtime_error("Failed to convert to local time");
        }
#endif
        return timeInfo;
    }
};
