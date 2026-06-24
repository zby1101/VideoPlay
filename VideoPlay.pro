QT       += core gui network opengl

greaterThan(QT_MAJOR_VERSION, 4): QT += widgets

TARGET = VideoPlay
TEMPLATE = app

DEFINES += QT_DEPRECATED_WARNINGS

CONFIG += c++11
CONFIG += console

# Default rules for deployment.
qnx: target.path = /tmp/$${TARGET}/bin
else: unix:!android: target.path = /opt/$${TARGET}/bin
!isEmpty(target.path): INSTALLS += target

SOURCES += \
        main.cpp \
        MainWindow.cpp \
    VideoProces.cpp \
    SDL2Widget.cpp \
    OpenGLVideoWidget.cpp \
    OpenNetWorkVideoWindow.cpp

HEADERS += \
        MainWindow.h \
    VideoProces.h \
    SDL2Widget.h \
    OpenGLVideoWidget.h \
    OpenNetWorkVideoWindow.h

FORMS += \
        MainWindow.ui \
    OpenNetWorkVideoWindow.ui

MOC_DIR = $$OUT_PWD/MOC
UI_DIR = $$OUT_PWD/UI
OBJECTS_DIR = $$OUT_PWD/OBJ

# FFmpeg 配置
# GPL
FFMPEG_HOME = Y:\work-1\win\video\ffmpeg-n5.1.6-18-g1bcb1be4a2-win64-gpl-shared-5.1

# LGPL
#FFMPEG_HOME = Y:\work-1\win\video\ffmpeg-n5.1.6-18-g1bcb1be4a2-win64-lgpl-shared-5.1


INCLUDEPATH += $$FFMPEG_HOME/include
LIBS += -L$$FFMPEG_HOME/lib \
        -lavcodec -lavdevice -lavfilter -lavformat \
        -lavutil -lswresample -lswscale -lpostproc

# SDL2 配置
SDL2HOME = Y:\work-1\win\video\SDL2-2.30.10\x86_64-w64-mingw32

INCLUDEPATH += $$SDL2HOME/include/SDL2
LIBS += -L$$SDL2HOME/lib -lmingw32 -lSDL2main -lSDL2
DEFINES += SDL_MAIN_HANDLED

