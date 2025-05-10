#include "Channel.h"
#include "EventLoop.h"
#include "Logger.h"

#include <sys/epoll.h>


// 静态常量，表示无事件，通常初始化为 0
const int Channel::kNoneEvent = 0;
// 静态常量，表示读事件，通常对应 EPOLLIN
const int Channel::kReadEvent = EPOLLIN | EPOLLPRI;
// 静态常量，表示写事件，通常对应 EPOLLOUT
const int Channel::kWriteEvent = EPOLLOUT;


// EventLoop: ChannelList Poller
// 构造函数，用于初始化 Channel 对象
// 接收一个 EventLoop 指针和一个文件描述符作为参数
Channel::Channel(EventLoop *loop, int fd)
    : loop_(loop)  // 初始化 loop_ 指针，指向该 Channel 所属的 EventLoop
    , fd_(fd)      // 初始化 fd_，表示该 Channel 所关联的文件描述符
    , events_(0)   // 初始化 events_ 为 0，意味着初始时不关注任何事件
    , revents_(0)  // 初始化 revents_ 为 0，用于存储 Poller 返回的该文件描述符实际发生的事件
    , index_(-1)   // 初始化 index_ 为 -1，index_ 用于 Poller 内部标识该 Channel 的索引，-1 表示尚未在 Poller 中注册
    , tied_(false) // 初始化 tied_ 为 false，表示尚未绑定对象
{

}

Channel::~Channel()
{

}

// channel的tie方法何时调用？
// 一个TcpConnection新连接创建的时候 TcpConnection => Channel
// 该方法用于将 Channel 与一个对象绑定，防止在对象被销毁时 Channel 还在执行回调操作
// 接收一个 std::shared_ptr<void> 类型的对象引用作为参数
void Channel::tie(const std::shared_ptr<void> &obj)
{
    tie_ = obj;
    tied_ = true;
}

/*
 * 当改变Channel所表示fd的events事件后，update负责在poller里面更改fd相应的事件epoll_ctl
 * EventLoop => ChannelList Poller
 */
void Channel::update()
{
    // 通过channel所属的EventLoop，调用poller的相应方法，注册fd的events事件
    // add code...
    loop_->updateChannel(this);
    // 这里注释掉的代码是需要补充的逻辑，应该调用 EventLoop 的 updateChannel 方法
    // 并将当前 Channel 对象的指针传递给它，让 EventLoop 去通知 Poller 更新事件
}


/*
 * 在Channel所属的EventLoop中，把当前的channel删除掉
 */
// remove 方法用于从 Channel 所属的 EventLoop 中移除该 Channel
// 当不再需要监听该文件描述符的事件时，调用此方法
void Channel::remove()
{   
    // add code...
    loop_->removeChannel(this);
    // 这里注释掉的代码是需要补充的逻辑，应该调用 EventLoop 的 removeChannel 方法
    // 并将当前 Channel 对象的指针传递给它，让 EventLoop 去通知 Poller 移除该 Channel
}

// fd得到poller通知以后，处理事件
// 当 Poller 检测到该文件描述符有事件发生时，会调用此方法处理事件
// 接收一个 Timestamp 类型的参数，表示事件发生的时间
void Channel::handleEvent(Timestamp receiveTime)
{
    if (tied_)
    {
        std::shared_ptr<void> guard = tie_.lock(); // 弱智能指针提升为强智能指针
        // 如果 tie_ 绑定了对象，尝试将弱引用提升为强引用
        // 提升成功表示对象仍然存活
        if (guard)
        {
            handleEventWithGuard(receiveTime);
        }// 如果提升失败了 就不做任何处理 说明Channel的TcpConnection对象已经不存在了
    }
    else
    {// 如果没有绑定对象，直接调用 handleEventWithGuard 方法处理事件
        handleEventWithGuard(receiveTime);
    }
}

// 根据poller通知的channel发生的具体事件， 由channel负责调用具体的回调操作
void Channel::handleEventWithGuard(Timestamp receiveTime)
{
    LOG_INFO("channel handleEvent revents:%d\n", revents_);

    if ((revents_ & EPOLLHUP) && !(revents_ & EPOLLIN)) // 出问题了
    {// 如果检测到 EPOLLHUP 事件（表示挂起）且没有 EPOLLIN 事件（可读事件）
        if (closeCallback_)
        {
            closeCallback_();
            // 如果设置了关闭事件的回调函数，调用该回调函数处理关闭事件
        }
    }

    if (revents_ & EPOLLERR)
    {
        if (errorCallback_)
        {
            errorCallback_();
        }
    }

    if (revents_ & (EPOLLIN | EPOLLPRI))
    {
        if (readCallback_)
        {
            readCallback_(receiveTime);
        }
    }

    if (revents_ & EPOLLOUT)
    {
        if (writeCallback_)
        {
            writeCallback_();
        }
    }
}