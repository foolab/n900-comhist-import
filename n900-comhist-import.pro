TEMPLATE = app
TARGET = import
DEPENDPATH += .
INCLUDEPATH += .

SOURCES += main.cpp
HEADERS += catcher.h

LIBS += -lsqlite3 -lcommhistory
