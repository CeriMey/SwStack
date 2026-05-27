TEMPLATE = subdirs
CONFIG += ordered

SUBDIRS += lib demo

lib.file = lib/SwQtVideoPlayerLib.pro
demo.file = demo/QtStaticVideoPlayer.pro
demo.depends = lib
