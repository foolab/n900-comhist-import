TEMPLATE = app
TARGET = n900-comhist-import
DEPENDPATH += .
INCLUDEPATH += .

SOURCES += main.cpp
HEADERS += catcher.h

LIBS += -lsqlite3 -lcommhistory

target.path = /usr/bin/
INSTALLS += target
