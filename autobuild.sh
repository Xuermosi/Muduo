#!/bin/bash

set -e

# 如果没有build目录，创建该目录
if [ ! -d "build" ]; then
    mkdir -p build
fi

# 清空build目录内容
rm -rf build/*

# 编译
cd build && \
    cmake -DCMAKE_BUILD_TYPE=Debug .. && \
    make

# 回到项目根目录
cd ..

# 创建头文件目录
if [ ! -d /usr/include/Muduo ]; then
    mkdir -p /usr/include/Muduo
fi

# 拷贝头文件
for header in $(find . -maxdepth 1 -name "*.h"); do
    cp "$header" /usr/include/Muduo/
done

# 检查并拷贝库文件
if [ -f "build/lib/libMuduo.so" ]; then
    cp "build/lib/libMuduo.so" /usr/lib/
elif [ -f "lib/libMuduo.so" ]; then
    cp "lib/libMuduo.so" /usr/lib/
else
    echo "错误：找不到libMuduo.so库文件"
    exit 1
fi

# 更新动态链接库缓存
ldconfig

echo "Muduo库安装完成！"