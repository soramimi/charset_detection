DESTDIR = $$PWD
TARGET = utf8
TEMPLATE = app
CONFIG += console c++17
CONFIG -= app_bundle
CONFIG -= qt

DEFINES += NOMINMAX

SOURCES += \
        ../charset_detection.cpp \
        ../unicode_conversion.cpp \
        main.cpp

HEADERS += \
    ../charset_detection.h \
    ../unicode_conversion.h
