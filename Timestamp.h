#pragma once

#include <iostream>
#include <string>

class Timestamp
{
public:
    Timestamp();
    explicit Timestamp(int64_t microSecondsSinceEpoch);  // 防止隐式转换对象
    static Timestamp now();
    std::string toString() const;
    
private:
    int64_t microSecondsSinceEpoch_;
};