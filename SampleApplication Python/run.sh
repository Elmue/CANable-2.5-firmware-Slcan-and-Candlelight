#!/usr/bin/env bash

cd "\((dirname "\)(readlink -f "$0")")" || exit 1

sudo python3 Source/CANableDemo.py

