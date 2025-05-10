#include "Timestamp.h"

#include <time.h>
// 默认构造
Timestamp::Timestamp():microSecondsSinceEpoch_(0) {}

Timestamp::Timestamp(int64_t microSecondsSinceEpoch)
            : microSecondsSinceEpoch_(microSecondsSinceEpoch)
            {}


Timestamp Timestamp::now()
{
    return Timestamp(time(NULL));
}

std::string Timestamp::toString() const
{
    char buf[128] = {0};
    tm *tm_time = localtime(&microSecondsSinceEpoch_);
    snprintf(buf, 128, "%4d/%02d/%02d %02d:%02d:%02d",
        tm_time->tm_year + 1900,  // tm_year成员存储的是从 1900 年开始计算的年数
        tm_time->tm_mon + 1,      // tm_mon成员存储的是从 0 开始计数的月份
        tm_time->tm_mday,
        tm_time->tm_hour,
        tm_time->tm_min,
        tm_time->tm_sec);
    return buf;
}

// #include <iostream>
// int main()
// {
//     std::cout << Timestamp::now().toString() << std::endl;
//     return 0;
// }