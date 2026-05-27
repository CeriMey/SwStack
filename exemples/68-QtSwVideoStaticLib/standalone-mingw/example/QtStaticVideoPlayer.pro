QT += widgets

TEMPLATE = app
CONFIG += c++11 release
TARGET = QtStaticVideoPlayer

SDK_ROOT = $$clean_path($$PWD/..)

INCLUDEPATH += $$SDK_ROOT/include

win32-g++ {
    LIBS += $$SDK_ROOT/lib/mingw/libSwQtVideoPlayer.a
    LIBS += -lws2_32 -luser32 -lgdi32 -lgdiplus
    LIBS += -ld3d11 -ldxgi -ld2d1 -ldwrite -ldwmapi -lwinmm
    LIBS += -lwindowscodecs -lmsimg32
    LIBS += -lwmcodecdspuuid -lmfplat -lmfreadwrite -lmf -lmfuuid
    LIBS += -lole32 -loleaut32 -luuid -lstrmiids -lmmdevapi
    LIBS += -lcrypt32 -lbcrypt
}

!win32-g++ {
    error("This standalone package is built for Qt MinGW. Rebuild libSwQtVideoPlayer.a for your compiler first.")
}

SOURCES += $$PWD/main.cpp

DESTDIR = $$SDK_ROOT/bin
OBJECTS_DIR = $$OUT_PWD/obj
MOC_DIR = $$OUT_PWD/moc
RCC_DIR = $$OUT_PWD/rcc
UI_DIR = $$OUT_PWD/ui
