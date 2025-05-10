#pragma once

#include "noncopyable.h"
#include "Thread.h"

#include <functional>
#include <mutex>
#include <condition_variable>
#include <string>

class EventLoop;

class EventLoopThread : noncopyable
{
public:
    // 定义一个类型别名 ThreadInitCallback，它是一个函数对象，该函数接受一个 EventLoop* 类型的参数并返回 void
    using ThreadInitCallback = std::function<void(EventLoop*)>;

    // 构造函数，接受两个参数
    // cb 是一个线程初始化回调函数，默认值为空的 ThreadInitCallback 对象
    // name 是线程的名称，默认值为空字符串
    EventLoopThread(const ThreadInitCallback &cb = ThreadInitCallback(),
        const std::string &name = std::string());

    // 析构函数，负责清理对象资源
    ~EventLoopThread();

    // 启动线程并开始运行 EventLoop，返回指向该 EventLoop 的指针
    EventLoop* startLoop();

private:
    // 线程函数，该函数将在新线程中执行
    void threadFunc();

    // 指向 EventLoop 对象的指针，用于管理事件循环
    EventLoop *loop_;

    // 标志位，用于表示线程是否正在退出
    bool exiting_;

    // 线程对象，用于管理新线程
    Thread thread_;

    // 互斥锁，用于线程同步
    std::mutex mutex_;

    // 条件变量，用于线程间的等待和通知机制
    std::condition_variable cond_;

    // 线程初始化回调函数，在 EventLoop 启动前执行
    ThreadInitCallback callback_;
};