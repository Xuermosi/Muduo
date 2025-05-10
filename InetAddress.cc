#include "InetAddress.h"

#include <arpa/inet.h>
#include <strings.h>
#include <string.h>

InetAddress::InetAddress(uint16_t port, std::string ip)
{
    ::memset(&addr_, 0, sizeof(addr_));            // addr_结构体的内存空间清零，避免其中存在未初始化数据
    addr_.sin_family = AF_INET;                    // 将地址族设置为IPv4
    addr_.sin_port = htons(port);                  // 借助htons函数将端口号从主机字节序转换为网络字节序
    addr_.sin_addr.s_addr = inet_addr(ip.c_str()); // 利用inet_addr函数把点分十进制的IP地址字符串转换为32位的网络字节序整数
}

/*
 * 把存储的IP地址转换为点分十进制的字符串形式
 */
std::string InetAddress::toIp() const
{
    char buf[64] = {0};
    // 把 32 位的网络字节序整数形式的 IP 地址转换为点分十进制的字符串
    ::inet_ntop(AF_INET, &addr_.sin_addr, buf, sizeof buf);
    return buf;
}

/*
 * 把存储的IP地址和端口号组合成一个字符串，格式为 IP:Port
 */
std::string InetAddress::toIpPort() const
{
    // ip : port
    char buf[64] = {0};
    ::inet_ntop(AF_INET, &addr_.sin_addr, buf, sizeof buf); // 将 IP 地址转换为点分十进制字符串并存储在 buf 中。
    // 获取 buf 中 IP 地址字符串的长度。
    size_t end = strlen(buf);
    // 使用 ntohs 函数把端口号从网络字节序转换为主机字节序。
    uint16_t port = ntohs(addr_.sin_port);
    // 在 IP 地址字符串后面拼接上冒号和端口号。
    sprintf(buf + end, ":%u", port);
    return buf;
}

uint16_t InetAddress::toPort() const
{
    return ntohs(addr_.sin_port);
}

#if 0
#include <iostream>
int main()
{
    InetAddress addr(8080);
    std::cout << addr.toIpPort() << std::endl;
}
#endif
