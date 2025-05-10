#include "EventLoopThread.h"
#include "EventLoop.h"

#include <memory>

/* 构造函数，接受两个参数
* cb 是一个线程初始化回调函数，默认值为空的 ThreadInitCallback 对象
* name 是线程的名称，默认值为空字符串
*/
EventLoopThread::EventLoopThread(const ThreadInitCallback &cb,
                                 const std::string &name)
        : loop_(nullptr)
        , exiting_(false)
        , thread_(std::bind(&EventLoopThread::threadFunc, this), name)
        , mutex_()
        , cond_()
        , callback_(cb)
{
}


// 析构函数，负责清理对象资源
EventLoopThread::~EventLoopThread()
{
    exiting_ = true;
    if (loop_ != nullptr)
    {
        loop_->quit();
        thread_.join();
    }
}

// 启动线程并开始运行 EventLoop，返回指向该 EventLoop 的指针
EventLoop* EventLoopThread::startLoop()
{
    thread_.start();  // 启动底层的新线程

    EventLoop *loop = nullptr;
    {
        std::unique_lock<std::mutex> lock(mutex_);
        // 若 loop_ 为空，当前线程进入等待状态，直到条件变量被通知
        while ( loop_ == nullptr)
        {
            cond_.wait(lock);
        }
        loop = loop_;// 将 loop_ 指针赋值给局部变量 loop
    }
    return loop;// 返回指向 EventLoop 对象的指针
}

// 下面这个方法，是在单独的新线程中运行的
// 线程函数，该函数将在新线程中执行
void EventLoopThread::threadFunc()  // 就是thread.cc中的func_
{
    EventLoop loop; // 创建一个独立的eventloop，和上面的线程是一一对应的，one loop per thread

    if (callback_)// 检查是否存在初始化回调函数
    {
        callback_(&loop);// 若存在，调用回调函数并传入新创建的 EventLoop 对象指针
    }

    {
        std::unique_lock<std::mutex> lock(mutex_);
        loop_ = &loop;// 将 loop_ 指针指向新创建的 EventLoop 对象
        cond_.notify_one();// 通知等待在条件变量上的线程，loop_ 指针已更新
    }

    loop.loop(); // EventLoop loop => Poller.poll
    std::unique_lock<std::mutex> lock(mutex_);
    loop_ = nullptr;
}