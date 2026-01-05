#include "SwCreatorSystemDragDrop.h"

#if defined(_WIN32)
#include "platform/win/SwWindows.h"

#include <Ole2.h>

#include <atomic>
#include <cstring>
#include <string>
#include <utility>
#endif

#if defined(_WIN32)
namespace {
bool ensureOleInitialized_() {
    thread_local bool attempted = false;
    thread_local bool ok = false;
    if (attempted) {
        return ok;
    }
    attempted = true;
    const HRESULT hr = ::OleInitialize(nullptr);
    ok = SUCCEEDED(hr) && hr != RPC_E_CHANGED_MODE;
    return ok;
}

UINT widgetClassFormatId_() {
    static UINT s_id = RegisterClipboardFormatW(L"SwCreator.WidgetClass");
    return s_id;
}

UINT layoutClassFormatId_() {
    static UINT s_id = RegisterClipboardFormatW(L"SwCreator.LayoutClass");
    return s_id;
}

class SingleFormatEnumerator final : public IEnumFORMATETC {
public:
    explicit SingleFormatEnumerator(const FORMATETC& fmt)
        : m_fmt(fmt) {}

    HRESULT __stdcall QueryInterface(REFIID riid, void** ppvObject) override {
        if (!ppvObject) {
            return E_POINTER;
        }
        *ppvObject = nullptr;
        if (riid == IID_IUnknown || riid == IID_IEnumFORMATETC) {
            *ppvObject = static_cast<IEnumFORMATETC*>(this);
            AddRef();
            return S_OK;
        }
        return E_NOINTERFACE;
    }

    ULONG __stdcall AddRef() override { return ++m_ref; }

    ULONG __stdcall Release() override {
        const ULONG next = --m_ref;
        if (next == 0) {
            delete this;
        }
        return next;
    }

    HRESULT __stdcall Next(ULONG celt, FORMATETC* rgelt, ULONG* pceltFetched) override {
        if (!rgelt) {
            return E_POINTER;
        }
        if (celt != 1 && !pceltFetched) {
            return E_INVALIDARG;
        }

        ULONG fetched = 0;
        if (celt == 0) {
            if (pceltFetched) {
                *pceltFetched = 0;
            }
            return S_OK;
        }
        if (m_done) {
            if (pceltFetched) {
                *pceltFetched = 0;
            }
            return S_FALSE;
        }

        rgelt[0] = m_fmt;
        if (pceltFetched) {
            *pceltFetched = 1;
        }
        fetched = 1;
        m_done = true;
        return (fetched == celt) ? S_OK : S_FALSE;
    }

    HRESULT __stdcall Skip(ULONG celt) override {
        if (celt == 0) {
            return S_OK;
        }
        if (m_done) {
            return S_FALSE;
        }
        m_done = true;
        return (celt == 1) ? S_OK : S_FALSE;
    }

    HRESULT __stdcall Reset() override {
        m_done = false;
        return S_OK;
    }

    HRESULT __stdcall Clone(IEnumFORMATETC** ppEnum) override {
        if (!ppEnum) {
            return E_POINTER;
        }
        auto* e = new SingleFormatEnumerator(m_fmt);
        e->m_done = m_done;
        *ppEnum = e;
        return S_OK;
    }

private:
    std::atomic<ULONG> m_ref{1};
    FORMATETC m_fmt{};
    bool m_done{false};
};

class CreatorDataObject final : public IDataObject {
public:
    CreatorDataObject(UINT formatId, std::wstring text)
        : m_formatId(formatId)
        , m_text(std::move(text)) {}

    HRESULT __stdcall QueryInterface(REFIID riid, void** ppvObject) override {
        if (!ppvObject) {
            return E_POINTER;
        }
        *ppvObject = nullptr;
        if (riid == IID_IUnknown || riid == IID_IDataObject) {
            *ppvObject = static_cast<IDataObject*>(this);
            AddRef();
            return S_OK;
        }
        return E_NOINTERFACE;
    }

    ULONG __stdcall AddRef() override { return ++m_ref; }

    ULONG __stdcall Release() override {
        const ULONG next = --m_ref;
        if (next == 0) {
            delete this;
        }
        return next;
    }

    HRESULT __stdcall GetData(FORMATETC* pFormatEtc, STGMEDIUM* pMedium) override {
        if (!pFormatEtc || !pMedium) {
            return E_POINTER;
        }
        if (pFormatEtc->cfFormat != m_formatId) {
            return DV_E_FORMATETC;
        }
        if ((pFormatEtc->tymed & TYMED_HGLOBAL) == 0) {
            return DV_E_TYMED;
        }
        if (pFormatEtc->dwAspect != DVASPECT_CONTENT) {
            return DV_E_DVASPECT;
        }

        const size_t bytes = (m_text.size() + 1) * sizeof(wchar_t);
        HGLOBAL h = GlobalAlloc(GMEM_MOVEABLE, bytes);
        if (!h) {
            return STG_E_MEDIUMFULL;
        }
        void* mem = GlobalLock(h);
        if (!mem) {
            GlobalFree(h);
            return STG_E_MEDIUMFULL;
        }
        std::memcpy(mem, m_text.c_str(), bytes);
        GlobalUnlock(h);

        pMedium->tymed = TYMED_HGLOBAL;
        pMedium->hGlobal = h;
        pMedium->pUnkForRelease = nullptr;
        return S_OK;
    }

    HRESULT __stdcall GetDataHere(FORMATETC*, STGMEDIUM*) override { return E_NOTIMPL; }

    HRESULT __stdcall QueryGetData(FORMATETC* pFormatEtc) override {
        if (!pFormatEtc) {
            return E_POINTER;
        }
        if (pFormatEtc->cfFormat != m_formatId) {
            return DV_E_FORMATETC;
        }
        if ((pFormatEtc->tymed & TYMED_HGLOBAL) == 0) {
            return DV_E_TYMED;
        }
        if (pFormatEtc->dwAspect != DVASPECT_CONTENT) {
            return DV_E_DVASPECT;
        }
        return S_OK;
    }

    HRESULT __stdcall GetCanonicalFormatEtc(FORMATETC*, FORMATETC* pFormatEtcOut) override {
        if (!pFormatEtcOut) {
            return E_POINTER;
        }
        pFormatEtcOut->ptd = nullptr;
        return E_NOTIMPL;
    }

    HRESULT __stdcall SetData(FORMATETC*, STGMEDIUM*, BOOL) override { return E_NOTIMPL; }

    HRESULT __stdcall EnumFormatEtc(DWORD dwDirection, IEnumFORMATETC** ppEnumFormatEtc) override {
        if (!ppEnumFormatEtc) {
            return E_POINTER;
        }
        *ppEnumFormatEtc = nullptr;
        if (dwDirection != DATADIR_GET) {
            return E_NOTIMPL;
        }

        FORMATETC fmt{};
        fmt.cfFormat = static_cast<CLIPFORMAT>(m_formatId);
        fmt.dwAspect = DVASPECT_CONTENT;
        fmt.lindex = -1;
        fmt.tymed = TYMED_HGLOBAL;
        fmt.ptd = nullptr;
        *ppEnumFormatEtc = new SingleFormatEnumerator(fmt);
        return S_OK;
    }

    HRESULT __stdcall DAdvise(FORMATETC*, DWORD, IAdviseSink*, DWORD*) override { return OLE_E_ADVISENOTSUPPORTED; }
    HRESULT __stdcall DUnadvise(DWORD) override { return OLE_E_ADVISENOTSUPPORTED; }
    HRESULT __stdcall EnumDAdvise(IEnumSTATDATA**) override { return OLE_E_ADVISENOTSUPPORTED; }

private:
    std::atomic<ULONG> m_ref{1};
    UINT m_formatId{0};
    std::wstring m_text;
};

class CreatorDropSource final : public IDropSource {
public:
    HRESULT __stdcall QueryInterface(REFIID riid, void** ppvObject) override {
        if (!ppvObject) {
            return E_POINTER;
        }
        *ppvObject = nullptr;
        if (riid == IID_IUnknown || riid == IID_IDropSource) {
            *ppvObject = static_cast<IDropSource*>(this);
            AddRef();
            return S_OK;
        }
        return E_NOINTERFACE;
    }

    ULONG __stdcall AddRef() override { return ++m_ref; }

    ULONG __stdcall Release() override {
        const ULONG next = --m_ref;
        if (next == 0) {
            delete this;
        }
        return next;
    }

    HRESULT __stdcall QueryContinueDrag(BOOL fEscapePressed, DWORD grfKeyState) override {
        if (fEscapePressed) {
            return DRAGDROP_S_CANCEL;
        }
        if ((grfKeyState & MK_LBUTTON) == 0) {
            return DRAGDROP_S_DROP;
        }
        return S_OK;
    }

    HRESULT __stdcall GiveFeedback(DWORD) override { return DRAGDROP_S_USEDEFAULTCURSORS; }

private:
    std::atomic<ULONG> m_ref{1};
};

bool extractPayload_(IDataObject* data, SwCreatorSystemDragDrop::Payload& outPayload) {
    if (!data) {
        return false;
    }

    struct Candidate {
        UINT formatId;
        bool isLayout;
    };
    const Candidate candidates[] = {
        {widgetClassFormatId_(), false},
        {layoutClassFormatId_(), true},
    };

    for (const Candidate& c : candidates) {
        if (!c.formatId) {
            continue;
        }

        FORMATETC fmt{};
        fmt.cfFormat = static_cast<CLIPFORMAT>(c.formatId);
        fmt.dwAspect = DVASPECT_CONTENT;
        fmt.lindex = -1;
        fmt.tymed = TYMED_HGLOBAL;
        fmt.ptd = nullptr;

        if (FAILED(data->QueryGetData(&fmt))) {
            continue;
        }

        STGMEDIUM stg{};
        const HRESULT hr = data->GetData(&fmt, &stg);
        if (FAILED(hr)) {
            continue;
        }

        bool ok = false;
        if (stg.tymed == TYMED_HGLOBAL && stg.hGlobal) {
            const void* mem = GlobalLock(stg.hGlobal);
            if (mem) {
                const wchar_t* w = static_cast<const wchar_t*>(mem);
                outPayload.className = SwString::fromWCharArray(w);
                outPayload.isLayout = c.isLayout;
                GlobalUnlock(stg.hGlobal);
                ok = !outPayload.className.isEmpty();
            }
        }
        ReleaseStgMedium(&stg);

        if (ok) {
            return true;
        }
    }

    return false;
}

class CreatorDropTarget final : public IDropTarget {
public:
    explicit CreatorDropTarget(SwCreatorSystemDragDrop::DropHandler* handler)
        : m_handler(handler) {}

    HRESULT __stdcall QueryInterface(REFIID riid, void** ppvObject) override {
        if (!ppvObject) {
            return E_POINTER;
        }
        *ppvObject = nullptr;
        if (riid == IID_IUnknown || riid == IID_IDropTarget) {
            *ppvObject = static_cast<IDropTarget*>(this);
            AddRef();
            return S_OK;
        }
        return E_NOINTERFACE;
    }

    ULONG __stdcall AddRef() override { return ++m_ref; }

    ULONG __stdcall Release() override {
        const ULONG next = --m_ref;
        if (next == 0) {
            delete this;
        }
        return next;
    }

    HRESULT __stdcall DragEnter(IDataObject* pDataObj, DWORD, POINTL pt, DWORD* pdwEffect) override {
        if (pdwEffect) {
            *pdwEffect = DROPEFFECT_NONE;
        }
        if (!m_handler) {
            return S_OK;
        }

        m_hasPayload = extractPayload_(pDataObj, m_payload);
        if (!m_hasPayload) {
            m_overAccepted = false;
            m_handler->onDragLeave();
            return S_OK;
        }

        POINT p{pt.x, pt.y};
        if (!toClientPoint_(p)) {
            m_overAccepted = false;
            m_handler->onDragLeave();
            return S_OK;
        }

        const bool accept = m_handler->canAcceptDrop(m_payload, p.x, p.y);
        m_overAccepted = accept;
        if (accept) {
            if (pdwEffect) {
                *pdwEffect = DROPEFFECT_COPY;
            }
            m_handler->onDragOver(m_payload, p.x, p.y);
        } else {
            m_handler->onDragLeave();
        }

        return S_OK;
    }

    HRESULT __stdcall DragOver(DWORD, POINTL pt, DWORD* pdwEffect) override {
        if (pdwEffect) {
            *pdwEffect = DROPEFFECT_NONE;
        }
        if (!m_handler || !m_hasPayload) {
            return S_OK;
        }

        POINT p{pt.x, pt.y};
        if (!toClientPoint_(p)) {
            if (m_overAccepted) {
                m_handler->onDragLeave();
                m_overAccepted = false;
            }
            return S_OK;
        }

        const bool accept = m_handler->canAcceptDrop(m_payload, p.x, p.y);
        if (accept) {
            if (pdwEffect) {
                *pdwEffect = DROPEFFECT_COPY;
            }
            m_handler->onDragOver(m_payload, p.x, p.y);
            m_overAccepted = true;
        } else if (m_overAccepted) {
            m_handler->onDragLeave();
            m_overAccepted = false;
        }

        return S_OK;
    }

    HRESULT __stdcall DragLeave() override {
        if (m_handler) {
            m_handler->onDragLeave();
        }
        m_hasPayload = false;
        m_overAccepted = false;
        m_payload = SwCreatorSystemDragDrop::Payload{};
        return S_OK;
    }

    HRESULT __stdcall Drop(IDataObject*, DWORD, POINTL pt, DWORD* pdwEffect) override {
        if (pdwEffect) {
            *pdwEffect = DROPEFFECT_NONE;
        }
        if (!m_handler || !m_hasPayload) {
            return S_OK;
        }

        POINT p{pt.x, pt.y};
        if (!toClientPoint_(p)) {
            m_handler->onDragLeave();
            m_overAccepted = false;
            return S_OK;
        }

        const bool accept = m_handler->canAcceptDrop(m_payload, p.x, p.y);
        if (accept) {
            if (pdwEffect) {
                *pdwEffect = DROPEFFECT_COPY;
            }
            m_handler->onDrop(m_payload, p.x, p.y);
        }

        m_handler->onDragLeave();
        m_hasPayload = false;
        m_overAccepted = false;
        m_payload = SwCreatorSystemDragDrop::Payload{};
        return S_OK;
    }

    void setWindowHandle(HWND hwnd) { m_hwnd = hwnd; }

private:
    bool toClientPoint_(POINT& p) const {
        if (!m_hwnd) {
            return false;
        }
        return ::ScreenToClient(m_hwnd, &p) != 0;
    }

    std::atomic<ULONG> m_ref{1};
    SwCreatorSystemDragDrop::DropHandler* m_handler{nullptr};
    HWND m_hwnd{nullptr};
    bool m_hasPayload{false};
    bool m_overAccepted{false};
    SwCreatorSystemDragDrop::Payload m_payload;
};
} // namespace
#endif

SwCreatorSystemDragDrop::Registration::~Registration() {
    reset_();
}

SwCreatorSystemDragDrop::Registration::Registration(Registration&& other) noexcept {
    m_dropTarget = other.m_dropTarget;
    m_windowHandle = other.m_windowHandle;
    other.m_dropTarget = nullptr;
    other.m_windowHandle = SwWidgetPlatformHandle{};
}

SwCreatorSystemDragDrop::Registration& SwCreatorSystemDragDrop::Registration::operator=(Registration&& other) noexcept {
    if (this == &other) {
        return *this;
    }
    reset_();
    m_dropTarget = other.m_dropTarget;
    m_windowHandle = other.m_windowHandle;
    other.m_dropTarget = nullptr;
    other.m_windowHandle = SwWidgetPlatformHandle{};
    return *this;
}

bool SwCreatorSystemDragDrop::Registration::isValid() const {
    return m_dropTarget != nullptr && m_windowHandle;
}

void SwCreatorSystemDragDrop::Registration::reset_() {
#if defined(_WIN32)
    if (!m_dropTarget || !m_windowHandle) {
        m_dropTarget = nullptr;
        m_windowHandle = SwWidgetPlatformHandle{};
        return;
    }

    HWND hwnd = SwWidgetPlatformAdapter::nativeHandleAs<HWND>(m_windowHandle);
    if (hwnd) {
        (void)::RevokeDragDrop(hwnd);
    }

    auto* dropTarget = reinterpret_cast<IDropTarget*>(m_dropTarget);
    dropTarget->Release();
#endif

    m_dropTarget = nullptr;
    m_windowHandle = SwWidgetPlatformHandle{};
}

bool SwCreatorSystemDragDrop::startDrag(const Payload& payload) {
#if defined(_WIN32)
    if (payload.className.isEmpty()) {
        return false;
    }

    if (!ensureOleInitialized_()) {
        return false;
    }

    UINT fmt = payload.isLayout ? layoutClassFormatId_() : widgetClassFormatId_();
    if (!fmt) {
        return false;
    }

    std::wstring text;
    try {
        text = payload.className.toStdWString();
    } catch (...) {
        return false;
    }

    auto* data = new CreatorDataObject(fmt, std::move(text));
    auto* source = new CreatorDropSource();

    // Our Win32 integration captures the mouse on button-down to ensure we always receive move/up
    // events. OLE's DoDragDrop manages capture internally; keeping an active capture here can lead
    // to inconsistent state (and crashes) on subsequent drags.
    if (::GetCapture() != nullptr) {
        ::ReleaseCapture();
    }

    DWORD effect = DROPEFFECT_NONE;
    const HRESULT hr = ::DoDragDrop(data, source, DROPEFFECT_COPY, &effect);

    source->Release();
    data->Release();

    return SUCCEEDED(hr);
#else
    (void)payload;
    return false;
#endif
}

SwCreatorSystemDragDrop::Registration SwCreatorSystemDragDrop::registerDropTarget(const SwWidgetPlatformHandle& windowHandle,
                                                                                  DropHandler* handler) {
    Registration reg;
#if defined(_WIN32)
    if (!windowHandle || !handler) {
        return reg;
    }

    HWND hwnd = SwWidgetPlatformAdapter::nativeHandleAs<HWND>(windowHandle);
    if (!hwnd) {
        return reg;
    }

    if (!ensureOleInitialized_()) {
        return reg;
    }

    auto* target = new CreatorDropTarget(handler);
    target->setWindowHandle(hwnd);

    HRESULT hr = ::RegisterDragDrop(hwnd, target);
    if (hr == DRAGDROP_E_ALREADYREGISTERED) {
        (void)::RevokeDragDrop(hwnd);
        hr = ::RegisterDragDrop(hwnd, target);
    }
    if (FAILED(hr)) {
        target->Release();
        return reg;
    }

    reg.m_dropTarget = target;
    reg.m_windowHandle = windowHandle;
#else
    (void)windowHandle;
    (void)handler;
#endif
    return reg;
}
