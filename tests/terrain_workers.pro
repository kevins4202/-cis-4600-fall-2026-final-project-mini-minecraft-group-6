QT += core widgets openglwidgets
CONFIG += console c++17
CONFIG -= app_bundle
TARGET = terrain_workers_test
INCLUDEPATH += ../assignment_package/src ../assignment_package/include
SOURCES += terrain_workers.cpp \
    ../assignment_package/src/scene/terrain.cpp \
    ../assignment_package/src/scene/terrainworkers.cpp \
    ../assignment_package/src/scene/chunk.cpp \
    ../assignment_package/src/scene/noise.cpp \
    ../assignment_package/src/drawable.cpp \
    ../assignment_package/src/shaderprogram.cpp \
    ../assignment_package/src/openglcontext.cpp
