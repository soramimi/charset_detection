DESTDIR = $$PWD
TEMPLATE = app
CONFIG += console c++17
CONFIG -= app_bundle
CONFIG -= qt

SOURCES += \
        charset_detection.cpp \
        main.cpp \
        unicode_conversion.cpp

HEADERS += \
    charset_detection.h \
    unicode_conversion.h

DISTFILES += \
    eucjp.table \
    validation_jp.table
