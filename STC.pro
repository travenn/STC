QT += qml quick quickwidgets

CONFIG += c++11

SOURCES += main.cpp \
    torrentfile.cpp

RESOURCES += qml.qrc

HEADERS += \
    torrentfile.h \
    qmlsettings.h


win32: RC_ICONS = images/file.ico
