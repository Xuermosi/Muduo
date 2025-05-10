#include "EPollPoller.h"
#include "Logger.h"
#include "Channel.h"

#include <errno.h>
#include <unistd.h>
#include <cstring>

// channel未添加到poller中
const int kNew = -1;
// channel 已添加到poller中
const int kAdded = 1;
// channel从poller中删除
const int kDeleted = 2;

EPollPoller::EPollPoller(EventLoop* loop)
    : Poller(loop)
    , epollfd_(::epoll_create1(EPOLL_CLOEXEC))
    , events_(kInitEventListSize) // vector<epoll_event>
{
    if (epollfd_ < 0)
    {
        LOG_FATAL("epoll_create error:%d \n", errno);
    }


}

EPollPoller:: ~EPollPoller() // 对应基类中的虚函数
{
    ::close(epollfd_);
}


/*
 * EventLoop会将自己创建好的ChannelList的地址传给poll
 * poll的作用是通过epoll_wait 监听哪些channel发生了事件
 * 把发生事件的channel通过activeChannels告知给EventLoop
 */
// 监听poll上的所有事件
Timestamp EPollPoller::poll(int timeoutMs, ChannelList* activeChannels)
{
    // 由于频繁调用poll 实际上应该使用LOG_DEBUG更合理 当遇到并发场景 关闭DEBUG日志提升效率
    LOG_INFO("func=%s => fd total count:%lu \n", __FUNCTION__, channels_.size());
    // events_.begin()返回的是起始迭代器，解引用就是起始元素，再加个&就是首个元素的地址了。
    int numEvents = ::epoll_wait(epollfd_, &*events_.begin(), static_cast<int>(events_.size()), timeoutMs);
    int saveErrno = errno; // 记录全局变量errno
    Timestamp now(Timestamp::now());

    if (numEvents > 0)
    {
        LOG_INFO("%d events happend\n", numEvents); // LOG_DEBUG最合理
        fillActiveChannels(numEvents, activeChannels);
        if (numEvents == events_.size()) // 返回发生事件的个数和eventlist中的事件个数一样则需要进行扩容
        {
            events_.resize(events_.size() * 2);
        }

    }
    else if (numEvents == 0)
    {
        LOG_DEBUG("%s timeout!\n", __FUNCTION__);
    }
    else
    {
        if (saveErrno != EINTR) // 外部中断
        {
            errno = saveErrno;
            LOG_ERROR("EPollPoller::poll() error!");
        }
    }
    return now;
    // poll end
}

// channel update remove => EventLoop updateChannel removeChannel => Poller updateChannel removeChannel
/*
 *      EventLoop => poller.poll
 *ChannelList    Poller
 *               ChannelMap <fd, channel*>
 *Loop中的channellist中包含了所有的channel，当channel注册到poll中的时候，
 *注册到poll的channel会记录在poller的channelmap。
 */
void EPollPoller::updateChannel(Channel *channel)
{
    const int index = channel->index();
    LOG_INFO("func=%s => fd=%d events=%d index=%d \n", __FUNCTION__, channel->fd(), channel->events(), index);

    if (index == kNew || index == kDeleted) // 未添加或者已删除
    {
        if (index == kNew)
        {
            int fd = channel->fd();
            channels_[fd] = channel;
        }
        else // index == kDeleted
        {
        }
        channel->set_index(kAdded);
        update(EPOLL_CTL_ADD, channel);  //往poller中添加channel
    }
    else // channel已经在Poller中注册过了
    {
        int fd = channel->fd();
        if (channel->isNoneEvent()) // channel对任何事件不感兴趣
        {
            update(EPOLL_CTL_DEL, channel);// 从poller中删除channel
            channel->set_index(kDeleted);  // channel标记为已删除
        }
        else
        {
            update(EPOLL_CTL_MOD, channel); //修改channel感兴趣的事件
        }
    }
}

// 从Poller中删除channel
void EPollPoller::removeChannel(Channel *channel)
{
    int fd = channel->fd();
    channels_.erase(fd);

    LOG_INFO("func=%s => fd = %d\n", __FUNCTION__, fd);

    int index = channel->index();
    if (index == kAdded) // 该channel已添加，需要从poller移除
    {
        update(EPOLL_CTL_DEL, channel);
    }
    channel->set_index(kNew); // channel不在poller中了 index需置为kNew
}

// 填写活跃的连接
void EPollPoller::fillActiveChannels(int numEvents, ChannelList *activeChannels) const
{
    for (int i = 0; i < numEvents; ++i)
    {
        Channel *channel = static_cast<Channel *>(events_[i].data.ptr);
        channel->set_revents(events_[i].events);
        activeChannels->push_back(channel); // EventLoop就拿到了它的Poller给它返回的所有发生的事件列表
    }
}

// 更新channel通道 其实就是调用epoll_ctl add/mod/del
void EPollPoller::update(int operation, Channel *channel)
{
    epoll_event event;
    ::memset(&event, 0, sizeof(event));

    int fd = channel->fd();
    
    event.events = channel->events();
    event.data.fd = fd;
    event.data.ptr = channel;
    

    if (::epoll_ctl(epollfd_, operation, fd, &event) < 0) // 返回-1表示操作失败
    {
        if (operation == EPOLL_CTL_DEL) // 删除失败
        {
            LOG_ERROR("epoll_ctl del error:%d\n", errno);
        }
        else
        {
            LOG_FATAL("epoll_ctl add/mod error:%d\n", errno);
        }
    }
}