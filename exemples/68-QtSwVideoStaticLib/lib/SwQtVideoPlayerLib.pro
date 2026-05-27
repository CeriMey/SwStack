QT += widgets

TEMPLATE = lib
CONFIG += staticlib c++11
TARGET = SwQtVideoPlayer

SWSTACK_ROOT = $$clean_path($$PWD/../../..)

INCLUDEPATH += \
    $$PWD \
    $$SWSTACK_ROOT/src \
    $$SWSTACK_ROOT/src/core \
    $$SWSTACK_ROOT/src/core/gui \
    $$SWSTACK_ROOT/src/core/gui/qtbinding \
    $$SWSTACK_ROOT/src/core/fs \
    $$SWSTACK_ROOT/src/core/hw \
    $$SWSTACK_ROOT/src/core/installer \
    $$SWSTACK_ROOT/src/core/io \
    $$SWSTACK_ROOT/src/core/object \
    $$SWSTACK_ROOT/src/core/platform \
    $$SWSTACK_ROOT/src/core/remote \
    $$SWSTACK_ROOT/src/core/runtime \
    $$SWSTACK_ROOT/src/core/storage \
    $$SWSTACK_ROOT/src/core/types \
    $$SWSTACK_ROOT/src/core/third_party/miniz \
    $$SWSTACK_ROOT/src/media

HEADERS += SwQtVideoPlayerWidget.h
SOURCES += SwQtVideoPlayerWidget.cpp

win32-g++ {
    DESTDIR = $$PWD/../prebuilt/mingw
}
win32-msvc* {
    DESTDIR = $$PWD/../prebuilt/msvc
}

OBJECTS_DIR = $$OUT_PWD/obj
MOC_DIR = $$OUT_PWD/moc
RCC_DIR = $$OUT_PWD/rcc
UI_DIR = $$OUT_PWD/ui
