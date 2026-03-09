#pragma once

/**
 * @file src/core/io/SwIODevice.h
 * @ingroup core_io
 * @brief Declares the public interface exposed by SwIODevice in the CoreSw IO layer.
 *
 * This header belongs to the CoreSw IO layer. It defines files, sockets, servers, descriptors,
 * processes, and network helpers that sit directly at operating-system boundaries.
 *
 * Within that layer, this file focuses on the IO device interface. The declarations exposed here
 * define the stable surface that adjacent code can rely on while the implementation remains free
 * to evolve behind the header.
 *
 * The main declarations in this header are SwIODevice.
 *
 * The declarations in this header are intended to make the subsystem boundary explicit: callers
 * interact with stable types and functions, while implementation details remain confined to
 * source files and private helpers.
 *
 * IO-facing declarations here usually manage handles, readiness state, buffering, and error
 * propagation while presenting a portable framework API.
 *
 */

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

#if defined(_WIN32)
#include "platform/win/SwWindows.h"
#else
#include <sys/stat.h>
#include <unistd.h>
#include <ctime>
#endif
#include "SwObject.h"
#include "SwIODescriptor.h"
#include "SwTimer.h"
#include "SwString.h"
#include "SwByteArray.h"

class SwIODevice : public SwObject {
    SW_OBJECT(SwIODevice, SwObject)
public:
    enum OpenModeFlag {
        NotOpen = 0x0,
        Read = 0x1,
        Write = 0x2,
        ReadWrite = Read | Write
    };

    /**
     * @brief Constructs a `SwIODevice` instance.
     * @param parent Optional parent object that owns this instance.
     *
     * @details The instance is initialized and can optionally be attached to a parent object for ownership management.
     */
    SwIODevice(SwObject* parent = nullptr) : SwObject(parent), monitoring(false), m_timerDercriptor(new SwTimer(100, this)){
    
        connect(m_timerDercriptor, &SwTimer::timeout, this, &SwIODevice::onTimerDescriptor);
    }

    /**
     * @brief Destroys the `SwIODevice` instance.
     *
     * @details Use this hook to release any resources that remain associated with the instance.
     */
    virtual ~SwIODevice() {
        // m_timerDercriptor has `this` as parent, SwObject will delete it.
        m_timerDercriptor->stop();
    }

    /**
     * @brief Opens the underlying resource managed by the object.
     * @param hFile Value passed to the method.
     * @return The requested open.
     *
     * @details The call affects the runtime state associated with the underlying resource or service.
     */
    virtual bool open(typename SwIODescriptor::Descriptor hFile) {
        SW_UNUSED(hFile)
        return false;
    }

    /**
     * @brief Closes the underlying resource and stops active work.
     * @return The current close.
     *
     * @details The returned value reflects the state currently stored by the instance.
     */
    virtual void close() {
    }

    /**
     * @brief Performs the `read` operation on the associated resource.
     * @param maxSize Value passed to the method.
     * @return The resulting read.
     */
    virtual SwString read(int64_t maxSize = 0) {
        SW_UNUSED(maxSize)
        return "";
    }

    /**
     * @brief Performs the `write` operation on the associated resource.
     * @param data Value passed to the method.
     * @return The requested write.
     */
    virtual bool write(const SwString& data) {
        SW_UNUSED(data)
        return false;
    }

    /**
     * @brief Performs the `write` operation on the associated resource.
     * @param data Value passed to the method.
     * @return The requested write.
     */
    virtual bool write(const SwByteArray& data) {
        return write(SwString(data.toStdString()));
    }

    /**
     * @brief Returns whether the object reports open.
     * @return The current open.
     *
     * @details The returned value reflects the state currently stored by the instance.
     */
    virtual bool isOpen() const {
        return false;
    }

    /**
     * @brief Returns the current exists.
     * @return `true` on success; otherwise `false`.
     *
     * @details The returned value reflects the state currently stored by the instance.
     */
    bool exists() const {
        return exists(filePath_);
    }

    /**
     * @brief Performs the `exists` operation.
     * @param path Path used by the operation.
     * @return The requested exists.
     */
    static bool exists(const SwString& path) {
        if (path.isEmpty()) {
            return false;
        }
#if defined(_WIN32)
        DWORD attributes = GetFileAttributesW(path.toStdWString().c_str());
        return (attributes != INVALID_FILE_ATTRIBUTES);
#else
        struct stat info;
        return stat(path.toStdString().c_str(), &info) == 0;
#endif
    }

    // Démarrer la surveillance
    /**
     * @brief Starts the monitoring managed by the object.
     *
     * @details The call affects the runtime state associated with the underlying resource or service.
     */
    void startMonitoring() {
        monitoring = true;
        updateLastWriteTime();
        m_timerDercriptor->start();
    }

    // Arrêter la surveillance
    /**
     * @brief Stops the monitoring managed by the object.
     *
     * @details The call affects the runtime state associated with the underlying resource or service.
     */
    void stopMonitoring() {
        monitoring = false;
        m_timerDercriptor->stop();
    }

signals:
    DECLARE_SIGNAL_VOID(readyRead);
    DECLARE_SIGNAL_VOID(readyWrite);

protected:
    SwTimer* m_timerDercriptor;
   

    /**
     * @brief Adds the specified descriptor.
     * @param descriptor Value passed to the method.
     */
    void addDescriptor(SwIODescriptor* descriptor) {
        if (descriptor && !descriptors_.contains(descriptor)) {
            descriptors_.append(descriptor);
        }
    }

    /**
     * @brief Removes the specified descriptor.
     * @param descriptor Value passed to the method.
     */
    void removeDescriptor(SwIODescriptor*& descriptor) {
          if (!descriptor) return;
          if (descriptors_.removeOne(descriptor)) {
              safeDelete(descriptor);
          }
      }

    /**
     * @brief Returns the current descriptor Count.
     * @return The current descriptor Count.
     *
     * @details The returned value reflects the state currently stored by the instance.
     */
    size_t getDescriptorCount() const {
        return descriptors_.size();
    }

protected slots:
    /**
     * @brief Returns the current on Timer Descriptor.
     * @return The current on Timer Descriptor.
     *
     * @details The returned value reflects the state currently stored by the instance.
     */
    virtual void onTimerDescriptor() {
        if (monitoring) {
            checkFileChanges();
        }

        bool readyToRead = false, readyToWrite = false;

        for (auto descriptor : descriptors_) {
            if (descriptor->waitForEvent(readyToRead, readyToWrite, 1)) {
                if (readyToRead) {
                    readyRead();
                    emitSignal("readyRead" + descriptor->descriptorName());
                }
                if (readyToWrite) {
                    readyWrite();
                    emitSignal("readyWrite" + descriptor->descriptorName());
                }
            }
        }
    }

protected:
    SwString filePath_;
#if defined(_WIN32)
    FILETIME lastWriteTime_;
#else
    std::time_t lastWriteTime_{0};
#endif

    /**
     * @brief Performs the `checkFileChanges` operation.
     */
    void checkFileChanges() {
        if (filePath_.isEmpty() || !exists()) {
            return;
        }

#if defined(_WIN32)
        WIN32_FILE_ATTRIBUTE_DATA fileInfo;
        if (GetFileAttributesExW(filePath_.toStdWString().c_str(), GetFileExInfoStandard, &fileInfo)) {
            if (CompareFileTime(&fileInfo.ftLastWriteTime, &lastWriteTime_) != 0) {
                lastWriteTime_ = fileInfo.ftLastWriteTime;
                emitSignal("fileChanged", filePath_);
            }
        }
#else
        struct stat info;
        if (stat(filePath_.toStdString().c_str(), &info) == 0) {
            if (info.st_mtime != lastWriteTime_) {
                lastWriteTime_ = info.st_mtime;
                emitSignal("fileChanged", filePath_);
            }
        }
#endif
    }

    // Mettre à jour l'horodatage de dernière modification
    /**
     * @brief Updates the last Write Time managed by the object.
     */
    void updateLastWriteTime() {
        if (exists()) {
#if defined(_WIN32)
            WIN32_FILE_ATTRIBUTE_DATA fileInfo;
            if (GetFileAttributesExW(filePath_.toStdWString().c_str(), GetFileExInfoStandard, &fileInfo)) {
                lastWriteTime_ = fileInfo.ftLastWriteTime;
            }
#else
            struct stat info;
            if (stat(filePath_.toStdString().c_str(), &info) == 0) {
                lastWriteTime_ = info.st_mtime;
            }
#endif
        }
    }

private:
    SwList<SwIODescriptor*> descriptors_;
    bool monitoring;

};
