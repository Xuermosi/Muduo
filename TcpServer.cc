#include <functional>
#include <string.h>

#include "TcpServer.h"
#include "Logger.h"
#include "TcpConnection.h"

EventLoop* CheckLoopNotNull(EventLoop *loop)
{
    if (loop == nullptr)
    {
        LOG_FATAL("%s:%s:%d mainLoop is null! \n", __FILE__, __FUNCTION__, __LINE__);
    }
    return loop;
}


TcpServer::TcpServer(EventLoop *loop,
              const InetAddress &listenAddr,
              const std::string &nameArg,
              Option option)
              : loop_(CheckLoopNotNull(loop))
              , ipPort_(listenAddr.toIpPort())
              , name_(nameArg)
              , acceptor_(new Acceptor(loop, listenAddr, option == kReusePort)) 
              , threadPool_(new EventLoopThreadPool(loop, name_))
              , connectionCallback_()
              , messageCallback_()
              , nextConnId_(1)
              , started_(0)
{
    // 有一个新的客户端的连接，会执行TcpServer::newConnection回调
    acceptor_->setNewConnectionCallback(std::bind(&TcpServer::newConnection, this,
            std::placeholders::_1, std::placeholders::_2));
}

TcpServer::~TcpServer()
{
    for (auto& item : connections_)
    {
        // 这个局部的shared_ptr智能指针对象，出右括号，可以自动释放new出来的TcpConnection对象资源
        TcpConnectionPtr conn(item.second); 
        item.second.reset();
        // 销毁连接
        conn->getLoop()->runInLoop(
            std::bind(&TcpConnection::connectDestroy, conn));
    }
}

// 设置底层subloop的个数
void TcpServer::setThreadNum(int numThreads)
{
    threadPool_->setThreadNum(numThreads);
}

// 开启服务器监听
void TcpServer::start()
{
    if (started_++ == 0) // 防止一个TcpServer对象被start多次
    {
        threadPool_->start(threadInitCallback_); // 启动底层的loop线程池
        loop_->runInLoop(std::bind(&Acceptor::listen, acceptor_.get()));
    }
}


void TcpServer::newConnection(int sockfd, const InetAddress &peerAddr)
{
    // 轮询算法 选择一个subLoop 来管理connfd对应的channel
    // getNextLoop使用轮询算法从线程池中选择一个EventLoop
    // 这个Loop实例负责处理新连接对应的事件
    EventLoop *ioLoop = threadPool_->getNextLoop();

    // 用于存储连接名称的临时缓冲区
    char buf[64] = {0};
    // 格式化连接名称， 格式为 服务器名称-IP地址和端口#连接ID
    snprintf(buf, sizeof buf, "-%s#%d", ipPort_.c_str(), nextConnId_);
    // 连接ID自增 用于下一个新连接
    nextConnId_++;
    // 此处不设置为原子类是因为其只在mainloop中执行，不涉及线程安全问题

    // 拼接成完整的连接名称
    std::string connName = name_ + buf;

    // 记录日志，输出服务器名称、新连接名称和客户端地址
    LOG_INFO("TcpServer::newConnection [%s] - new connection [%s] from %s\n",
             name_.c_str(), connName.c_str(), peerAddr.toIpPort().c_str());

    // 通过sockfd获取其绑定的本机的ip地址和端口信息
    // 定义一个 sockaddr_in 结构体用于存储本地地址信息
    sockaddr_in local;
    // 初始化 local 结构体
    ::memset(&local, 0, sizeof(local));
    // 存储地址长度
    socklen_t addrlen = sizeof(local);
    // 调用 getsockname 函数获取本地地址信息
    if (::getsockname(sockfd, (sockaddr *)&local, &addrlen) < 0)
    {
        // 获取失败，记录错误日志
        LOG_ERROR("sockets::getLocalAddr");
    }
    // 将 sockaddr_in结构体转换为InetAddress对象
    InetAddress localAddr(local);

    // 根据连接成功的sockfd，创建一个TcpConnection对象， 用于管理新连接
    TcpConnectionPtr conn (new TcpConnection(
                        ioLoop,     // 负责处理该处理事件的EventLoop 实例
                        connName,   // 是连接的名称
                        sockfd,     // 连接的套接字描述符
                        localAddr,  // 本地地址
                        peerAddr)); // 客户端地址
    
    // 将新连接对象存储到 connections_ 映射中， 键为连接名称
    connections_[connName] = conn;

    // 下面的回调都是用户设置给TcpServer => TcpConnection => Channel =(注册)>Poller=>notify channel调用回调
    // 至于Channel绑定的则是TcpConnection设置的四个
    // handleRead,handleWrite... 这下面的回调用于handlexxx函数中
    
    // 设置连接建立时的回调函数
    conn->setConnectionCallback(connectionCallback_);
    // 设置接收到消息时的回调函数
    conn->setMessageCallback(messageCallback_);
    // 设置数据发送完成时的回调函数
    conn->setWriteCompleteCallback(writeCompleteCallback_);

    // 设置如何关闭连接的回调 conn->shutdown
    // 当连接关闭时，会调用 TcpServer::removeConnection方法来移除该连接
    conn->setCloseCallback(
        std::bind(&TcpServer::removeConnection, this, std::placeholders::_1));
    
    // 在ioLoop中直接调用connectEstablished方法， 标志连接已建立
    // 该方法会出发连接建立时的回调函数
    ioLoop->runInLoop(
        std::bind(&TcpConnection::connectEstablished, conn));

}

//  移除一个TCP连接，将移除连接的操作封装到一个回调函数中，并通过事件循环（loop_）来异步执行。
//  确保移除连接的操作在事件循环所在的线程中执行，避免多线程并发访问的问题。
//  conn是一个指向TcpConnection对象的智能指针，表示要移除的TCP连接。
void TcpServer::removeConnection(const TcpConnectionPtr &conn)
{   // 使用事件循环（loop_）的runInLoop方法来执行回调函数。
    // 回调函数是通过std::bind绑定的removeConnectionInLoop方法，确保该方法在事件循环所在的线程中执行。
    loop_->runInLoop(std::bind(&TcpServer::removeConnectionInLoop, this, conn));
}

// 该函数在事件循环的线程中执行实际的移除连接操作。
void TcpServer::removeConnectionInLoop(const TcpConnectionPtr &conn)
{
    LOG_INFO("TcpServer::removeConnectionInLoop [%s] - connection %s\n",
             name_.c_str(), conn->name().c_str());
    // 从连接映射表（connections_）中移除指定名称的连接。
    // 这样可以确保服务器不再维护该连接的信息。
    connections_.erase(conn->name());
    // 获取该连接所属的事件循环对象。
    // 每个连接可能有自己独立的事件循环，用于处理该连接的读写事件等
    EventLoop *ioLoop = conn->getLoop();
    
    // 将连接销毁操作封装到一个回调函数中，并通过事件循环的queueInLoop方法异步执行。
    // 这样可以确保连接的销毁操作在其所属的事件循环线程中执行，避免多线程并发访问的问题。
    ioLoop->queueInLoop(
        std::bind(&TcpConnection::connectDestroy, conn));
}