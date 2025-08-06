#!/bin/bash
set -e

API=33
# 编译参数
CXX=$NDK_HOME/home/ouyang/android-ndk/android-ndk-r28c/toolchains/llvm/prebuilt/linux-x86_64/bin/clang++
TARGET=--target=aarch64-linux-android$API

CXXFLAGS="-std=c++17 -O2 -Wall -static-libstdc++"
LDFLAGS=""

$CXX $TARGET $CXXFLAGS monitor.cpp preload.cpp -o monitor $LDFLAGS


echo "✅ Build successful: ./monitor"
