#include <sys/types.h>
#include <sys/socket.h>
#include <errno.h>
#include <unistd.h>

#include "Acceptor.h"
#include "Logger.h"
#include "InetAddress.h"

// 创建一个非阻塞的套接字
// 返回值: 成功返回套接字描述符，失败则记录致命错误并可能退出程序
static int createNonblocking()
{
    // 调用系统的 socket 函数创建一个非阻塞的 TCP 套接字
    // AF_INET 表示使用 IPv4 地址族
    // SOCK_STREAM 表示使用面向连接的 TCP 协议
    // SOCK_NONBLOCK 表示将套接字设置为非阻塞模式
    // SOCK_CLOEXEC 表示在执行 exec 系列函数时关闭该套接字
    // IPPROTO_TCP 表示使用 TCP 协议
    int sockfd = ::socket(AF_INET, SOCK_STREAM | SOCK_NONBLOCK | SOCK_CLOEXEC, IPPROTO_TCP);
    if (sockfd < 0)
    {
        LOG_FATAL("%s:%s:%d listen socket create err:%d\n", __FILE__, __FUNCTION__, __LINE__, errno);
    }
    return sockfd;
}

// Acceptor 类的构造函数
// loop: 事件循环对象指针，用于处理事件
// listenAddr: 监听的地址对象
// reuseport: 是否允许端口重用
Acceptor::Acceptor(EventLoop *loop, const InetAddress &listenAddr, bool reuseport)
    : loop_(loop)                               // 初始化事件循环指针
    , acceptSocket_(createNonblocking())        // 创建非阻塞的监听套接字
    , acceptChannel_(loop, acceptSocket_.fd())  // 创建与监听套接字关联的 Channel 对象
    , listenning_(false)                         // 初始化监听状态为未监听
{
    // 设置监听套接字允许地址重用
    acceptSocket_.setReuseAddr(true);
    // 设置监听套接字允许端口重用
    acceptSocket_.setReusePort(true);
    // 将监听套接字绑定到指定的地址
    acceptSocket_.bindAddress(listenAddr);
    //  TcpServer::start() Acceptor.listen 有新用户的连接，要执行一个回调(connfd=> channel=> subLoop)
    //  baseLoop => acceptChannel_(listenfd) =>

    // 为 acceptChannel_ 设置读事件回调函数
    // 当监听套接字有可读事件发生时，会调用 Acceptor::handleRead 函数
    acceptChannel_.setReadCallback(
        std::bind(&Acceptor::handleRead, this));
}

// Acceptor 类的析构函数
Acceptor::~Acceptor()
{
   // 禁用 acceptChannel_ 对所有事件的监听
    // 即从 Poller 中移除该 Channel 感兴趣的事件 
    acceptChannel_.disableAll();
    // 从 EventLoop 和 Poller 中移除 acceptChannel_
    // 会调用 EventLoop 的 removeChannel 方法，进而调用 Poller 的 removeChannel 方法
    // 从 Poller 的 ChannelMap 中删除对应的部分
    acceptChannel_.remove();
}

// 开始监听连接请求
void Acceptor::listen()
{
    // 设置监听状态为已监听
    listenning_ = true;
    // 调用监听套接字的 listen 方法，将其设置为监听状态
    acceptSocket_.listen();
    // 启用 acceptChannel_ 对读事件的监听
    // 将 acceptChannel_ 注册到 Poller 中， 以便Poller监听该套接字的读事件
    acceptChannel_.enableReading();
}

// 处理监听套接字的读事件 即有新的连接请求到来
// listenfd有事件发生了，有新的用户连接
void Acceptor::handleRead()
{
    // 定义一个 InitAddress 对象，用于存储客户端的地址信息
    InetAddress peerAddr;
    // 调用监听套接字的 accept 方法接受新的连接请求
    // 并将客户端的地址信息存储到 peerAddr 中
    int connfd = acceptSocket_.accept(&peerAddr);
    if (connfd >= 0)
    {
        if (NewConnectionCallback_)
        {
            NewConnectionCallback_(connfd, peerAddr); // 轮询找到subLoop唤醒并分发当前的新客户端的Channel   
        }
        else
        {
            ::close(connfd);
        }
    }
    else
    {
        LOG_ERROR("%s:%s%d accept err:%d\n", __FILE__, __FUNCTION__, __LINE__, errno);
        if (errno == EMFILE) // 代表当前线程已达到其可打开文件描述符的最大数量限制
        {
            LOG_ERROR("%s:%s%d sockfd reached limit\n", __FILE__, __FUNCTION__, __LINE__);
        }
    }
}