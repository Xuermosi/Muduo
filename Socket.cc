#include "Socket.h"
#include "Logger.h"
#include "InetAddress.h"

#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <string.h>
#include <netinet/tcp.h>
#include <sys/socket.h>


Socket::~Socket()
{
    // 调用系统的 close 函数关闭套接字描述符 sockfd_
    ::close(sockfd_);
}

// 将套接字绑定到指定的本地地址
// localaddr 是一个 InetAddress 类型的常量引用，代表要绑定的本地地址
void Socket::bindAddress(const InetAddress &localaddr)
{
    // 调用系统的 bind 函数进行绑定操作
    // 第一个参数是套接字描述符
    // 第二个参数将 InetAddress 对象转换为 sockaddr 类型的指针
    // 第三个参数是地址结构体的大小
    if(0 != bind(sockfd_, (sockaddr*)localaddr.getSockAddr(), sizeof(sockaddr_in)))
    {
        // 如果绑定失败（返回值不为 0），使用日志记录器输出致命错误信息
        LOG_FATAL("bind sockfd:%d fail \n", sockfd_);
    }
}

// 使套接字进入监听状态，准备接受客户端的连接请求
void Socket::listen()
{
    // 调用系统的 listen 函数将套接字设置为监听状态
    // 第二个参数 1024 表示允许的最大连接请求队列长度
    if (0 != ::listen(sockfd_, 1024))
    {
        LOG_FATAL("listen sockfd:%d fail \n", sockfd_);
    }
}

// 接受一个客户端的连接请求，并返回一个新的套接字描述符用于和客户端通信
// peeraddr 是一个指向 InetAddress 对象的指针，用于存储客户端的地址信息
int Socket::accept(InetAddress *peeraddr)
{
    /*
     * 1. accept函数的参数不合法
     * 2. 对返回的connfd没有设置非阻塞
     * Reactor 模型 one loop per thread
     * poller + non-blocking IO
     */
    // 定义一个 sockaddr_in 结构体变量 addr 用于存储客户端地址信息
    sockaddr_in addr;
    // 定义一个 socklen_t 类型的变量 len 用于存储地址结构体的长度
    socklen_t len = sizeof(addr);
    ::memset(&addr, 0, sizeof(addr));
    // fixed : int connfd = ::accept(sockfd_, (sockaddr *)&addr, &len);

    // 调用系统的 accept 函数接受客户端的连接请求
    // 该函数会阻塞直到有客户端连接请求到来
    // 第一个参数是监听套接字描述符
    // 第二个参数将 addr 结构体的地址转换为 sockaddr 类型的指针
    // 第三个参数是传入地址结构体的长度，并在函数返回时更新实际长度
    // fixed : int connfd = ::accept(sockfd_, (sockaddr*)&addr, &len);
    int connfd = ::accept4(sockfd_, (sockaddr*)&addr, &len, SOCK_NONBLOCK | SOCK_CLOEXEC);
    if (connfd >= 0)
    {
        // 如果接受成功（返回值大于等于 0），将客户端地址信息存储到 peeraddr 指向的对象中
        peeraddr->setSockAddr(addr);
    }
    // 返回新的连接套接字描述符
    return connfd; 
}

// 关闭套接字的写端，意味着不能再向该套接字发送数据，但仍可接收数据
void Socket::shutdownWrite()
{
    // 调用系统的 shutdown 函数关闭套接字的写端
    // 第二个参数 SHUT_WR 表示关闭写操作
    if (::shutdown(sockfd_, SHUT_WR) < 0)
    {
        // 如果关闭失败（返回值小于 0），使用日志记录器输出错误信息
        LOG_ERROR("socket::shutdownWrite error");
    }
}

// 设置 TCP 连接是否禁用 Nagle 算法
// on 是一个布尔类型的参数，true 表示禁用，false 表示启用
void Socket::setTcpNoDelay(bool on)
{
    // 根据 on 的值设置选项值，1 表示启用（这里是禁用 Nagle 算法），0 表示禁用
    int optval = on ? 1 : 0;
    // 调用系统的 setsockopt 函数设置套接字选项
    // 第一个参数是套接字描述符
    // 第二个参数 IPPROTO_TCP 表示 TCP 协议层
    // 第三个参数 TCP_NODELAY 表示禁用 Nagle 算法的选项
    // 第四个参数是选项值的指针
    // 第五个参数是选项值的大小
    ::setsockopt(sockfd_, IPPROTO_TCP, TCP_NODELAY, &optval, sizeof optval);
}

// 设置套接字是否允许地址重用
// on 是一个布尔类型的参数，true 表示允许，false 表示不允许
void Socket::setReuseAddr(bool on)
{
    // 根据 on 的值设置选项值，1 表示允许，0 表示不允许
    int optval = on ? 1 : 0;
    // 调用系统的 setsockopt 函数设置套接字选项
    // 第二个参数 SOL_SOCKET 表示套接字层
    // 第三个参数 SO_REUSEADDR 表示允许地址重用的选项
    ::setsockopt(sockfd_, SOL_SOCKET, SO_REUSEADDR, &optval, sizeof optval);
}

// 设置套接字是否允许端口重用
// on 是一个布尔类型的参数，true 表示允许，false 表示不允许
void Socket::setReusePort(bool on)
{
    // 根据 on 的值设置选项值，1 表示允许，0 表示不允许
    int optval = on ? 1 : 0;
    // 调用系统的 setsockopt 函数设置套接字选项
    // 第二个参数 SOL_SOCKET 表示套接字层
    // 第三个参数 SO_REUSEPORT 表示允许端口重用的选项
    ::setsockopt(sockfd_, SOL_SOCKET, SO_REUSEPORT, &optval, sizeof optval);
}

// 设置套接字是否启用保持活动状态
// on 是一个布尔类型的参数，true 表示启用，false 表示禁用
void Socket::setKeepAlive(bool on)
{
    // 根据 on 的值设置选项值，1 表示启用，0 表示禁用
    int optval = on ? 1 : 0;
    // 调用系统的 setsockopt 函数设置套接字选项
    // 第二个参数 SOL_SOCKET 表示套接字层
    // 第三个参数 SO_KEEPALIVE 表示启用保持活动状态的选项
    ::setsockopt(sockfd_, SOL_SOCKET, SO_KEEPALIVE, &optval, sizeof optval);
}