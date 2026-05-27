QT += widgets

TEMPLATE = app
CONFIG += c++11
TARGET = QtStaticVideoPlayer

INCLUDEPATH += $$PWD/../lib

win32-g++ {
    LIBS += $$PWD/../prebuilt/mingw/libSwQtVideoPlayer.a
    LIBS += -lws2_32 -luser32 -lgdi32 -lgdiplus
    LIBS += -ld3d11 -ldxgi -ld2d1 -ldwrite -ldwmapi -lwinmm
    LIBS += -lwindowscodecs -lmsimg32
    LIBS += -lwmcodecdspuuid -lmfplat -lmfreadwrite -lmf -lmfuuid
    LIBS += -lole32 -loleaut32 -luuid -lstrmiids -lmmdevapi
    LIBS += -lcrypt32 -lbcrypt
}

win32-msvc* {
    LIBS += $$PWD/../prebuilt/msvc/SwQtVideoPlayer.lib
    LIBS += ws2_32.lib user32.lib gdi32.lib gdiplus.lib
    LIBS += d3d11.lib dxgi.lib d2d1.lib dwrite.lib dwmapi.lib winmm.lib
    LIBS += windowscodecs.lib msimg32.lib
    LIBS += wmcodecdspuuid.lib mfplat.lib mfreadwrite.lib mf.lib mfuuid.lib
    LIBS += ole32.lib oleaut32.lib uuid.lib strmiids.lib mmdevapi.lib
    LIBS += crypt32.lib bcrypt.lib
}

SOURCES += main.cpp

OBJECTS_DIR = $$OUT_PWD/obj
MOC_DIR = $$OUT_PWD/moc
RCC_DIR = $$OUT_PWD/rcc
UI_DIR = $$OUT_PWD/ui
