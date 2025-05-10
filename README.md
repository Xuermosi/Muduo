基于C++11新特性实现的高并发TCP服务器模型核心（TcpServer），模拟muduo网络库的功能。项目采用非阻塞I/O复用机制，实现了高效的网络通信模型，支持多线程服务端网络编程。

主要特点：

核心模块实现

实现了Acceptor、EventThreadPool、Channel、Poller、EventLoop、Buffer、TcpConnection、Logger等关键模块。
采用one loop per thread的多线程模型，结合Reactor模式，实现高效的事件驱动架构。
技术栈优化

内存管理：使用智能指针（unique_ptr、shared_ptr、weak_ptr）对Poller、Channel等模块进行内存资源管理，确保资源安全释放。
线程安全：采用atomic原子操作类型保护状态变量，使用unique_lock替代lock_guard，提升多线程环境下的性能和安全性。
缓冲区设计：借鉴Netty的设计理念，实现高效的缓冲区管理，支持prepend、read、write标志划分数据区域，提升数据处理效率。
日志系统：使用snprintf实现日志的格式化输出，支持不同日志等级的分类记录。
性能与功能提升

将原muduo库从依赖Boost库转变为完全基于C++标准库实现，减少外部依赖，提升代码的可移植性和维护性。
通过非阻塞I/O复用和多线程模型，显著提升了服务器的并发处理能力。
使用智能指针和原子操作，降低了内存泄漏和竞态条件的风险，提升了代码的健壮性。
成果展示

基于重写的网络库，成功实现了EchoServer的开发和测试，验证了TcpServer模型的稳定性和高效性。
