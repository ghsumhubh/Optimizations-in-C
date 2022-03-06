cmake_minimum_required(VERSION 3.16)
project(Ex05 C)

set(CMAKE_C_STANDARD 99)

add_executable(Ex05 myfunction.c readBMP.c readBMP.h showBMP.c writeBMP.c writeBMP.h)
