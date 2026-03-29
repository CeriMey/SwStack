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
#include <sys/inotify.h>
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
        registerFileWatcher_();
    }

    // Arrêter la surveillance
    /**
     * @brief Stops the monitoring managed by the object.
     *
     * @details The call affects the runtime state associated with the underlying resource or service.
     */
    void stopMonitoring() {
        monitoring = false;
        unregisterFileWatcher_();
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
    size_t m_fileWatchToken{0};
#if defined(_WIN32)
    HANDLE m_fileWatchHandle{INVALID_HANDLE_VALUE};
#else
    int m_inotifyFd{-1};
    int m_inotifyWatch{-1};
#endif

    SwString directoryPathForWatch_() const {
        std::string path = filePath_.toStdString();
        std::replace(path.begin(), path.end(), '\\', '/');
        size_t pos = path.find_last_of('/');
        if (pos == std::string::npos) {
            return ".";
        }
        if (pos == 0) {
            return "/";
        }
        return SwString(path.substr(0, pos));
    }

    SwString baseNameForWatch_() const {
        std::string path = filePath_.toStdString();
        std::replace(path.begin(), path.end(), '\\', '/');
        size_t pos = path.find_last_of('/');
        if (pos == std::string::npos) {
            return SwString(path);
        }
        return SwString(path.substr(pos + 1));
    }

    void registerFileWatcher_() {
        unregisterFileWatcher_();
        if (!monitoring || filePath_.isEmpty()) {
            return;
        }

        SwCoreApplication* app = SwCoreApplication::instance(false);
        if (!app) {
            return;
        }

        ThreadHandle* affinity = threadHandle();
        if (!affinity) {
            affinity = ThreadHandle::currentThread();
        }

#if defined(_WIN32)
        const SwString directory = directoryPathForWatch_();
        m_fileWatchHandle = FindFirstChangeNotificationW(
            directory.toStdWString().c_str(),
            FALSE,
            FILE_NOTIFY_CHANGE_FILE_NAME |
            FILE_NOTIFY_CHANGE_LAST_WRITE |
            FILE_NOTIFY_CHANGE_SIZE |
            FILE_NOTIFY_CHANGE_ATTRIBUTES);
        if (m_fileWatchHandle == INVALID_HANDLE_VALUE) {
            return;
        }
        m_fileWatchToken = app->ioDispatcher().watchHandle(
            m_fileWatchHandle,
            [affinity](std::function<void()> task) mutable {
                if (affinity && ThreadHandle::isLive(affinity) &&
                    ThreadHandle::currentThread() != affinity) {
                    affinity->postTask(std::move(task));
                    return;
                }
                task();
            },
            [this]() {
                if (!SwObject::isLive(this) || !monitoring) {
                    return;
                }
                checkFileChanges();
                if (m_fileWatchHandle != INVALID_HANDLE_VALUE) {
                    FindNextChangeNotification(m_fileWatchHandle);
                }
            });
#else
        m_inotifyFd = ::inotify_init1(IN_NONBLOCK | IN_CLOEXEC);
        if (m_inotifyFd < 0) {
            return;
        }
        const SwString directory = directoryPathForWatch_();
        m_inotifyWatch = ::inotify_add_watch(
            m_inotifyFd,
            directory.toStdString().c_str(),
            IN_CLOSE_WRITE | IN_ATTRIB | IN_MODIFY | IN_MOVED_TO | IN_CREATE | IN_DELETE | IN_DELETE_SELF | IN_MOVE_SELF);
        if (m_inotifyWatch < 0) {
            ::close(m_inotifyFd);
            m_inotifyFd = -1;
            return;
        }
        m_fileWatchToken = app->ioDispatcher().watchFd(
            m_inotifyFd,
            SwIoDispatcher::Readable,
            [affinity](std::function<void()> task) mutable {
                if (affinity && ThreadHandle::isLive(affinity) &&
                    ThreadHandle::currentThread() != affinity) {
                    affinity->postTask(std::move(task));
                    return;
                }
                task();
            },
            [this](uint32_t events) {
                if (!SwObject::isLive(this) || !monitoring) {
                    return;
                }
                if (!(events & SwIoDispatcher::Readable)) {
                    return;
                }
                char buffer[4096];
                const ssize_t bytes = ::read(m_inotifyFd, buffer, sizeof(buffer));
                if (bytes <= 0) {
                    return;
                }
                const SwString fileName = baseNameForWatch_();
                bool relevant = false;
                ssize_t pos = 0;
                while (pos < bytes) {
                    const struct inotify_event* ev =
                        reinterpret_cast<const struct inotify_event*>(buffer + pos);
                    if ((ev->mask & (IN_DELETE_SELF | IN_MOVE_SELF)) != 0) {
                        relevant = true;
                    } else if (ev->len > 0 && fileName == SwString(ev->name)) {
                        relevant = true;
                    }
                    pos += static_cast<ssize_t>(sizeof(struct inotify_event) + ev->len);
                }
                if (relevant) {
                    checkFileChanges();
                }
            });
#endif
    }

    void unregisterFileWatcher_() {
        if (m_fileWatchToken) {
            if (SwCoreApplication* app = SwCoreApplication::instance(false)) {
                app->ioDispatcher().remove(m_fileWatchToken);
            }
            m_fileWatchToken = 0;
        }
#if defined(_WIN32)
        if (m_fileWatchHandle != INVALID_HANDLE_VALUE) {
            FindCloseChangeNotification(m_fileWatchHandle);
            m_fileWatchHandle = INVALID_HANDLE_VALUE;
        }
#else
        if (m_inotifyWatch >= 0 && m_inotifyFd >= 0) {
            ::inotify_rm_watch(m_inotifyFd, m_inotifyWatch);
            m_inotifyWatch = -1;
        }
        if (m_inotifyFd >= 0) {
            ::close(m_inotifyFd);
            m_inotifyFd = -1;
        }
#endif
    }

};
