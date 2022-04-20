#include<iostream>
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
#include<cstring>
#include<pthread.h>
#include<cstdlib>
#include<sys/mman.h>
#include<cstdarg>
#include<errno.h>
#include<sys/wait.h>
#include<sys/uio.h>
#include<map>

#include"../lock/myLock.h"

class http_conn {
public:
    //设置读取文件的名称m_real_file的大小
    static const int FILENAME_LEN = 200;
    //设置读读缓冲区m_read_buf的大小
    static const int READ_BUFFER_SIZE = 2048;
    //设置写缓冲区m_write_buf的大小
    static const int WRITE_BUFFER_SIZE = 1024;
    //报文的请求方法，本项目只用到GET和POST
    enum METHOD{
        GET = 0,
        POST,
        HEAD,
        PUT,
        DELETE,
        TRACE,
        OPTIONS,
        CONNECT,
        PATH
    };
    //主状态机的状态
    enum CHECK_STATE{
        //解析请求行
        CHECK_STATE_REQUESTLINE = 0,
        //解析请求头
        CHECK_STATE_HEADER,
        //解析消息体 仅用于POST请求
        CHECK_STATE_CONTENT
    };
    //报文解析的结果
    enum HTTP_CODE{
        NO_REQUEST,     //请求不完整 需要继续读取请求报文
        GET_REQUEST,    //获得了完整的HTTP请求
        BAD_REQUEST,    //HTTP请求报文有语法错误
        NO_RESOURCE,
        FORBIDDEN_REQUEST,
        FILE_REQUEST,
        INTERNAL_ERROR,
        CLOSED_CONNECTION
    };
    //从状态机的状态
    enum LINE_STATUS{
        //完整读取一行
        LINE_OK = 0,
        //报文语法有误
        LINE_BAD,
        //读取的行不完整
        LINE_OPEN
    };

public:
    http_conn() {}
    ~http_conn() {}

public:
    //初始化套接字地址，函数内部会调用私有方法init
    void init(int sockfd, const sockaddr_in& addr, char*, int, int, std::string user, std::string passwd, std::string sqlname);
    //关闭http连接
    void close_conn(bool real_close = true);
    /*处理客户请求*/
    void process();
    /*非阻塞读操作*/
    bool read_once();
    /*非阻塞写操作*/
    bool write();

private:
    /*初始化连接*/
    void init();
    /*解析HTTP请求*/
    HTTP_CODE process_read();
    /*填充HTTP应答*/
    bool process_write(HTTP_CODE ret);
    //主状态机解析报文中的请求行数据
    HTTP_CODE parse_request_line(char *text);
    //主状态机解析报文中的请求头数据
    HTTP_CODE parse_headers(char *text);
    //主状态机解析报文中的请求内容
    HTTP_CODE parse_content(char *text);
    //生成响应报文
    HTTP_CODE do_request();

    //m_start_line是已经解析的字符
    //get_line用于将指针向后偏移，指向未处理的字符
    char* get_line()
    {
        return m_read_buf + m_start_line;
    }
    //从状态机读取一行，分析是请求报文的哪一部分
    LINE_STATUS parse_line();
    
    /*被process_write调用用以填充HTTP应答*/
    void unmap();
    bool add_response(const char *format, ...);
    bool add_content(const char *content);
    bool add_status_line(int status, const char *title);
    bool add_headers(int content_length);
    bool add_content_type();
    bool add_content_length(int content_length);
    bool add_linger();
    bool add_blank_line();

public:
    /*所有socket事件注册到同一个epoll内核事件中*/
    static int m_epollfd;
    /*统计用户数量*/
    static int m_user_count;

private:
    /*该HTTP连接的socket和对方的socket地址*/
    int m_sockfd;
    struct sockaddr_in m_address;
    /*读缓冲区*/
    char m_read_buf[READ_BUFFER_SIZE];
    /*标识读缓冲中已经读入的客户数据的最后一个字节的下一个位置*/
    int m_read_idx;
    /*当前正在分析的字符在读缓冲区中的位置*/
    int m_checked_idx;
    /*当前正在解析的行的位置*/
    int m_start_line;
    /*写缓冲区*/
    char m_write_buf[WRITE_BUFFER_SIZE];
    /*写缓冲区中待发送的字节数*/
    int m_write_idx;
    
    
    //主状态机的状态
    CHECK_STATE m_check_state;
    //请求方法
    METHOD m_method;
    
    /*客户请求的目标文件的完整路径  doc_root + m_url(doc_root是网站根目录)*/
    char m_real_file[FILENAME_LEN];
    /*客户请求的目标文件的文件名*/
    char *m_url;
    /*HTTP协议版本号 仅支持HTTP/1.1*/
    char *m_version;
    /*主机名*/
    char *m_host;
    /*请求的消息体长度*/
    int m_content_length;
    /*HTTP请求是否要保持连接*/
    bool m_linger;

    /*客户请求的目标文件被mmap到内存的起始位置*/
    char *m_file_address;
    /*目标文件的状态 判断文件是否存在、是否为目录、是否可读 获取文件大小*/
    struct stat m_file_stat;
    /*使用writev来执行写操作*/
    struct iovec m_iv[2];
    int m_iv_count;
};