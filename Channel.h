#pragma once
#include "noncopyable.h"
#include "Timestamp.h"

#include <functional>
#include <memory>

class EventLoop;  // 前置声明
class Timestamp;
/*
 * 理清楚 EventLoop、Channel、Poller之间的关系 
 * - EventLoop： 事件循环，负责管理多个Channel、会调用Poller进行事件监听
 *      当有事件发生时，会通知对应的Channel处理事件
 * - Channel： 通道，封装了文件描述符(sockfd)以及该文件描述符感性的事件
 *      如：EPOLLIN(可读事件)、EPOLLOUT(可写事件)等。还绑定了Poller返回的具体事件
 *      并负责调用相应的回调函数处理事件。
 * - Poller： 事件轮询器，负责监听多个文件描述符上的事件，当有事件发生时，
 *      会将发生的事件信息返回给EventLoop
 * 
 * 
 * ---Channel可以理解为一个桥梁，连接了文件描述符和事件处理逻辑
 *    它将文件描述符和事件进行封装，方便EventLoop进行管理和处理
 */
class Channel : noncopyable
{

public:
    // 定义一个类型别名EventCallback，是一个无参数无返回值的函数对象
    // 用于表示普通事件（如写事件、关闭事件、错误事件）的回调函数
    using EventCallback = std::function<void()>;

    // 定义一个类型别名ReadEventCallback，是一个接受Timestamp类型参数、无返回值的函数对象
    // 用于表示读事件的回调函数，Timestamp 参数表示事件发生的时间
    using ReadEventCallback = std::function<void(Timestamp)>;

    // 构造函数，接受一个EventLoop指针和一个文件描述符作为参数
    Channel(EventLoop *loop, int fd);
    ~Channel();

    // 处理事件的函数，接受一个Timestamp类型的参数，表示事件发生的时间
    // 当文件描述符得到 Poller 通知有事件发生后，调用此函数处理具体事件
    void handleEvent(Timestamp receiveTime);

    // 设置读事件的回调函数，使用 std::move 进行移动语义，避免不必要的拷贝
    void setReadCallback(ReadEventCallback cb) { readCallback_ = std::move(cb); }
    // 设置写事件的回调函数，使用 std::move 进行移动语义
    void setWriteCallback(EventCallback cb) { writeCallback_ = std::move(cb); }
    // 设置关闭事件的回调函数，使用 std::move 进行移动语义
    void setCloseCallback(EventCallback cb) { closeCallback_ = std::move(cb); }
    // 设置错误事件的回调函数，使用 std::move 进行移动语义
    void setErrorCallback(EventCallback cb) { errorCallback_ = std::move(cb); }

    // 防止当channel被手动remove掉，channel还在执行回调操作
    // 通过弱引用 tie_ 绑定一个对象，确保在对象被销毁时，不会再执行回调
    void tie(const std::shared_ptr<void>&);

    int fd() const { return fd_; }
    int events() const { return events_; }
    int set_revents(int revt) { revents_ = revt; return 0;}

    // 设置文件描述符相应的事件状态

    // 启用读事件，将 kReadEvent 按位或到 events_ 中，表示对读事件感兴趣
    // 并调用 update 函数更新事件状态
    void enableReading() { events_ |= kReadEvent; update(); }

    // 禁用读事件，将 events_ 与 kReadEvent 的按位取反进行按位与操作
    // 表示不再对读事件感兴趣，并调用 update 函数更新事件状态
    void disableReading() { events_ &= ~kReadEvent; update(); }

    // 启用写事件，将 kWriteEvent 按位或到 events_ 中，表示对写事件感兴趣
    // 并调用 update 函数更新事件状态
    void enableWriting() { events_ |= kWriteEvent; update(); }

    // 禁用写事件，将 events_ 与 kWriteEvent 进行按位与操作
    // 这里原代码有误，应该是 events_ &= ~kWriteEvent，正确表示不再对写事件感兴趣
    // 并调用 update 函数更新事件状态
    void disableWriting() { events_ &= ~kWriteEvent; update(); }

    // 禁用所有事件，将 events_ 设置为 kNoneEvent，表示不再对任何事件感兴趣
    // 并调用 update 函数更新事件状态
    void disableAll() { events_ = kNoneEvent; update(); }

    // 返回fd当前的事件状态
    // 判断该文件描述符是否没有感兴趣的事件
    bool isNoneEvent() const { return events_ == kNoneEvent; }
    // 判断该文件描述符是否对读事件感兴趣
    bool isWriting() const { return events_ & kWriteEvent; }
    // 判断该文件描述符是否对写事件感兴趣
    bool isReading() const { return events_ & kReadEvent; }

    // 获取该 Channel 在 Poller 中的索引
    int index() { return index_; }
    // 设置该 Channel 在 Poller 中的索引
    void set_index(int idx) { index_ = idx; }

    // 返回该 Channel 所属的 EventLoop 对象指针
    // one loop per thread 设计理念，即一个线程一个事件循环
    EventLoop* owernLoop() { return loop_; }

    // 从 EventLoop中移除该Channel
    void remove();

private:
    // 更新Channel在Poller中的事件监听状态
    void update();

    // 带有保护机制的事件处理函数，用于处理事件时确保对象的有效性
    void handleEventWithGuard(Timestamp receiveTime);

    // 静态常量，表示无事件，通常初始化为 0
    static const int kNoneEvent;
    // 静态常量，表示读事件，通常对应 EPOLLIN
    static const int kReadEvent;
    // 静态常量，表示写事件，通常对应 EPOLLOUT
    static const int kWriteEvent;

    // 指向 EventLoop 对象的指针，该 Channel 所属的事件循环
    EventLoop *loop_;
    // 该 Channel 所关联的文件描述符，Poller 会监听这个文件描述符的事件
    const int fd_;
    // 该文件描述符感兴趣的事件，例如 EPOLLIN、EPOLLOUT 等
    int events_;
    // Poller 返回的该文件描述符实际发生的事件
    int revents_;
    // 用于 Poller 内部标识该 Channel 的索引，方便 Poller 管理多个 Channel
    int index_;

    // 当对象被销毁时，tie_ 会自动失效，避免悬空指针问题
    std::weak_ptr<void> tie_;   // 弱引用，用于绑定一个对象，防止对象在事件处理过程中被意外销毁
    bool tied_;                 // 标记是否已经绑定了对象

    // 因为Channel通道里面能够获知fd最终发生的具体的事件revents，所以它负责调用具体事件的回电函数
    // 四类事件发生时调用的回调函数
    ReadEventCallback readCallback_;
    EventCallback writeCallback_;
    EventCallback closeCallback_;
    EventCallback errorCallback_;
};

