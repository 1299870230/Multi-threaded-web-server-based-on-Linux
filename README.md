基于Linux下多线程的web服务器   
===========================
# 一.服务器基本框架   
## 应用语言：  
c/c++语言  
## 项目介绍：  
### 1、服务器的功能：  
结合线程池实现的一个并发web服务器，能够解析HTTP的GET请求，支持HTTP长连接，使用浏览器访问可以返回对应的内容。  
  
### 2、服务器的架构：采用同步I/O模拟Proactor模式（事件驱动+非阻塞IO）+ 线程池  
1）服务器的模型:  
>C/S模型  
  
2）I/O模型:  
>I/O复用，常用的I/O复用函数有select、poll、epoll_wait，本项目所用I/O复用函数为epoll_wait。  
>而且为了提高效率，采用ET工作模式。  
>除了可以提高效率，还有一个原因：如果将一个具有读事件的fd的任务对象放入生产者-消费者队列中后，拿到这个任务的工作线程却没有读完这个fd，因为没读完数>据，所以这个fd可读，那么下一次事件循环又返回这个fd，这时分给别的线程，就会出现数据一半给1号线程，另一半给了2号线程，这样就没法处理完整的数据，那怎么处理？（注：水平触发LT每次读取数据，没读完fd的数据就注册事件，等到下一个epollwait再处理。边缘触发ET每次都需要读完数据，用了while循环读取输入数据。）于是采用边缘触发ET，边缘触发中输入缓冲收到数据仅注册一次该事件，即使输入缓冲中还有数据，也不会注册事件。具体用法是：将文件描述符fd设置为非阻塞，如果该fd可读，就使用一个while循环读取输入数据，直到返回-1且EAGAIN为止。        
>除此之外，还有一个原因：即使我们使用了ET模式，一个socket上的某个事件也可能被触发多次，这会在并发程序中引发一个问题，比如一个线程在读取完某个socket上的数据后开始处理这些数据，而在处理过程中，该socket上又有新数据可读（这里注意与前一种原因的区别，前面是处理同一批数据，而这里是新来的一批数据），此时另外一个线程会被唤醒来读取这些新的数据，这时就会出现两个线程同时操作一个socket的现象，这样当然是不行的，这一点可以使用epoll的EPOLLONESHOT事件解决。因为注册了EPOLLONSESHOT事件的文件描述符，操作系统最多触发其上注册的一个可读、可写或者异常事件，且只触发一次，除非我们使用epoll_ctl函数重置该文件描述符上注册的EPOLLONESHOT事件。这样，当一个线程在处理某个socket时，其他线程是不可能有机会操作该socket的。同时一旦注册了EPOLLONSESHOT事件的socket被某个线程处理完毕，该线程就应该立即重置这个socket上的EPOLLONESHOT事件，以确保这个socket下一次可读时，其EPOLLIN事件能被触发，进而让其他工作线程有机会继续处理这个socket。  
  
3）事件处理模式：
>使用同步I/O方式模拟Proactor模式。  
>首先介绍Reactor模式，它要求主线程只负责监听文件描述符上是否有事件发生，如果有就立即通知工作线程。除此之外，主线程不做任何其他实质性的工作。例如读写数据，接受新的连接，以及处理客户请求均在工作线程中完成。  
接下来是Proactor模式，它将所有I/O操作都交给主线程和内核来处理，工作线程仅负责业务逻辑，所以相比Reactor模式，Proactor模式更符合服务器编程框架。  
而模拟Proactor模式，就是在主线程执行数据读写操作，读写完成之后，主线程向工作线程通知这一“完成事件”。而对于工作线程，直接获取数据读写的结果，之后仅是对数据读写结果进行逻辑处理。  
>具体工作流程如下：  
>>a）主线程往epoll内核事件表中注册socket上的读就绪事件。  
>>b）主线程调用epoll_wait等待socket上有数据可读。  
>>c）当socket上有数据可读时，epoll_wait通知主线程。主线程从socket循环读取数据，直到没有更多数据可读，然后将读到的数据封装成一个请求对象并插入请求队列，这里我采用了线程池，所以在定义线程池类中封装了一个append函数，用来往请求队列中添加任务。  
>>d）睡眠在请求队列上的某个工作线程被唤醒，它获得请求对象并处理客户请求，然后往epoll内核事件表中注册socket上的写就绪事件。  
>>e）主线程调用epoll_wait等待socket可写。  
>>f）当socket可写时，epoll_wait通知主线程。主线程往socket上写入服务器处理客户端请求的结果。  
  
4）并发模式：  
>半同步/半反应堆模式，该模式使用模拟的Proactor事件处理模式。异步线程只有一个，由主线程来充当，它负责监听所有socket上的事件，如果监听socket上有可读事件发生，即有新的连接请求到来，主线程就接受它来得到新的连接socket，然后往epoll内核事件表中注册该socket上的读写事件。如果连接socket上有读写事件发生，即有新的客户请求到来或有数据要发送给客户端，由于采用的事件处理模式是模拟的Proactor，所以首先有主线程完成数据的读写，在这种情况下，主线程会将应用程序数据、任务类型等信息封装为一个任务对象，然后将该对象插入请求队列，这个插入的函数，在定义线程池类的中封装了一个append函数。这时，所有工作线程都睡眠在请求队列上，当有任务到来，它们通过竞争（我利用的信号量）获得任务的接管权。这种竞争机制使得只有空闲的工作线程才有机会来处理新任务。  
  
5）线程池：  
>为了节约系统开销，采用线程池，因此封装了一个线程池类，主要采用的是半同步/半反应堆的并发模式，因为使用一个工作队列可以完全解除了主线程和工作线程的耦合关系：主线程往工作队列中插入任务，工作线程通过竞争来获取并执行它。（这种方式必须保证请求无状态，因为同一个连接的请求可能会被不同工作线程处理）  
  
6）解决线程之间：  
>竞争同时基于oop的设计思想，封装了一个锁类（包含互斥锁、信号量及条件变量）解决线程之间的竞争问题。  
  
7）逻辑单元实现：  
>最后对于服务器的逻辑单元，主要是关于http的解析和应答的实现，本项目采用有效状态机这种高效编程方式，同时在应答的实现过程中，为了发送高效，利用writev函数。其功能可以简单概括为对数据进行整合传输及发送，即所谓分散读，集中写。将数据进行整合再传输，是因为一个HTTP响应，其包含一个状态行，多个头部字段，一个空行和文档的内容。前三者可能被web服务器放置在一块内存中，而文档的内容则通常被读入到另外一块单独的内存中（通过read或mmap函数），最后通过writev函数将他们一并发出。  
8）忽略SIGPIPE信号:  
>这是一个看似很小，但是如果不注意会直接引发bug的地方。如果往一个读端关闭的管道或者socket中写数据，会引发SIGPIPE，程序收到SIGPIPE信号后默认的操作时终止进程。也就是说，如果客户端意外关闭，那么服务器可能也就跟着直接挂了，这显然不是我们想要的。所以网络程序中服务端一般会忽略SIGPIPE信号。  
# 二.加入时间定时器功能  
>加入定时器的目的是：处理非活动连接。服务器程序通常需要定期处理非活动连接：给客户端发送一个重连请求或关闭连接。Linux内核提供了对连接是否处于活动状态的定期检查机制，通过socket选项KEEPALIVE来激活它，但是该方法将使得应用程序对连接的管理变的复杂，所以可以考虑在应用层实现类似KEEPALIVE的机制。利用alarm函数定时产生SIGALRM信号的原理设计定时器，常用的定时器主要有基于时间上升序列定时器、时间轮以及最小时间堆，后两者更高效，因为添加定时器时间复杂度更小。但是为了方便设计，我采用了基于时间升序链表的的定时器，基于时间升序链表的方法就是利用双向链表的设计思想，每个节点保存有定时的相关信息，安照时间长短由短到长排列，通过心跳检测函数定期检测到期时间的定时，并调用相关的处理函数进行处理。  
>主要工作流程：利用alarm函数周期性的触发SIGALRM信号，该信号的信号处理函数利用管道，通知主循环执行定时器链表上的定时任务--关闭非活动的连接
>为了方便统一管理，采用了统一事件源的事件处理方式:信号处理函数和程序的主循环是两条不同的执行路线。因为信号处理函数需要尽可能地执行完毕，以确保该信号不被屏蔽太久。一种典型的解决方案是：把信号的主要处理逻辑放到程序的主循环中，当信号处理函数被触发时，它只是简单地通知主循环程序接收到信号，并把信息值传递给主循环，主循环在根据接收到地信号值执行目标信号对应的逻辑代码。信号处理函数通常使用管道来将信号“传递”给主循环：信号处理函数往管道的写端写入信号值，主循环则从管道的读端读出该信号值,利用I/O复用检测。  
# 三.建立服务器日志系统  
>服务器日志主要涉及了两个模块，一个是日志模块，一个是日志条队列模块;其中加入日志条队列模块主要是解决异步写入日志做准备，如果不用异步日志模块，则可以不用。加入异步写日志模块的主要是解决，当当单条日志比较大的时候，同步模式会阻塞整个处理流程，那么应用能处理的并发能力将有所下降，尤其是在峰值的时候，写日志可能成为系统的瓶颈，异步的好处就是在峰值的时候能够平滑过渡，降低写日志的压力。缺点就是在服务崩溃或者重启服务的时候可能会造成部分日志丢失，同步写的话就不会造成，因此可以视应用场景来选择采用写日志的主要模式。  
>日志模块主要采用多线程下安全的单例模式设计，可以支持自动按天分文件，按日志行数自动分文件。详细设计见源码，简单介绍下使用方法：  
>>1.首先在主文件中初始化日志函数，可选择的参数有日志文件、日志缓冲区大小、最大行数以及最长日志条队列（如果为0采用同步写，大于0则采用异步写，其中由于STL序列中的队列函数在多线程中不安全，采用自定义的安全队列）  
>>2.初始化日志函数之后，便可以直接在需要输出日志的地方（如来源、错误信息等）直接使用，有两种使用方式，一是直接使用对应的宏，二是使用实例的成员函数;具体可以参考使用的源码。  
# 四.数据库连接池的实现  
1)设计背景：  
>一般的应用程序都会对数据库进行访问，在程序访问数据库的时候，每一次数据访问请求都必须经过下面几个步骤：建立数据库连接，打开数据库，对数据库中的数据进行操作，关闭数据库连接。而其中建立数据库连接和打开数据库是一件很消耗资源并且费时的工作，如果在系统中很频繁的发生这种数据库连接，必然会影响到系统的性能，甚至会导致系统的崩溃。  
2）引入数据库连接池的思想：  
>在系统初始化阶段，建立一定数量的数据库连接对象，并将其存储在连接池中定义的容器中(主要利用单例设计模式来初始化一个连接池以及一定数量的与数据库连接对象，保证唯一性)。当有数据库访问请求时，就从连接池中的这个容器中拿出一个连接；当容器中的连接已经用完，就要等待其他访问请求将连接放回容器后才能使用。当使用完连接的时候，必须将连接放回容器中，这样不同的数据库访问请求就可以共享这些连接，通过重复使用这些已经建立的数据库连接，可以解决上节中说到的频繁建立连接的缺点，从而提高了系统的性能。  
>具体操作：  
>>1)首先建立一个数据库连接池对象;  
>>2)初始化数据库连接，放入连接池对象的容器中;  
>>3)当有数据库访问请求时，直接从连接池对象的容器中得到一个连接;  
>>4)当数据库访问完成后，应该将连接放回连接池的容器中;  
>>5)当服务停止时，需要先释放数据库连接池中的所有数据库连接，然后再释放其对象。  
>本项目关于数据库的操作是验证服务器输入的用户名和密码是否存在于服务器端的数据库中，数据库是mysql，具体流程为：每次得到一个与数据库的连接后，执行数据库的操作得到表中的用户名和密码的数据，然后将从数据库得到的数据与从客户端发送来的的http中解析出来的数据进行比对，进而更改客户端请求的目标文件名。
# 五.一个简单的服务器压力测试  
>压力测试程序有很多实现方法，比如I/O复用方式，多线程，多进程并发编程方式。在这里，因为单纯的I/O复用方式施压程度是最高的，因为线程和进程调度本身也要占用一定的CPU时间。因此，设计一个epoll实现的通用服务器压力测试程序。  
>主要思想是先创建指定的连接数(可自己定义，程序限定的为10000)，然后加入EPOLL中监听事件，与服务器进行数据的不断交换，如果服务器足够稳定，则会一直进行下去。 
# 六.服务器CGI的实现  
>这里基于数据库模块设计了一个简单的服务器CGI处理浏览器一些针对数据库的请求（与第四个功能处理的目标相同，验证用户名以及用户密码），然后将返回结果重新发送给服务器，服务器根据处理结果返回对应的结果给浏览器。  
>具体流程:  
>>创建CGI程序，本项目利用四的数据库连接处实现与数据库的连接以及数据的处理。  
>>然后web服务器接收客户端的请求。此处，在编写HTML中，添加在form标签中添加action属性，用来在提交表单后，更改客户端的目标文件为CGI文件。  
>>web服务器接收用户请求并交给CGI程序处理。此处，建立管道，fork，在子进程接收到账户名和密码的信息，然后执行CGI程序，产生相应的结果，传入写管道。  
>>CGI程序把处理结果传送给web服务器。此处，在父进程中通过读管道，将CGI程序的处理结果读出来，然后更改客户端的目标文件名。  
>>web服务器把结果送回到用户。此处，经过读目标文件等操作后，服务器将响应结果再送回客户端。  
>需要注意的是要将可执行的的CGI程序(c++)放到对应的目录处，这里是放在与请求目标文件同一个文件夹下。  
  
##以上仅是项目的框架以及设计思路，具体请见代码。
