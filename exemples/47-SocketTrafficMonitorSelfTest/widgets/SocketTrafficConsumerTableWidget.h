#pragma once

#include "SwStandardItemModel.h"
#include "SwTableView.h"
#include "SwWidget.h"
#include "SocketTrafficViewTypes.h"

class SocketTrafficConsumerTableWidget : public SwWidget {
    SW_OBJECT(SocketTrafficConsumerTableWidget, SwWidget)

public:
    explicit SocketTrafficConsumerTableWidget(SwWidget* parent = nullptr);

    virtual SwSize minimumSizeHint() const override;

    void rebuild(const SwList<SwSocketTrafficTelemetryConsumerSnapshot>& consumers,
                 unsigned long long preferredConsumerId,
                 bool followTop);
    void clearEntries();
    unsigned long long currentSelectedConsumerId() const;
    bool isTopSelected() const;
    bool isPinnedToTop() const;

signals:
    DECLARE_SIGNAL(currentConsumerChanged, unsigned long long)

private:
    void resetModelColumns_();
    int rowForConsumerId_(unsigned long long consumerId) const;
    void selectRow_(int row);
    void restoreViewport_(unsigned long long anchorConsumerId, int rowOffsetWithinAnchor, bool followTop);

    SwTableView* tableView_{nullptr};
    SwStandardItemModel* tableModel_{nullptr};
    SwList<unsigned long long> consumerIds_;
    bool suppressSelectionSignal_{false};
};
