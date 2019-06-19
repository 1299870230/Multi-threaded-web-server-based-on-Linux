# Multi-threaded-web-server-based-on-Linux  
Build a web server  
应用语言：c/c++语言  
项目介绍：  
一、服务器的功能：  
结合线程池实现的一个并发web服务器，能够解析HTTP的GET请求，支持HTTP长连接，使用浏览器访问可以返回对应的内容。  
二、服务器的架构：采用同步I/O模拟Proactor模式（事件驱动+非阻塞IO）+ 线程池  
1.服务器的模型：C/S模型  
2.I/O模型：I/O复用，常用的I/O复用函数有select、poll、epoll_wait。本项目所用I/O复用函数为epoll_wait。  
而且为了提高效率，采用ET工作模式。  
除了可以提高效率，还有一个原因：如果将一个具有读事件的fd的任务对象放入生产者-消费者队列中后，拿到这个任务的工作线程却没有读完这个fd，因为没读完数据，所以这个fd可读，那么下一次事件循环又返回这个fd，这时分给别的线程，就会出现数据一半给1号线程，另一半给了2号线程，这样就没法处理完整的数据，那怎么处理？（注：水平触发LT每次读取数据，没读完fd的数据就注册事件，等到下一个epollwait再处理。边缘触发ET每次都需要读完数据，用了while循环读取输入数据。）于是采用边缘触发ET，边缘触发中输入缓冲收到数据仅注册一次该事件，即使输入缓冲中还有数据，也不会注册事件。具体用法是：将文件描述符fd设置为非阻塞，如果该fd可读，就使用一个while循环读取输入数据，直到返回-1且EAGAIN为止。        
除此之外，还有一个原因：即使我们使用了ET模式，一个socket上的某个事件也可能被触发多次，这会在并发程序中引发一个问题，比如一个线程在读取完某个socket上的数据后开始处理这些数据，而在处理过程中，该socket上又有新数据可读（这里注意与前一种原因的区别，前面是处理同一批数据，而这里是新来的一批数据），此时另外一个线程会被唤醒来读取这些新的数据，这时就会出现两个线程同时操作一个socket的现象，这样当然是不行的，这一点可以使用epoll的EPOLLONESHOT事件解决。因为注册了EPOLLONSESHOT事件的文件描述符，操作系统最多触发其上注册的一个可读、可写或者异常事件，且只触发一次，除非我们使用epoll_ctl函数重置该文件描述符上注册的EPOLLONESHOT事件。这样，当一个线程在处理某个socket时，其他线程是不可能有机会操作该socket的。同时一旦注册了EPOLLONSESHOT事件的socket被某个线程处理完毕，该线程就应该立即重置这个socket上的EPOLLONESHOT事件，以确保这个socket下一次可读时，其EPOLLIN事件能被触发，进而让其他工作线程有机会继续处理这个socket。  
3.事件处理模式：使用同步I/O方式模拟Proactor模式。  
  首先介绍Reactor模式，它要求主线程只负责监听文件描述符上是否有事件发生，如果有就立即通知工作线程。除此之外，主线程不做任何其他实质性的工作。例如读写数据，接受新的连接，以及处理客户请求均在工作线程中完成。  
  接下来是Proactor模式，它将所有I/O操作都交给主线程和内核来处理，工作线程仅负责业务逻辑，所以相比Reactor模式，Proactor模式更符合服务器编程框架。  
  而模拟Proactor模式，就是在主线程执行数据读写操作，读写完成之后，主线程向工作线程通知这一“完成事件”。而对于工作线程，直接获取数据读写的结果，之后仅是对数据读写结果进行逻辑处理。具体工作流程如下：  
  a.主线程往epoll内核事件表中注册socket上的读就绪事件。  
  b.主线程调用epoll_wait等待socket上有数据可读。  
  c.当socket上有数据可读时，epoll_wait通知主线程。主线程从socket循环读取数据，直到没有更多数据可读，然后将读到的数据封装成一个请求对象并插入请求队列，这里我采用了线程池，所以在定义线程池类中封装了一个append函数，用来往请求队列中添加任务。  
  d.睡眠在请求队列上的某个工作线程被唤醒，它获得请求对象并处理客户请求，然后往epoll内核事件表中注册socket上的写就绪事件。  
  e.主线程调用epoll_wait等待socket可写。  
  f.当socket可写时，epoll_wait通知主线程。主线程往socket上写入服务器处理客户端请求的结果。
4.并发模式：半同步/半反应堆模式，该模式使用模拟的Proactor事件处理模式。异步线程只有一个，由主线程来充当，它负责监听所有socket上的事件，如果监听socket上有可读事件发生，即有新的连接请求到来，主线程就接受它来得到新的连接socket，然后往epoll内核事件表中注册该socket上的读写事件。如果连接socket上有读写事件发生，即有新的客户请求到来或有数据要发送给客户端，由于采用的事件处理模式是模拟的Proactor，所以首先有主线程完成数据的读写，在这种情况下，主线程会将应用程序数据、任务类型等信息封装为一个任务对象，然后将该对象插入请求队列，这个插入的函数，在定义线程池类的中封装了一个append函数。这时，所有工作线程都睡眠在请求队列上，当有任务到来，它们通过竞争（我利用的信号量）获得任务的接管权。这种竞争机制使得只有空闲的工作线程才有机会来处理新任务。
5.线程池：为了节约系统开销，采用线程池，因此封装了一个线程池类，主要采用的是半同步/半反应堆的并发模式，因为使用一个工作队列可以完全解除了主线程和工作线程的耦合关系：主线程往工作队列中插入任务，工作线程通过竞争来获取并执行它。（这种方式必须保证请求无状态，因为同一个连接的请求可能会被不同工作线程处理）  
6.同时基于oop的设计思想，封装了一个锁类（包含互斥锁、信号量及条件变量）解决线程之间的竞争问题。  
7.最后对于服务器的逻辑单元，主要是关于http的解析和应答的实现，本项目采用有效状态机这种高效编程方式，同时在应答的实现过程中，为了发送高效，利用了readv和writev函数。其功能可以简单概括为对数据进行整合传输及发送，即所谓分散读，集中写。将数据进行整合再传输，是因为一个HTTP响应，其包含一个状态行，多个头部字段，一个空行和文档的内容。前三者可能被web服务器放置在一块内存中，而文档的内容则通常被读入到另外一块单独的内存中最后，最后通过writev函数将他们一并发出。  
