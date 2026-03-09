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

/**
 * @file src/core/types/SwDateTime.h
 * @ingroup core_types
 * @brief Declares the public interface exposed by SwDateTime in the CoreSw fundamental types
 * layer.
 *
 * This header belongs to the CoreSw fundamental types layer. It provides value types, containers,
 * text and binary helpers, and lightweight serialization primitives shared across the stack.
 *
 * Within that layer, this file focuses on the date time interface. The declarations exposed here
 * define the stable surface that adjacent code can rely on while the implementation remains free
 * to evolve behind the header.
 *
 * The main declarations in this header are SwDateTime.
 *
 * The declarations in this header are intended to make the subsystem boundary explicit: callers
 * interact with stable types and functions, while implementation details remain confined to
 * source files and private helpers.
 *
 * Interfaces in this area are intentionally reused by runtime, IO, GUI, remote, and media modules
 * to keep cross-module semantics consistent.
 *
 */


#include <ctime>
#include <string>
#include <stdexcept>
#include <sstream>
#include <iomanip>
#include <math.h>
class SwDateTime {
public:
    // Constructeurs
    /**
     * @brief Constructs a `SwDateTime` instance.
     *
     * @details The instance is initialized and prepared for immediate use.
     */
    SwDateTime() : time_(std::time(nullptr)) {} // Date/heure actuelle

    /**
     * @brief Constructs a `SwDateTime` instance.
     * @param time Value passed to the method.
     *
     * @details The instance is initialized and prepared for immediate use.
     */
    SwDateTime(std::time_t time) : time_(time) {} // Constructeur avec std::time_t

    /**
     * @brief Constructs a `SwDateTime` instance.
     * @param year Value passed to the method.
     * @param month Value passed to the method.
     * @param day Value passed to the method.
     * @param hour Value passed to the method.
     * @param minute Value passed to the method.
     * @param second Value passed to the method.
     *
     * @details The instance is initialized and prepared for immediate use.
     */
    SwDateTime(int year, int month, int day, int hour = 0, int minute = 0, int second = 0) {
        setDateTime(year, month, day, hour, minute, second);
    }

    // Constructeur de copie
    /**
     * @brief Constructs a `SwDateTime` instance.
     * @param other Value passed to the method.
     *
     * @details The instance is initialized and prepared for immediate use.
     */
    SwDateTime(const SwDateTime& other) = default;

    // Constructeur de déplacement
    /**
     * @brief Constructs a `SwDateTime` instance.
     * @param other Value passed to the method.
     *
     * @details The instance is initialized and prepared for immediate use.
     */
    SwDateTime(SwDateTime&& other) noexcept = default;

    // Opérateur d'affectation (copie)
    /**
     * @brief Performs the `operator=` operation.
     * @param other Value passed to the method.
     * @return The requested operator =.
     */
    SwDateTime& operator=(const SwDateTime& other) = default;

    // Opérateur d'affectation (déplacement)
    /**
     * @brief Performs the `operator=` operation.
     * @param other Value passed to the method.
     * @return The requested operator =.
     */
    SwDateTime& operator=(SwDateTime&& other) noexcept = default;

    // Destructeur
    /**
     * @brief Destroys the `SwDateTime` instance.
     *
     * @details Use this hook to release any resources that remain associated with the instance.
     */
    ~SwDateTime() = default;


    /**
     * @brief Returns the current time t.
     * @return The current time t.
     *
     * @details The returned value reflects the state currently stored by the instance.
     */
    operator std::time_t() const {
        return time_;
    }

    /**
     * @brief Returns the current time t&.
     * @return The current time t&.
     *
     * @details The returned value reflects the state currently stored by the instance.
     */
    operator std::time_t&() {
        return time_;
    }

    /**
     * @brief Returns the current time t&.
     * @return The current time t&.
     *
     * @details The returned value reflects the state currently stored by the instance.
     */
    operator const std::time_t&() const {
        return time_;
    }

    // Setter avec des champs spécifiques
    /**
     * @brief Sets the date Time.
     * @param year Value passed to the method.
     * @param month Value passed to the method.
     * @param day Value passed to the method.
     * @param hour Value passed to the method.
     * @param minute Value passed to the method.
     * @param second Value passed to the method.
     *
     * @details Call this method to replace the currently stored value with the caller-provided one.
     */
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
    /**
     * @brief Returns the current to Time T.
     * @return The current to Time T.
     *
     * @details The returned value reflects the state currently stored by the instance.
     */
    std::time_t toTimeT() const {
        return time_;
    }

    // Accès aux composants
    /**
     * @brief Performs the `year` operation.
     * @return The requested year.
     */
    int year() const { return localTime().tm_year + 1900; }
    /**
     * @brief Performs the `month` operation.
     * @return The requested month.
     */
    int month() const { return localTime().tm_mon + 1; }
    /**
     * @brief Performs the `day` operation.
     * @return The requested day.
     */
    int day() const { return localTime().tm_mday; }
    /**
     * @brief Performs the `hour` operation.
     * @return The requested hour.
     */
    int hour() const { return localTime().tm_hour; }
    /**
     * @brief Performs the `minute` operation.
     * @return The requested minute.
     */
    int minute() const { return localTime().tm_min; }
    /**
     * @brief Performs the `second` operation.
     * @return The requested second.
     */
    int second() const { return localTime().tm_sec; }

    // Représentation sous forme de chaîne (ISO 8601)
    /**
     * @brief Returns the current to String.
     * @return The current to String.
     *
     * @details The returned value reflects the state currently stored by the instance.
     */
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
    /**
     * @brief Performs the `operator==` operation.
     * @param other Value passed to the method.
     * @return `true` on success; otherwise `false`.
     */
    bool operator==(const SwDateTime& other) const {
        return time_ == other.time_;
    }

    /**
     * @brief Performs the `operator!=` operation.
     * @param other Value passed to the method.
     * @return `true` on success; otherwise `false`.
     */
    bool operator!=(const SwDateTime& other) const {
        return !(*this == other);
    }

    /**
     * @brief Performs the `operator<` operation.
     * @param other Value passed to the method.
     * @return `true` on success; otherwise `false`.
     */
    bool operator<(const SwDateTime& other) const {
        return time_ < other.time_;
    }

    /**
     * @brief Performs the `operator<=` operation.
     * @param other Value passed to the method.
     * @return `true` on success; otherwise `false`.
     */
    bool operator<=(const SwDateTime& other) const {
        return time_ <= other.time_;
    }

    /**
     * @brief Performs the `operator>` operation.
     * @param other Value passed to the method.
     * @return `true` on success; otherwise `false`.
     */
    bool operator>(const SwDateTime& other) const {
        return time_ > other.time_;
    }

    /**
     * @brief Performs the `operator>=` operation.
     * @param other Value passed to the method.
     * @return `true` on success; otherwise `false`.
     */
    bool operator>=(const SwDateTime& other) const {
        return time_ >= other.time_;
    }

    // Ajouter ou retirer des jours
    /**
     * @brief Adds the specified days.
     * @param days Value passed to the method.
     * @return The requested days.
     */
    SwDateTime addDays(int days) const {
        return SwDateTime(time_ + days * 24 * 60 * 60);
    }

    /**
     * @brief Performs the `subtractDays` operation.
     * @param days Value passed to the method.
     * @return The requested subtract Days.
     */
    SwDateTime subtractDays(int days) const {
        return addDays(-days);
    }

    // Ajouter ou retirer des secondes
    /**
     * @brief Adds the specified seconds.
     * @param seconds Value passed to the method.
     * @return The requested seconds.
     */
    SwDateTime addSeconds(int seconds) const {
        return SwDateTime(time_ + seconds);
    }

    /**
     * @brief Performs the `subtractSeconds` operation.
     * @param seconds Value passed to the method.
     * @return The requested subtract Seconds.
     */
    SwDateTime subtractSeconds(int seconds) const {
        return addSeconds(-seconds);
    }

    // Ajouter ou retirer des minutes
    /**
     * @brief Adds the specified minutes.
     * @param minutes Value passed to the method.
     * @return The requested minutes.
     */
    SwDateTime addMinutes(int minutes) const {
        return addSeconds(minutes * 60);
    }

    /**
     * @brief Performs the `subtractMinutes` operation.
     * @param minutes Value passed to the method.
     * @return The requested subtract Minutes.
     */
    SwDateTime subtractMinutes(int minutes) const {
        return addMinutes(-minutes);
    }

    // Ajouter ou retirer des mois
    /**
     * @brief Adds the specified months.
     * @param months Value passed to the method.
     * @return The requested months.
     */
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

    /**
     * @brief Performs the `subtractMonths` operation.
     * @param months Value passed to the method.
     * @return The requested subtract Months.
     */
    SwDateTime subtractMonths(int months) const {
        return addMonths(-months);
    }

    // Ajouter ou retirer des années
    /**
     * @brief Adds the specified years.
     * @param years Value passed to the method.
     * @return The requested years.
     */
    SwDateTime addYears(int years) const {
        return addMonths(years * 12);
    }

    /**
     * @brief Performs the `subtractYears` operation.
     * @param years Value passed to the method.
     * @return The requested subtract Years.
     */
    SwDateTime subtractYears(int years) const {
        return addYears(-years);
    }

    /**
     * @brief Performs the `daysInMonth` operation.
     * @param year Value passed to the method.
     * @param month Value passed to the method.
     * @return The requested days In Month.
     */
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
    /**
     * @brief Returns whether the object reports leap Year.
     * @param year Value passed to the method.
     * @return The requested leap Year.
     *
     * @details This query does not modify the object state.
     */
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
