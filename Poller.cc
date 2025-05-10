#include "Poller.h"
#include "Channel.h"

// poller的构造函数主要就是记录所属的eventloop
Poller::Poller(EventLoop *loop)
    : ownerLoop_(loop)
{
}

// 判断指定channel是否在当前Poller中
bool Poller::hasChannel(Channel *channel) const
{
    auto it = channels_.find(channel->fd());
    return it != channels_.end() && it->second == channel; 
}

