#include "EventLoop.h"
#include "Logger.h"
#include "Poller.h"
#include "Channel.h"

#include <sys/eventfd.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <memory>

// 防止一个线程创建多个EventLoop  thread_local
__thread EventLoop *t_loopInThisThread = nullptr;

// 定义默认的Poller IO复用接口的超时时间
const int kPollTimeMs = 10000;

int createEventfd()
{
    int evtfd = ::eventfd(0, EFD_NONBLOCK | EFD_CLOEXEC);
    if (evtfd < 0)
    {
        LOG_FATAL("eventfd error:%d \n", errno);
    }
    return evtfd;
}

EventLoop::EventLoop()
    : looping_(false),  // 表示EventLoop是否正在循环，初始为false
    quit_(false),       // 表示是否退出循环，初始为false
    callingPendingFunctors_(false), // 表示是否正在调用待处理的函数，初始为false
    threadId_(CurrentThread::tid()), // 获取当前线程的ID
    poller_(Poller::newDefaultPoller(this)), // 创建一个默认的Poller对象，传入当前EventLoop的指针
    wakeupFd_(createEventfd()),  // 创建一个事件文件描述符
    wakeupChannel_(new Channel(this, wakeupFd_)) // 创建一个新的Channel对象，用于处理wakeupFd的事件
    // currentActiveChannel_(nullptr) // 当前活跃的Channel指针，初始为nullptr
{
    // 输出调试日志，记录EventLoop对象的地址和所在线程的ID
    LOG_DEBUG("EventLoop created %p in thread %d \n", this, threadId_);
    // 检查当前线程是否已经存在另一个EventLoop对象
    if (t_loopInThisThread)
    {
        // 如果存在，输出致命错误日志，记录另一个EventLoop对象的地址和当前线程的ID
        LOG_FATAL("Another EventLoop %p exists in this thread %d \n", t_loopInThisThread, threadId_);
    }
    else
    {
        // 如果不存在，将当前EventLoop对象的指针赋值给全局变量t_loopInThisThread
        t_loopInThisThread = this;
    }

    // 设置wakeupfd的事件类型以及发生事件后的回调操作
    // 使用std::bind将EventLoop的handleRead方法绑定到wakeupChannel的读回调上
    wakeupChannel_->setReadCallback(std::bind(&EventLoop::handleRead, this));
    // 每一个eventloop都将监听wakeupchannel的EPOLLIN读事件了
    // 启用wakeupChannel的读事件监听
    wakeupChannel_->enableReading();
}

EventLoop::~EventLoop()
{
    wakeupChannel_->disableAll(); // channel静止
    wakeupChannel_->remove();   // 删除channel
    ::close(wakeupFd_);
    t_loopInThisThread = nullptr;
}

// 开启事件循环
void EventLoop::loop()
{
    looping_ = true;
    quit_ = false;

    LOG_INFO("EventLoop %p start looping \n", this);

    while (!quit_)
    {
        activeChannels_.clear();
        // 监听两类fd  一种是client的fd 一种wakeupfd(mainloop唤醒subloop用的)
        pollReturnTime_ = poller_->poll(kPollTimeMs, &activeChannels_);
        for (Channel *channel : activeChannels_)
        {
            //  Poller监听了哪些channel发生事件，然后上报给EventLoop，通知channel处理相应的事件
            channel->handleEvent(pollReturnTime_);
        }
        // 执行当前EventLoop事件循环需要处理的回调操作
        /*
         * IO线程 mainLoop accept 返回fd <-channel打包fd wakeup subloop
         * mainLoop 实现注册一个回调cb(需要subloop来执行)  wakeup subloop后，执行下面的方法，执行之前mainloop注册的cb操作
         */
        doPendingFunctors();
    }
    LOG_INFO("EventLoop %p stop lopping. \n", this);
    looping_ = false;
}

// 退出事件循环
// 1. loop在自己的线程中调用quit 2.在非loop的线程中，调用loop的quit
/*
 *              mainLoop
 *============生产者-消费者的线程安全的队列==========
 * subLoop1     subLoop2    subLoop3
 */
void EventLoop::quit()
{
    quit_ = true;

    if (!isInLoopThread()) // 如果是在其它线程中，调用的quit 假如在subloop(worker)中调用了mainloop(IO)的quit
    {
        wakeup(); // 唤醒mainloop来处理
    }
}


// 在当前loop中执行cb
void EventLoop::runInLoop(Functor cb)
{
    if (isInLoopThread()) // 在当前的loop线程中，执行cb
    {
        cb();
    }
    else // 在非当前loop线程中执行cb,需要唤醒loop所在线程，执行cb
    {
        queueInLoop(cb);
    }
}

// 把上层注册的回调函数cb放入队列中 唤醒loop所在的线程执行cb
void EventLoop::queueInLoop(Functor cb)
{
    {
        std::unique_lock<std::mutex> lock(mutex_);
        pendingFunctors_.emplace_back(cb);
    }
        /*
     * 有了 callingPendingFunctors_ 的判断，当 callingPendingFunctors_ 为 true 时，
     * 会再次唤醒线程（即使在同一线程），使得新加入的回调函数能够及时被排入执行队列并尽快执行。
     */
    // 唤醒相应的loop，需要执行上面回调操作的loop的线程了
    if (!isInLoopThread() || callingPendingFunctors_) 

    {
        wakeup(); // 唤醒所在线程
    }
}


/* 唤醒事件循环
 * EventLoop::handleRead() 函数的核心作用是唤醒事件循环，
 * 保证 mainLoop 和 subLoop 能够及时响应外部的事件。
 */
void EventLoop::handleRead()
{
    uint64_t one = 1;
    ssize_t n = read(wakeupFd_, &one, sizeof one);
    if (n != sizeof one)
    {
        LOG_ERROR("EventLoop::handleRead() read %d bytes instead of 8", n); 
    }

}

// 唤醒loop所在的线程的 向wakeup_写一个数据，wakeupChannel就发生读事件
void EventLoop::wakeup()
{
    uint64_t one = 1;
    ssize_t n = write(wakeupFd_, &one, sizeof one);
    if (n != sizeof one)
    {
        LOG_ERROR("EventLoop::wakeup() write %lu bytes instead of 8\n", n);
    } 
}

// EventLoop的方法 => Poller的方法
void EventLoop::updateChannel(Channel *channel)
{
    poller_->updateChannel(channel);
}

void EventLoop::removeChannel(Channel *channel)
{
    poller_->removeChannel(channel);
}

bool EventLoop::hasChannel(Channel *channel)
{
    return poller_->hasChannel(channel);
}

void EventLoop::doPendingFunctors() // 执行上层回调
{
    /* 定义一个局部的 vector 来存储待执行的回调函数
     * 使用局部 vector 的目的是为了减少互斥锁的持有时间。
     * 因为将待执行的回调函数从 pendingFunctors_ 中交换到局部 vector 后，
     * 就可以在没有锁的情况下执行这些回调函数，避免在执行回调函数时其他线程无法操作 pendingFunctors_
     */
    std::vector<Functor> functors;
    callingPendingFunctors_ = true;

    {
        std::unique_lock<std::mutex> lock(mutex_);
        functors.swap(pendingFunctors_);
    }
    for (const Functor& functor : functors)
    {
        functor(); // 执行当前loop需要执行的回调操作
    }
    callingPendingFunctors_ = false;
}