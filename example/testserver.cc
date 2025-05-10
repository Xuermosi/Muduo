#include <Muduo/TcpServer.h>
#include <Muduo/Logger.h>

#include <string>
#include <functional>

class EchoServer
{
public:
    EchoServer(EventLoop *loop,         // 事件循环
            const InetAddress &addr,    // IP+Port
            const std::string &name)    // 服务器的名字
            : server_(loop, addr, name)
            , loop_(loop)
    {
        // 注册回调函数
        // 给服务器注册用户连接的创建和断开回调
        server_.setConnectionCallback(
            std::bind(&EchoServer::onConnection, this, std::placeholders::_1));
        
        // 给服务器注册用户读写事件回调
        server_.setMessageCallback(std::bind(&EchoServer::onMessage, this,
                                   std::placeholders::_1, std::placeholders::_2, std::placeholders::_3));

        // 设置合适的loop线程数量 loopthread
        server_.setThreadNum(3);
    }
    void start()
    {
        server_.start();
    }
private:
    // 连接建立或断开的回调函数
    // 专门处理用户的连接创建和断开 epoll listen accept
    void onConnection(const TcpConnectionPtr &conn)
    {
        if (conn->connected())
        {
            LOG_INFO("Connection UP : %s", conn->peerAddress().toIpPort().c_str());
        }
        else
        {
            LOG_INFO("Connection DOWN : %s", conn->peerAddress().toIpPort().c_str());
        }
    }

    // 可读写事件回调
    // 专门处理用户的读写事件
    void onMessage(const TcpConnectionPtr &conn, // 连接
                   Buffer *buf,                  // 缓冲区
                   Timestamp time)               // 时间戳
    {
        std::string msg = buf->retrieveAllAsString();
        conn->send(msg);
        // conn->shutdown(); // 关闭写端 底层响应EPOLLHUP => 执行closeCallback_
    }
    
    TcpServer server_;    // #1
    EventLoop *loop_;     // #2 epoll  
};

int main()
{
    EventLoop loop; // epoll
    InetAddress addr(8000); // 端口
    // 创建server对象
    EchoServer server(&loop, addr, "EchoServer-01"); // Acceptor non-blocking listenfd create bind
    server.start(); // liste loopthread listenfd => acceptChannel => mainLoop =>
    loop.loop();    // 启动mainLoop的底层Poller
    return 0;
}