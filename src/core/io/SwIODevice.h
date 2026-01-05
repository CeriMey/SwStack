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
public:
    enum OpenModeFlag {
        NotOpen = 0x0,
        Read = 0x1,
        Write = 0x2,
        ReadWrite = Read | Write
    };

    SwIODevice(SwObject* parent = nullptr) : SwObject(parent), monitoring(false), m_timerDercriptor(new SwTimer(100, this)){
    
        connect(m_timerDercriptor, SIGNAL(timeout), this, &SwIODevice::onTimerDescriptor);
    }

    virtual ~SwIODevice() {
        // m_timerDercriptor has `this` as parent, SwObject will delete it.
        m_timerDercriptor->stop();
    }

    virtual bool open(typename SwIODescriptor::Descriptor hFile) {
        SW_UNUSED(hFile)
        return false;
    }

    virtual void close() {
    }

    virtual SwString read(int64_t maxSize = 0) {
        SW_UNUSED(maxSize)
        return "";
    }

    virtual bool write(const SwString& data) {
        SW_UNUSED(data)
        return false;
    }

    virtual bool write(const SwByteArray& data) {
        return write(SwString(data.toStdString()));
    }

    virtual bool isOpen() const {
        return false;
    }

    bool exists() const {
        return exists(filePath_);
    }

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
    void startMonitoring() {
        monitoring = true;
        updateLastWriteTime();
        m_timerDercriptor->start();
    }

    // Arrêter la surveillance
    void stopMonitoring() {
        monitoring = false;
        m_timerDercriptor->stop();
    }

signals:
    DECLARE_SIGNAL_VOID(readyRead);
    DECLARE_SIGNAL_VOID(readyWrite);

protected:
    SwTimer* m_timerDercriptor;
   

    void addDescriptor(SwIODescriptor* descriptor) {
        if (descriptor && !descriptors_.contains(descriptor)) {
            descriptors_.append(descriptor);
        }
    }

    void removeDescriptor(SwIODescriptor*& descriptor) {
          if (!descriptor) return;
          if (descriptors_.removeOne(descriptor)) {
              safeDelete(descriptor);
          }
      }

    size_t getDescriptorCount() const {
        return descriptors_.size();
    }

protected slots:
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
