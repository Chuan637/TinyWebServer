# TinyWebServer
Linux下C++轻量级Web服务器

# 实现
半同步/半反应堆的并发模式**线程池**

**epoll** I/O复用 **ET模式**

使用**状态机**解析HTTP请求报文，支持解析GET和POST请求

**定时器**关闭非活动连接

# 参考
[@qinguoyi](https://github.com/qinguoyi/TinyWebServer)

《Linux高性能服务器编程》 游双 著