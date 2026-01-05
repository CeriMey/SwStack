#pragma once
/***************************************************************************************************
 * SwCreatorSystemDragDrop - OS drag&drop integration (Creator).
 *
 * Windows:
 * - Uses OLE DoDragDrop + RegisterDragDrop to let the system manage drag cursors and drop routing.
 *
 * Other platforms:
 * - Currently unsupported (callers should fall back to the in-app SwDragDrop overlay).
 **************************************************************************************************/

#include "SwString.h"
#include "SwWidgetPlatformAdapter.h"

class SwCreatorSystemDragDrop {
public:
    struct Payload {
        SwString className;
        bool isLayout{false};
    };

    class DropHandler {
    public:
        virtual ~DropHandler() = default;
        virtual bool canAcceptDrop(const Payload& payload, int clientX, int clientY) = 0;
        virtual void onDragOver(const Payload& payload, int clientX, int clientY) = 0;
        virtual void onDragLeave() = 0;
        virtual void onDrop(const Payload& payload, int clientX, int clientY) = 0;
    };

    class Registration {
    public:
        Registration() = default;
        ~Registration();
        Registration(const Registration&) = delete;
        Registration& operator=(const Registration&) = delete;
        Registration(Registration&& other) noexcept;
        Registration& operator=(Registration&& other) noexcept;

        bool isValid() const;

    private:
        friend class SwCreatorSystemDragDrop;
        void reset_();
        void* m_dropTarget{nullptr};
        SwWidgetPlatformHandle m_windowHandle{};
    };

    static bool startDrag(const Payload& payload);
    static Registration registerDropTarget(const SwWidgetPlatformHandle& windowHandle, DropHandler* handler);
};

