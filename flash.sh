#!/bin/bash
pio run -t upload -t monitor 2>&1 | tee serial.log
