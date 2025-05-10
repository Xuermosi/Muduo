#include <functional>
#include <string>
#include <errno.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <string.h>
#include <netinet/tcp.h>
#include <sys/sendfile.h>
#include <fcntl.h> // for open
#include <unistd.h> // for close

#include "TcpConnection.h"
#include "Logger.h"
#include "Socket.h"
#include "Channel.h"
#include "EventLoop.h"

static EventLoop* CheckLoopNotNull(EventLoop *loop)
{
    if (loop == nullptr)
    {
        LOG_FATAL("%s:%s:%d TcpConnection Loop is null ! \n", __FILE__, __FUNCTION__, __LINE__);
    }
    return loop;
}

TcpConnection::TcpConnection(EventLoop *loop,
                const std::string &nameArg,
                int sockfd,
                const InetAddress& localAddr,
                const InetAddress& peerAddr)
    : loop_(CheckLoopNotNull(loop))
    , name_(nameArg)
    , state_(kConnecting)
    , reading_(true)
    , socket_(new Socket(sockfd))
    , channel_(new Channel(loop, sockfd))
    , localAddr_(localAddr)
    , peerAddr_(peerAddr)
    , highWaterMark_(64*1024*1024)  // 64M
{
    // 下面给channel设置相应的回调函数， poller给channel通知感兴趣的事件发生了，channel会回调相应的回调函数
    channel_->setReadCallback(
        std::bind(&TcpConnection::handleRead, this, std::placeholders::_1)
    );
    channel_->setWriteCallback(
        std::bind(&TcpConnection::handleWrite, this)
    );
    channel_->setCloseCallback(
        std::bind(&TcpConnection::handleClose, this)
    );
    channel_->setCloseCallback(
        std::bind(&TcpConnection::handleError,this)
    ); 
    LOG_INFO("TcpConnection::ctor[%s] at fd=%d\n", name_.c_str(), sockfd);
    socket_->setKeepAlive(true);
}

TcpConnection::~TcpConnection()
{
    LOG_INFO("TcpConnection::dtor[%s] at fd=%d state=%d\n", name_.c_str(), channel_->fd(), (int)state_);
}

/*
 * 发送数据 应用写的快，而内核发送数据慢，需要把待发送数据写入缓冲区，并且设置了水位回调
 */
void TcpConnection::sendInLoop(const void* data, size_t len)
{
    
    ssize_t nwrote = 0; // 用于记录实际写入的字节数
    size_t remaining = len; // 记录剩余未发送的字节数，初始值为要发送数据的总长度
    bool faultError = false; // 标记是否出现错误

    // 检查连接状态，如果连接已经断开，则记录错误日志并放弃发送数据，直接返回
    if (state_ == kDisconnected)
    {
        LOG_ERROR("disconnected, give up writing!");
        return;
    }

    // 检查channel_ 是否正在进行写操作，并且输出缓冲区没有待发送数据
    // 表示channel_第一次开始写数据，且缓冲区为空
    if (channel_->isWriting() && outputBuffer_.readableBytes() == 0)
    {
        // 尝试将数据写入channel_对应的文件描述符
        nwrote = ::write(channel_->fd(), data, len);
        // 写入成功 即写入的字节数大于等于0
        if (nwrote >= 0)
        {
            // 更新剩余未发送的字节数
            remaining = len - nwrote;
            // 若剩余未发送的字节数为0，说明数据全部发送完成
            // 并且存在写完成回调函数writeCompleteCallback_
            if (remaining == 0 && writeCompleteCallback_)
            {
                // 既然在这里数据全部发送完成，就不用再给Channel设置epollout事件
                // 将写完成回调函数加入事件循环的队列中，后续会执行该回调
                loop_->queueInLoop(
                    std::bind(writeCompleteCallback_, shared_from_this())
                );
            }
        }
        else // 写入失败 即写入的字节数小于0
        {
            // 将实际写入字节数置为0
            nwrote = 0;
            // 若错误码不是 EWOULDBLOCK (表示当前不能立即写入，需要等待)
            if (errno != EWOULDBLOCK)
            {
                // 记录错误日志
                LOG_ERROR("TcpConnection::sendInLoop");
                // 如果错误码是 EPIPE（表示管道破裂，通常是对方关闭连接后继续写）
                // 或者 ECONNRESET（表示连接被重置）
                if (errno == EPIPE || errno == ECONNRESET) // SIGPIPE RESET
                {
                    // 标记出现了错误
                    faultError = true;
                }
            }
        }
    }
    /**
     * 说明当前这一次write并没有把数据全部发送出去 剩余的数据需要保存到缓冲区当中
     * 然后给channel注册EPOLLOUT事件，Poller发现tcp的发送缓冲区有空间后会通知
     * 相应的sock->channel，调用channel对应注册的writeCallback_回调方法，
     * channel的writeCallback_实际上就是TcpConnection设置的handleWrite回调，
     * 把发送缓冲区outputBuffer_的内容全部发送完成
     **/
    if (!faultError && remaining > 0)
    {
        // 目前发送缓冲区剩余的待发送的数据的长度
        size_t oldLen = outputBuffer_.readableBytes();
        // 如果添加剩余数据后，缓冲区的总长度超过了高水位标记highWaterMark_
        // 并且之前的缓冲区的长度小于高水位标记
        // 同时存在高水位标记回调函数highWaterMarkCallback_
        if (oldLen + remaining >= highWaterMark_ 
            && oldLen < highWaterMark_ 
            && highWaterMarkCallback_)
        {
            // 将高水位标记回调函数加入事件循环的队列中，后续会执行该回调
            // 同时传递当前的 TcpConnection 对象指针和新的缓冲区总长度
            loop_->queueInLoop(
                std::bind(highWaterMarkCallback_, shared_from_this(), oldLen + remaining));
        }
        // 将剩余未发送的数据添加到输出缓冲区
        outputBuffer_.append((char *)data + nwrote, remaining);
        // 如果 channel_ 没有注册写事件
        if (!channel_->isWriting())
        {
            // 注册 channel_ 的写事件，这样 Poller 才能在 TCP 发送缓冲区有空间时通知 channel_
            channel_->enableWriting(); // 这里一定要注册channel的写事件 否则poller不会给channel通知epollout
        }
    }
}

// 关闭半连接的函数，用于发起关闭连接的操作
// 该函数会检查当前连接状态，若为已连接则将状态设置为正在断开连接，并在事件循环中执行关闭操作
void TcpConnection::shutdown()
{
    // 检查连接状态是否为已连接
    if (state_ == kConnected)
    {
        // 将连接状态置为正在断开连接
        setState(kDisconnecting);
        // 在所属的事件循环中执行shutdownInLoop函数
        // 使用std::bind将当前对象的this指针和shutdownInLoop函数绑定，确保在事件循环中正确调用
        loop_->runInLoop(
            std::bind(&TcpConnection::shutdownInLoop, this));
    }
}


// 在事件循环中执行的关闭半连接的具体操作函数
// 该函数会检查当前channel是否正在进行写操作，若没有则关闭写端
void TcpConnection::shutdownInLoop()
{
    // 检查channel是否没有正在进行写操作
    if (!channel_->isWriting()) // 说明outputBuffer中的数据已经全部发送完成
    {
        // 如果channel没有正在写，调用socket的shutdownWrite方法关闭写端
        // 这意味着不再向对端发送数据，但仍可接收对端的数据，实现半关闭
        socket_->shutdownWrite(); // 关闭写端
    }
}

// 连接建立
void TcpConnection::connectEstablished()
{
    // 连接状态置为已连接
    setState(kConnected);

    // 将channel与当前TcpConnection对象绑定，确保在channel生命周期内TcpConnection不会被销毁
    // 这里使用了shared_from_this()来获取当前对象的共享指针，以便在其他地方安全使用
    channel_->tie(shared_from_this());
    // 启动channel的读事件，意味着向poller注册epollin事件
    channel_->enableReading(); // 向poller注册channel的epollin事件
    // 调用用户自定义的连接回调函数，将当前TcpConnection对象的共享指针作为参数传递进去
    // 这样用户可以在回调函数中对已建立的连接进行进一步的操作和处理
    connectionCallback_(shared_from_this());
}

// 连接销毁
void TcpConnection::connectDestroy()
{
    if (state_ == kConnected)
    {
        setState(kDisconnected);
        channel_->disableAll(); // 把channel的所有感兴趣的事件，从poller中del掉
        connectionCallback_(shared_from_this());
    }
    channel_->remove(); //把channel从poller中删除掉
}

void TcpConnection::handleRead(Timestamp receiveTime)
{
    int savedErrno = 0;
    ssize_t n = inputBuffer_.readFd(channel_->fd(), &savedErrno);
    if (n > 0) // 有数据到达
    {
        // 已建立连接的用户有可读事件发生了 调用用户传入的回调操作onMessage shared_from_this就是获取了TcpConnection的智能指针
        messageCallback_(shared_from_this(), &inputBuffer_, receiveTime);
    }
    else if (n == 0) // 客户端断开
    {
        handleClose();
    }
    else // 出错了
    {
        errno = savedErrno;
        LOG_ERROR("TcpConnection::handleRead");
        handleError();
    }
}

void TcpConnection::handleWrite()//处理写事件
{
    int savedErrno = 0;
    if (channel_->isWriting())
    {
        ssize_t n = outputBuffer_.writeFd(channel_->fd(), &savedErrno);
        if (n > 0)
        {
            outputBuffer_.retrieve(n);
            if (outputBuffer_.readableBytes() == 0)
            {
                channel_->disableWriting();
                if (writeCompleteCallback_)
                {
                    // 换线loop_对应的thread线程，执行回调
                    loop_->queueInLoop(
                        std::bind(writeCompleteCallback_, shared_from_this())
                    );
                }
                if (state_ == kDisconnecting)
                {
                    shutdownInLoop();
                }
                
            }
        }
        else
        {
            LOG_ERROR("TcpConnection::handleWrite");
        }
    }
    else
    {
        LOG_ERROR("TcpConnection fd=%d is down, no more writing", channel_->fd());
    }
}

// poller => channel::closeCallback => TcpConnection::handleClose
// 处理 TCP 连接关闭的函数
void TcpConnection::handleClose()
{
    // 记录日志信息，输出当前连接对应的文件描述符以及连接的状态
    // channel_->fd() 用于获取连接对应的文件描述符
    // (int)state_ 将连接状态转换为整数类型进行输出
    LOG_INFO("TcpConnection::handleClose fd=%d state=%d\n", channel_->fd(), (int)state_);
    // 将连接状态设置为已断开连接
    setState(kDisconnected);
    // 禁用 channel 中所有的事件监听，避免后续不必要的事件触发
    channel_->disableAll();

    // 使用 std::shared_from_this() 创建一个指向当前对象的共享指针
    // 这样做的目的是为了在回调函数中安全地使用当前的 TcpConnection 对象
    // 防止在回调函数执行期间对象被意外销毁
    TcpConnectionPtr connPtr(shared_from_this());
    // 执行连接关闭时的回调函数
    // connectionCallback_ 是一个函数对象，用于处理连接关闭相关的逻辑
    // 传入当前连接的共享指针作为参数
    connectionCallback_(connPtr); 
    
    // closeCallback_ 同样是一个函数对象，用于执行关闭连接时需要完成的操作
    // 传入当前连接的共享指针作为参数
    closeCallback_(connPtr);    // 执行关闭连接的回调 执行的是TcpServer::removeConnection回调方法
}

// 处理 TCP 连接错误的函数
void TcpConnection::handleError()
{
    // 用于存储从 getsockopt 函数获取的套接字选项值
    int optval;
    // 存储 optval 变量的长度
    socklen_t optlen = sizeof optval;
    // 用于存储最终的错误码
    int err = 0;

    // 使用 getsockopt 函数获取套接字的错误信息
    // channel_->fd() 是当前连接对应的文件描述符
    // SOL_SOCKET 表示要获取的是套接字级别的选项
    // SO_ERROR 表示获取套接字的错误状态
    // &optval 是用于存储错误信息的缓冲区
    // &optlen 是缓冲区的长度
    // 如果 getsockopt 函数调用失败（返回值小于 0）
    if (::getsockopt(channel_->fd(), SOL_SOCKET, SO_ERROR, &optval, &optlen) < 0)
    {
        // 则将 errno （系统定义的错误码）赋值给 err
        err = errno;
    }
    else
    {
        // 如果 getsockopt 函数调用成功
        // 则将获取到的错误信息赋值给 err
        err = optval;
    }

    // 记录错误日志
    // name_.c_str() 获取连接的名称并转换为 C 风格字符串
    // err 是获取到的错误码
    LOG_ERROR("TcpConnection::handleError name:%s - SO_ERROR:%d\n", name_.c_str(), err);
}

void TcpConnection::send(const std::string &buf)
{
    if (state_ == kConnected)
    {
        if (loop_->isInLoopThread())
        {
            sendInLoop(buf.c_str(), buf.size());
        }
        else
        {
            loop_->runInLoop(std::bind(
                &TcpConnection::sendInLoop,
                this,
                buf.c_str(),
                buf.size()
            ));
        }
    }
}



