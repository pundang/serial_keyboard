cmake_minimum_required(VERSION 3.10)
project(ch340_keyboard VERSION 0.1.0)

if(WIN32)
    add_executable(${PROJECT_NAME} WIN32
        src/main.c
    )
endif(WIN32)
