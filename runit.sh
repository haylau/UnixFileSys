#!/bin/bash

rm -f a.out

gcc -Wall -Wextra -Wno-sign-compare -g3 *.c

./a.out
