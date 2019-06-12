# Multi-threaded-web-server-based-on-Linux
Build a web server
应用语言：c/c++语言
项目介绍：
1.服务器的模型：C/S模型
2.事件处理模式：Reactor模式，主线程负责监听文件描述符上是否有事件发生，如果有事件发生，就通知工作线程，主线程不做任何其他实质性的工作。
3.I/O模型：I/O复用，常用的I/O复用函数有select、poll、epoll_wait。本项目所用I/O模型为epoll_wait。
