cmake_minimum_required(VERSION 3.10)
project(serial_keyboard VERSION 1.0.0)

if(WIN32)
    add_executable(${PROJECT_NAME} WIN32
        src/main.c
        src/serial.c
        src/serial.h
    )
endif(WIN32)
