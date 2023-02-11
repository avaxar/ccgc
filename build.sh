#!/usr/bin/bash

gcc ./example.c ./ccgc.c -I . -g -o ./example.out
./example.out
