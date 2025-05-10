#include <errno.h>
#include <sys/uio.h>
#include <unistd.h>

#include "Buffer.h"

/**
 * 从fd上读取数据 Poller工作在LT模式
 * Buffer缓冲区是有大小的！ 但是从fd上读取数据的时候 却不知道tcp数据的最终大小
 *
 * @description: 从socket读到缓冲区的方法是使用readv先读至buffer_，
 * Buffer_空间如果不够会读入到栈上65536个字节大小的空间，然后以append的
 * 方式追加入buffer_。既考虑了避免系统调用带来开销，又不影响数据的接收。
 **/

ssize_t Buffer::readFd(int fd, int *saveErrno)
{
    char extrabuf[65536] = {0}; // 栈上的内存空间
    /*
    struct iovec {
        ptr_t iov_base; // iov_base指向的缓冲区存放的是readv所接收的数据或是writev将要发送的数据
        size_t iov_len; // iov_len在各种情况下分别确定了接收的最大长度以及实际写入的长度
    };
    */
    // 使用iovec分配两个连续的缓冲区
    struct iovec vec[2];
    const size_t writable = writableBytes();    // 这是Buffer底层缓冲区剩余的可写空间大小 不一定能完全存储从fd读出的数据
   
    // 第一块缓冲区，指向可写空间
    vec[0].iov_base = begin() + writerIndex_;
    vec[0].iov_len = writable;
    // 第二块缓冲区，指向栈空间
    vec[1].iov_base = extrabuf;
    vec[1].iov_len = sizeof(extrabuf);

    // 如果第一个缓冲区>=64k 那就只采用一个缓冲区 而不使用栈空间extrabuf[65536]的内容
    // 如果第一个缓冲区<64k 则采用两个缓冲区
    const int iovcnt = (writable < sizeof(extrabuf)) ? 2 : 1;
    const ssize_t n = ::readv(fd, vec, iovcnt); // n 表示从fd读取的数据大小
    if (n < 0)
    {
       *saveErrno = errno; 
    }
    else if (n <= writable) // Buffer的可写缓冲区已经够存储读出来的数据了
    {
       writerIndex_ += n; 
    }
    else // extrabuf里面也写入了n-writable长度的数据
    {
        writerIndex_ = buffer_.size(); 
        append(extrabuf, n - writable); // 对buffer_扩容 并将extrabuf存储的另一部分数据追加至buffer_
    }
    return n;
}

ssize_t Buffer::writeFd(int fd, int *saveErrno)
{
    ssize_t n = ::write(fd, peek(), readableBytes());
    if (n < 0)
    {
        *saveErrno = errno;
    }
    return n;
}