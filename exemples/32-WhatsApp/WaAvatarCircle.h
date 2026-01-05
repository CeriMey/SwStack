#pragma once

#include "SwWidget.h"

#include "Sw.h"
#include "SwString.h"

class WaAvatarCircle final : public SwWidget {
    SW_OBJECT(WaAvatarCircle, SwWidget)

public:
    WaAvatarCircle(const SwString& initial, const SwColor& color, SwWidget* parent = nullptr);

    void setInitial(const SwString& initial);
    void setColor(const SwColor& color);

protected:
    void paintEvent(PaintEvent* event) override;

private:
    SwString m_initial;
    SwColor m_color{100, 116, 139};
};

