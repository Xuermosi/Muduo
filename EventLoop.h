#pragma once

#include <functional>
#include <vector>
#include <atomic>
#include <memory>
#include <mutex>

#include "noncopyable.h"
#include "Timestamp.h"
#include "CurrentThread.h"
#include "Logger.h"
#include "Poller.h"
#include "Channel.h"

class Channel;
class Poller;

// 事件循环类 主要包括了两个大模块 Channel  Poller(epoll的抽象)

class EventLoop : noncopyable
{
public:
    using Functor = std::function<void()>;

    EventLoop();
    ~EventLoop();

    // 开启事件循环
    void loop();
    // 退出事件循环
    void quit();

    Timestamp pollReturnTime() const { return pollReturnTime_; }

    // 在当前loop中执行cb
    void runInLoop(Functor cb);

    // 把上层注册的回调函数cb放入队列中 唤醒loop所在的线程执行cb
    void queueInLoop(Functor cb);

    // 唤醒loop所在的线程
    void wakeup();

    // EventLoop的方法 => Poller的方法
    void updateChannel(Channel *channel);
    void removeChannel(Channel *channel);
    bool hasChannel(Channel *channel);
    
    // 判断EventLoop对象是否在自己的线程里面
    bool isInLoopThread() const { return threadId_ == CurrentThread::tid(); }// thread

private:
    void handleRead();        // 给eventfd返回的文件描述符wakeupFd_绑定的事件回调  
    void doPendingFunctors(); // 执行上层回调

    using ChannelList = std::vector<Channel*>;

    std::atomic_bool looping_; // 原子操作，通过CAS实现
    std::atomic_bool quit_; //标识退出loop循环
    
    const pid_t threadId_; // 记录当前loop所在线程的id
    Timestamp pollReturnTime_; // poller返回发生事件的channels的时间点
    std::unique_ptr<Poller> poller_;

    // eventfd
    // mainReactor-轮询-唤醒-subReactor
    int wakeupFd_; // 主要作用：当mainLoop获取一个新用户的channel，需通过轮询算法选择一个subLoop，通过该成员唤醒subLoop
    std::unique_ptr<Channel> wakeupChannel_;

    ChannelList activeChannels_;
    // Channel *currentActiveChannel_;
    std::atomic_bool callingPendingFunctors_; // 标识当前loop是否有需要执行的回调操作
    std::vector<Functor> pendingFunctors_; // 存储loop需要执行的所有的回调操作
    std::mutex mutex_; // 互斥锁，用来保护上面vector容器的线程安全操作

};