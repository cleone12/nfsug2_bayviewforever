#!/bin/bash
# This feature is part of BayviewForever (since 2026-10-18): one .asi for every feature.
# This script just builds that: build/BayviewForever.asi + build/BayviewForever.ini.
exec "$(dirname "$0")/../bayview-forever/build.sh"
