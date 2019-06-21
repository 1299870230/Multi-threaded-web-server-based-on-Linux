#ifndef HTTPCONNECTION_H
#define HTTPCONNECTION_H
#include<unistd.h>
#include<signal.h>
#include<sys/types.h>
#include<sys/epoll.h>
#include<fcntl.h>
#include<sys/socket.h>
#include<netinet/in.h>
#include<arpa/inet.h>
#include<assert.h>
#include<sys/stat.h>
#include<string.h>
#include<pthread.h>
#include<stdio.h>
#include<stdlib.h>
#include<sys/mman.h>
#include<stdarg.h>
#include<errno.h>
#include<sys/wait.h>
#include"locker.h"
//HTTP连接任务类型，用于线程池工作队列的任务类型T  
class http_conn{
    public:
		//文件名的最大长度
        static const int FILENAME_LEN=200;
		//读缓冲区大小
        static const int READ_BUFFER_SIZE=2048;
		//写缓冲区大小
        static const int WRITE_BUFFER_SIZE=1024;
		//HTTP请求方法：
        enum METHOD{GET=0,POST,HEAD,PUT,DELETE,TRACE,OPTIONS,CONNECT,PATH};
		//HTTP请求状态：
		//CHECK_STATE_REQUESTLINE：当前正在分析请求行
		//CHECK_STATE_HEADER：当前正在分析请求头
		//CHECK_STATE_CONTENT：
        enum CHECK_STATE{CHECK_STATE_REQUESTLINE=0,CHECK_STATE_HEADER,CHECK_STATE_CONTENT};
        //HTTP请求结果：
		//NO_REQUEST：请求不完整，需要继续读取客户数据
		//GET_REQUEST：获得了一个完整的客户请求
		//BAD_REQUEST：客户请求有语法错误
		
		//NO_RESOURCE：
		//FORBIDDEN_REQUEST：客户对资源没有没有足够的访问权限
		//FILE_REQUEST
		//INTERNAL_ERROR：服务器内部错误
		//CLOSED_CONNECTION：客户端已经关闭连接
		enum HTTP_CODE{NO_REQUEST,GET_REQUEST,BAD_REQUEST,NO_RESOURCE,FORBIDDEN_REQUEST,FILE_REQUEST,INTERNAL_ERROR,CLOSED_CONNECTION};
        //HTTP每行解析状态：
		//LINE_OK：读取到完整的行
		//LINE_BAD：行出错
		//LINE_OPEN：行数据尚且不完整
		enum LINE_STATUS{LINE_OK=0,LINE_BAD,LINE_OPEN};
    public:
        http_conn(){}
        ~http_conn(){}
    public:
		//初始化新的HTTP连接  
        void init(int sockfd,const sockaddr_in &addr);
        void close_conn(bool real_close=true);
		//处理客户请求
        void process();
		//循环读取客户数据，直到无数据可读或者对方关闭连接(HTTP请求)  
        bool read_1();
		//将请求结果返回给客户端  
        bool write();
        sockaddr_in *get_address(){
            return &m_address;
        }
    private:
		//重载init初始化连接，用于内部调用  
        void init();
		//解析HTTP请求,内部调用parse_系列函数  
        HTTP_CODE process_read();
        bool process_write(HTTP_CODE ret);
		//解析HTTP请求行，获得请求方法、目标URL，以及HTTP版本号
        HTTP_CODE parse_request_line(char *text);
        HTTP_CODE parse_headers(char *text);
        HTTP_CODE parse_content(char *text);
        HTTP_CODE do_request();
        char* get_line(){return m_read_buf+m_start_line;};
		//检查请求行
        LINE_STATUS parse_line();
        void unmap();
        bool add_response(const char* format,...);
        bool add_content(const char* content);
        bool add_status_line(int status,const char* title);
        bool add_headers(int content_length);
        bool add_content_length(int content_length);
        bool add_linger();
        bool add_blank_line();
    public:
        static int m_epollfd;
        static int m_user_count;
    private:
        int m_sockfd;
        sockaddr_in m_address;
        char m_read_buf[READ_BUFFER_SIZE];
        int m_read_idx;
        int m_checked_idx;
        int m_start_line;
        char m_write_buf[WRITE_BUFFER_SIZE];
        int m_write_idx;
        CHECK_STATE m_check_state;
        METHOD m_method;
        char m_real_file[FILENAME_LEN];
		//客户请求的目标文件的文件名
        char *m_url;
        char *m_version;
        char *m_host;
        int m_content_length;
        bool m_linger;
		//客户请求的目标文件被mmap到内存中的起始位置
        char *m_file_address;
        struct stat m_file_stat;
        struct iovec m_iv[2];
        int m_iv_count;
        int cgi;	   //是否启用的POST
        char *m_string;//存储请求头数据
};
#endif
