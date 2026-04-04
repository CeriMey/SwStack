inline SwString SwEmbeddedDb::notificationObjectName_() const {
    return SwString("db_") + SwString::number(swEmbeddedDbDetail::stableHash64_(dbPath_), 16);
}

inline void SwEmbeddedDb::setupShmNotifications_() {
    teardownShmNotifications_();
    if (!options_.enableShmNotifications || dbPath_.isEmpty()) {
        return;
    }

    try {
        shmRegistry_.reset(new sw::ipc::Registry("sw_embedded_db", notificationObjectName_()));
        shmNotification_.reset(
            new sw::ipc::Signal<unsigned long long, unsigned long long>(*shmRegistry_, "__changes__"));
        if (options_.readOnly) {
            shmNotificationSub_ = shmNotification_->connect(
                [this](unsigned long long, unsigned long long) {
                    this->shmRefreshHint_.store(true, std::memory_order_release);
                },
                false);
        }
    } catch (const std::exception& e) {
        swCError(kSwLogCategory_SwEmbeddedDb) << "failed to initialize SHM notifications:" << e.what();
        teardownShmNotifications_();
    }
}

inline void SwEmbeddedDb::teardownShmNotifications_() {
    shmNotificationSub_.stop();
    shmNotification_.reset();
    shmRegistry_.reset();
    shmRefreshHint_.store(false, std::memory_order_release);
}

inline void SwEmbeddedDb::publishShmNotificationLocked_() {
    if (!options_.enableShmNotifications || !shmNotification_) {
        return;
    }
    (void)shmNotification_->publish(lastVisibleSequence_, manifest_.manifestId);
}

inline void SwEmbeddedDb::maybeRefreshFromNotifications_() {
    if (!opened_.load(std::memory_order_acquire) ||
        !options_.readOnly ||
        !options_.enableShmNotifications ||
        !shmNotification_) {
        return;
    }

    bool hinted = shmRefreshHint_.exchange(false, std::memory_order_acq_rel);
    unsigned long long publishedSequence = 0;
    unsigned long long publishedManifestId = 0;
    if (!shmNotification_->readLatest(publishedSequence, publishedManifestId)) {
        if (!hinted) {
            return;
        }
    } else {
        std::lock_guard<std::mutex> lock(mutex_);
        hinted = hinted ||
                 publishedSequence > lastVisibleSequence_ ||
                 publishedManifestId > manifest_.manifestId;
    }

    if (!hinted) {
        return;
    }

    const SwDbStatus status = refresh();
    if (!status.ok()) {
        shmRefreshHint_.store(true, std::memory_order_release);
    }
}

