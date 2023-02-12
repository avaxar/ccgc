@echo off

gcc ./example.c ./ccgc.c -I . -g -o ./example.exe
"%CD%"\example.exe
