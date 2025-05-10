**基于C++11新特性重写muduo网络库的核心TcpServer （2024.12 - 2025.03）**
**描述：**
基于C++11新特性实现的高并发TCP服务器模型核心（TcpServer），模拟muduo网络库的功能。项目采用非阻塞I/O复用机制，实现了高效的网络通信模型，支持多线程服务端网络编程。
**技术栈：**
- C++11特性：采用unique_ptr、shared_ptr、weak_ptr管理内存，使用atomic和unique_lock提升线程安全性。
- 缓冲区设计：借鉴Netty设计理念，支持prepend、read、write标志划分数据区域。
- 日志系统：使用snprintf实现格式化输出，支持不同日志等级分类记录。
- 网络模型：基于one loop per thread的多线程模型，结合Reactor模式实现事件驱动架构。
