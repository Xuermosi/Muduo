#pragma once

#include <unistd.h>
#include <sys/syscall.h>

// 用于获取当前线程id
namespace CurrentThread
{
    // 保存tid缓存 因为系统调用非常耗时 拿到tid后将其保存
    extern __thread int t_cachedTid;

    void cacheTid();

    // 内联函数只在当前文件中起作用
    inline int tid()
    {
        // __builtin_expect 是一种底层优化 此语句意思是如果还未获取tid 进入if 通过cacheTid()系统调用获取tid
        if (__builtin_expect(t_cachedTid == 0, 0))
        {
            cacheTid();
        }
        return t_cachedTid;
    }
}