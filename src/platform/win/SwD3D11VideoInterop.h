#pragma once

#if defined(_WIN32)

#include "platform/win/SwWindows.h"

#include <d3d11.h>
#include <dxgi1_2.h>
#include <mfapi.h>
#include <mfidl.h>
#include <wrl/client.h>

#include <mutex>

#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "dxgi.lib")

class SwD3D11VideoInterop {
public:
    static SwD3D11VideoInterop& instance() {
        static SwD3D11VideoInterop g_instance;
        return g_instance;
    }

    bool ensure() {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (m_initialized) {
            return m_ready;
        }
        m_initialized = true;

        UINT creationFlags = D3D11_CREATE_DEVICE_BGRA_SUPPORT | D3D11_CREATE_DEVICE_VIDEO_SUPPORT;
        D3D_FEATURE_LEVEL featureLevels[] = {
            D3D_FEATURE_LEVEL_11_1,
            D3D_FEATURE_LEVEL_11_0,
            D3D_FEATURE_LEVEL_10_1,
            D3D_FEATURE_LEVEL_10_0
        };
        D3D_FEATURE_LEVEL featureLevel = D3D_FEATURE_LEVEL_10_0;

        HRESULT hr = D3D11CreateDevice(nullptr,
                                       D3D_DRIVER_TYPE_HARDWARE,
                                       nullptr,
                                       creationFlags,
                                       featureLevels,
                                       ARRAYSIZE(featureLevels),
                                       D3D11_SDK_VERSION,
                                       m_device.GetAddressOf(),
                                       &featureLevel,
                                       m_context.GetAddressOf());
        if (FAILED(hr)) {
            creationFlags = D3D11_CREATE_DEVICE_BGRA_SUPPORT;
            hr = D3D11CreateDevice(nullptr,
                                   D3D_DRIVER_TYPE_HARDWARE,
                                   nullptr,
                                   creationFlags,
                                   featureLevels,
                                   ARRAYSIZE(featureLevels),
                                   D3D11_SDK_VERSION,
                                   m_device.GetAddressOf(),
                                   &featureLevel,
                                   m_context.GetAddressOf());
        }
        if (FAILED(hr)) {
            hr = D3D11CreateDevice(nullptr,
                                   D3D_DRIVER_TYPE_WARP,
                                   nullptr,
                                   D3D11_CREATE_DEVICE_BGRA_SUPPORT,
                                   featureLevels,
                                   ARRAYSIZE(featureLevels),
                                   D3D11_SDK_VERSION,
                                   m_device.GetAddressOf(),
                                   &featureLevel,
                                   m_context.GetAddressOf());
        }
        if (FAILED(hr) || !m_device || !m_context) {
            return false;
        }

        (void)m_device.As(&m_dxgiDevice);
        (void)m_device.As(&m_videoDevice);
        (void)m_context.As(&m_videoContext);

        UINT resetToken = 0;
        Microsoft::WRL::ComPtr<IMFDXGIDeviceManager> manager;
        hr = MFCreateDXGIDeviceManager(&resetToken, manager.GetAddressOf());
        if (SUCCEEDED(hr) && manager) {
            hr = manager->ResetDevice(m_device.Get(), resetToken);
            if (SUCCEEDED(hr)) {
                m_deviceManager = manager;
                m_resetToken = resetToken;
            }
        }

        m_featureLevel = featureLevel;
        m_ready = true;
        return true;
    }

    bool isReady() const {
        return m_ready;
    }

    ID3D11Device* device() const {
        return m_device.Get();
    }

    ID3D11DeviceContext* context() const {
        return m_context.Get();
    }

    IDXGIDevice* dxgiDevice() const {
        return m_dxgiDevice.Get();
    }

    ID3D11VideoDevice* videoDevice() const {
        return m_videoDevice.Get();
    }

    ID3D11VideoContext* videoContext() const {
        return m_videoContext.Get();
    }

    IMFDXGIDeviceManager* deviceManager() const {
        return m_deviceManager.Get();
    }

    UINT resetToken() const {
        return m_resetToken;
    }

    D3D_FEATURE_LEVEL featureLevel() const {
        return m_featureLevel;
    }

    void flush() const {
        if (m_context) {
            m_context->Flush();
        }
    }

private:
    SwD3D11VideoInterop() = default;

    mutable std::mutex m_mutex;
    bool m_initialized{false};
    bool m_ready{false};
    UINT m_resetToken{0};
    D3D_FEATURE_LEVEL m_featureLevel{D3D_FEATURE_LEVEL_10_0};
    Microsoft::WRL::ComPtr<ID3D11Device> m_device;
    Microsoft::WRL::ComPtr<ID3D11DeviceContext> m_context;
    Microsoft::WRL::ComPtr<IDXGIDevice> m_dxgiDevice;
    Microsoft::WRL::ComPtr<ID3D11VideoDevice> m_videoDevice;
    Microsoft::WRL::ComPtr<ID3D11VideoContext> m_videoContext;
    Microsoft::WRL::ComPtr<IMFDXGIDeviceManager> m_deviceManager;
};

#endif
