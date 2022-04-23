#include"http_conn.h"

//定义http响应的一些状态信息
const char *ok_200_title = "OK";
const char *error_400_title = "Bad Request";
const char *error_400_form = "Your request has bad syntax or is inherently impossible to satisfy.\n";
const char *error_403_title = "Forbidden";
const char *error_403_form = "You do not have permission to get file from this server.\n";
const char *error_404_title = "Not Found";
const char *error_404_form = "The requested file was not found on this server.\n";
const char *error_500_title = "Internal Error";
const char *error_500_form = "There was an unusual problem serving the request file.\n";
/*网站根目录*/
const char* doc_root = "/var/www/html";

int setnonblocking(int fd)
{
    int old_option = fcntl(fd, F_GETFL);
    int new_option = old_option | O_NONBLOCK;
    fcntl(fd, F_SETFL, new_option);
    return old_option;
}

void addfd(int epollfd, int fd, bool one_shot)
{
    struct epoll_event event;
    event.data.fd = fd;
    event.events = EPOLLIN | EPOLLET | EPOLLRDHUP;
    if(one_shot)
    {
        event.events |= EPOLLONESHOT;
    }
    epoll_ctl(epollfd, EPOLL_CTL_ADD, fd, &event);
    setnonblocking(fd);
}

//从内核事件表删除描述符
void removefd(int epollfd, int fd)
{
    epoll_ctl(epollfd, EPOLL_CTL_DEL, fd, 0);
    close(fd);
}

//将事件重置为EPOLLONESHOT
void modfd(int epollfd, int fd, int ev)
{
    epoll_event event;
    event.data.fd = fd;
    event.events = ev | EPOLLET | EPOLLONESHOT | EPOLLRDHUP;
    epoll_ctl(epollfd, EPOLL_CTL_MOD, fd, &event);
}

int http_conn::m_user_count = 0;
int http_conn::m_epollfd = -1;

//关闭连接，关闭一个连接，客户总量-1
void http_conn::close_conn(bool real_close)
{
    if(real_close && (m_sockfd != -1))
    {
        removefd(m_epollfd, m_sockfd);
        m_sockfd = -1;
        m_user_count--;
    }
}

//初始化连接，外部调用初始化套接字地址
void http_conn::init(int sockfd, const struct sockaddr_in& addr)
{
    m_sockfd = sockfd;
    m_address = addr;
    
    /*避免TIME_WAIT状态 调试用*/
    int reuse = 1;
    setsockopt(m_sockfd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));

    addfd(m_epollfd, sockfd, true);
    m_user_count++;

    init();
}

//初始化新接受的连接
//check_state默认为分析请求行的状态
void http_conn::init()
{
    m_check_state = CHECK_STATE_REQUESTLINE;
    m_linger = false;

    m_method = GET;
    m_url = 0;
    m_version = 0;
    m_content_length = 0;
    m_host = 0;
    m_start_line = 0;
    m_checked_idx = 0;
    m_read_idx = 0;
    m_write_idx = 0;

    memset(m_read_buf, '\0', READ_BUFFER_SIZE);
    memset(m_write_buf, '\0', WRITE_BUFFER_SIZE);
    memset(m_real_file, '\0', FILENAME_LEN);
}

/*从状态机*/
http_conn::LINE_STATUS http_conn::parse_line()
{
    char temp;
    /*m_checked_idx指向m_read_buf(应用程序读缓冲区)中当前正在分析的字节，m_read_idx指向m_read_buf中客户数据的尾部的下一字节*/
    /*m_read_buf中第0~m_checked_idx字节都已经分析完毕，第m_checked_idx~(m_read_idx-1)字节由下面的循环挨个分析*/
    for(; m_checked_idx < m_read_idx; ++m_checked_idx)
    {
        /*获得当前要分析的字节*/
        temp = m_read_buf[m_checked_idx];
        /*如果当前字节是'\r'回车符，则可能读取到一个完整的行*/
        if(temp == '\r')
        {
            /*'\r'碰巧是目前m_read_buf中的最后一个已经被读入的客户数据，那么此次分析没有读取到一个完整的行*/
            if((m_checked_idx + 1) == m_read_idx)
            {
                return LINE_OPEN;   /*行数据不完整 需要继续读取客户数据*/
            }
            /*如果下一个字符是'\n'换行符，则说明成功读取到一个完整的行*/
            else if(m_read_buf[m_checked_idx + 1] == '\n')
            {
                m_read_buf[m_checked_idx++] = '\0';
                m_read_buf[m_checked_idx++] = '\0';
                return LINE_OK;     /*读取到一个完整的行*/
            }
            /*否则 客户发送的HTTP请求存在语法问题*/
            return LINE_BAD;
        }
        /*如果当前字节是'\n'换行符，也有可能读取到一个完整的行*/
        else if(temp == '\n')
        {
            if(m_checked_idx > 1 && m_read_buf[m_checked_idx - 1] == '\r')
            {
                m_read_buf[m_checked_idx - 1] = '\0';
                m_read_buf[m_checked_idx++] = '\0';
                return LINE_OK;
            }
            return LINE_BAD;
        }
    }
    /*所有内容分析完毕也没有遇到'\r'字符，则返回LINE_OPEN，说明需继续读取客户数据*/
    return LINE_OPEN;
}

//循环读取客户数据，直到无数据可读或对方关闭连接
bool http_conn::read_once()
{
    if(m_read_idx >= READ_BUFFER_SIZE)
    {
        return false;
    }

    int bytes_read = 0;
    while(true)
    {
        //不论是客户还是服务器应用程序都用recv函数从TCP连接的另一端接收数据
        bytes_read = recv(m_sockfd, m_read_buf + m_read_idx, READ_BUFFER_SIZE - m_read_idx, 0);
        if(bytes_read == -1)\
        {
            //非阻塞ET工作模式下，需要一次性将数据读完  (EAGAIN和EWOULDBLOCK等价)
            if(errno == EAGAIN || errno == EWOULDBLOCK)
            {
                break;
            }
            return false;
        }
        else if(bytes_read == 0)
        {
            return false;
        }
        m_read_idx += bytes_read;
    }
    return true;
}

/*分析请求行*/
http_conn::HTTP_CODE http_conn::parse_request_line(char* text)
{
    /*
    HTTP请求行格式：
        方法字段(char* method)+URL字段(char* url)+HTTP版本字段(char* version)
    */

    /*strpbrk函数 返回两个字符串首个相同字符在字符串1的位置*/
    m_url = strpbrk(text, " \t");
    /*如果请求行中没有空白字符或'\t'字符，则HTTP请求必有问题*/
    if(!m_url)
    {
        return BAD_REQUEST; /*客户请求有语法错误*/
    }
    *m_url++ = '\0';
    char* method = text;
    /*strcasecmp函数 忽略大小写判断字符串是否相等 对空终止字符串进行操作*/
    if(strcasecmp(method, "GET") == 0)
    {
        m_method = GET;
    }
    else
    {
        /*仅支持GET方法*/
        return BAD_REQUEST;
    }
    /*从字符串1的第一个元素开始往后数，看字符串1中是不是连续往后每个字符都在字符串2中可以找到*/
    /*到第一个不在字符串2的元素为止。看从字符串1第一个开始，前面的字符有几个在字符串2中*/
    m_url += strspn(m_url, " \t");  /*消除掉每个间隔中多余的空格和'\t'*/
    m_version = strpbrk(m_url, " \t");
    if(!m_version)
    {
        return BAD_REQUEST;
    }
    *m_version++ = '\0';
    m_version += strspn(m_version, " \t");
    /*仅支持HTTP/1.1*/
    if(strcasecmp(m_version, "HTTP/1.1") != 0)
    {
        return BAD_REQUEST;
    }
    /*检查URL是否合法*/
    if(strncasecmp(m_url, "http://", 7) == 0)
    {
        m_url += 7;
        /*strchr函数 从字符串1中寻找字符2第一次出现的位置。*/
        m_url = strchr(m_url, '/');
    }
    if(!m_url || m_url[0] != '/')
    {
        return BAD_REQUEST;
    }
    /*HTTP请求行处理完毕 状态转移到头部字段的分析*/
    m_check_state = CHECK_STATE_HEADER;
    return NO_REQUEST;  /*请求不完整 需继续处理*/
}

/*分析头部字段*/
http_conn::HTTP_CODE http_conn::parse_headers(char* text)
{
    /*遇到一个空行 说明得到了一个正确的HTTP请求 首部行和实体之间有一个空行*/
    if(text[0] == '\0')
    {
        /*如果HTTP请求有消息体 则还需要读取m_content_length字节的消息体*/
        /*状态机转移到CHECK_STATE_CONTENT状态*/
        if(m_content_length != 0)
        {
            m_check_state = CHECK_STATE_CONTENT;
            return NO_REQUEST;
        }
        /*否则说明得到一个完整HTTP请求*/
        return GET_REQUEST;
    }
    /*处理Host头部字段*/
    else if(strncasecmp(text, "Host:", 5) == 0)
    {
        text += 5;
        text += strspn(text, " \t");
        m_host = text;
    }
    /*处理Connection头部字段*/
    else if(strncasecmp(text, "Connection:", 11) == 0)
    {
        text += 11;
        text += strspn(text, " \t");
        if(strcasecmp(text, "keep-alive") == 0)
        {
            m_linger = true;
        }
    }
    /*处理Content-Length头部字段*/
    else if(strncasecmp(text, "Content-Length:", 15) == 0)
    {
        text += 15;
        text += strspn(text, " \t");\
        m_content_length = atol(text);
    }
    /*其他头部字段不处理*/
    else
    {
        printf("I can not handle this header %s\n", text);
    }
    return NO_REQUEST;
}

/*没有真正解析HTTP请求消息体 只是判断它是否被完整的读入了*/
http_conn::HTTP_CODE http_conn::parse_content(char* text)
{
    if(m_read_idx >= (m_content_length + m_checked_idx))
    {
        text[m_content_length] = '\0';
        return GET_REQUEST;
    }
    return NO_REQUEST;
}

/*主状态机*/
http_conn::HTTP_CODE http_conn::process_read()
{
    LINE_STATUS line_status = LINE_OK;    /*记录当前行的读取状态*/
    HTTP_CODE ret = NO_REQUEST;     /*记录HTTP请求的处理结果*/
    char* text = 0;
    while(((m_check_state == CHECK_STATE_CONTENT) && (line_status == LINE_OK))
            || ((line_status == parse_line()) == LINE_OK))
    {
        //get_line用于将指针向后偏移，指向未处理的字符
        text = get_line();
        m_start_line = m_checked_idx;     /*记录下一行的起始位置*/
        printf("got 1 http line: %s\n", text);
        /*m_check_state记录主状态机当前状态*/
        switch(m_check_state)
        {
            case CHECK_STATE_REQUESTLINE:
            {   //第一个状态分析请求行
                ret = parse_request_line(text);
                if(ret == BAD_REQUEST)
                {
                    return BAD_REQUEST; /*请求语法有误*/
                }
                break;
            }
            case CHECK_STATE_HEADER:
            {   //第二个状态分析头部字段
                ret = parse_headers(text);
                if(ret == BAD_REQUEST)
                {
                    return BAD_REQUEST; /*请求语法有误*/
                }
                else if(ret == GET_REQUEST)
                {
                    return do_request();    /*生成响应报文*/
                }
                break;
            }
            case CHECK_STATE_CONTENT:
            {   /*分析消息体*/
                ret = parse_content(text);
                if(ret == GET_REQUEST)
                {
                    return do_request();
                }
                line_status = LINE_OPEN;
                break;
            }
            default:
            {
                return INTERNAL_ERROR;
            }
        }
    }
    return NO_REQUEST;
}

/*
当得到一个完整、正确的HTTP请求时 就分析目标文件的属性
如果目标文件存在 对所有用户可读 且不是目录 则使用mmap将其映射到内存地址m_file_address处
并告诉调用者获取文件成功
*/
http_conn::HTTP_CODE http_conn::do_request()
{
    strcpy(m_real_file, doc_root);
    int len = strlen(doc_root);
    strncpy(m_real_file + len, m_url, FILENAME_LEN - len - 1);
    if(stat(m_real_file, &m_file_stat) < 0)
    {
        return NO_RESOURCE;
    }
    if(!(m_file_stat.st_mode & S_IROTH))
    {
        return FORBIDDEN_REQUEST;
    }
    if(S_ISDIR(m_file_stat.st_mode))
    {
        return BAD_REQUEST;
    }

    int fd = open(m_real_file, O_RDONLY);
    m_file_address = (char*)mmap(0, m_file_stat.st_size, PROT_READ, MAP_SHARED, fd, 0);
    close(fd);
    return FILE_REQUEST;
}

/*对内存映射取执行munmap操作*/
void http_conn::unmap()
{
    if(m_file_address)
    {
        munmap(m_file_address, m_file_stat.st_size);
        m_file_address = 0;
    }
}

/*写HTTP响应*/
bool http_conn::write()
{
    int temp = 0;
    int bytes_have_send = 0;
    int bytes_to_send = m_write_idx;
    if(bytes_to_send == 0)
    {
        /*事件重置EPOLLONESHOT*/
        modfd(m_epollfd, m_sockfd, EPOLLIN);
        init();
        return true;
    }

    while(1)
    {
        temp = writev(m_sockfd, m_iv, m_iv_count);
        if(temp <= -1)
        {
            /*如果TCP写缓冲没有空间 则等待下一轮EPOLLOUT事件*/
            /*虽然无法接受同一客户的下一个请求 但能保证连接的完整性*/
            if(errno == EAGAIN)
            {
                modfd(m_epollfd, m_sockfd, EPOLLOUT);
                return true;
            }
            unmap();
            return false;
        }

        bytes_to_send -= temp;
        bytes_have_send += temp;
        if(bytes_to_send <= bytes_have_send)
        {
            /*发送HTTP响应成功 根据Connection字段决定是否关闭连接*/
            unmap();
            modfd(m_epollfd, m_sockfd, EPOLLIN);
            if(m_linger)
            {
                init();
                return true;
            }
            else
            {
                return false;
            }
        }
    }
}

/*往写缓冲中写入待发送的数据*/
bool http_conn::add_response(const char* format, ...)
{
    if(m_write_idx >= WRITE_BUFFER_SIZE)
    {
        return false;
    }
    va_list arg_list;
    va_start(arg_list, format);
    int len = vsnprintf(m_write_buf + m_write_idx, WRITE_BUFFER_SIZE - 1 - m_write_idx, format, arg_list);
    if(len >= (WRITE_BUFFER_SIZE - 1 - m_write_idx))
    {
        return false;
    }
    m_write_idx += len;
    va_end(arg_list);
    return true;
}

bool http_conn::add_status_line(int status, const char* title)
{
    return add_response("%s %d %s\r\n", "HTTP/1.1", status, title);
}

bool http_conn::add_headers(int content_len)
{
    add_content_length(content_len);
    add_linger();
    add_blank_line();
}

bool http_conn::add_content_length(int content_len)
{
    return add_response("Content-Length: %d\r\n", content_len);
}

bool http_conn::add_linger()
{
    return add_response("Connection: %s\r\n", (m_linger == true) ? "keep-alive" : "close");
}
bool http_conn::add_blank_line()
{
    return add_response("%s", "\r\n");
}

bool http_conn::add_content(const char* content)
{
    return add_response("%s", content);
}

/*根据服务器处理HTTP请求的结果 决定返回给客户端的内容*/
bool http_conn::process_write(HTTP_CODE ret)
{
    switch(ret)
    {
        case INTERNAL_ERROR:
        {
            add_status_line(500, error_500_title);
            add_headers(strlen(error_500_form));
            if(!add_content(error_500_form))
            {
                return false;
            }
            break;
        }
        case BAD_REQUEST:
        {
            add_status_line(400, error_400_title);
            add_headers(strlen(error_400_form));
            if(!add_content(error_400_form))
            {
                return false;
            }
            break;
        }
        case NO_RESOURCE:
        {
            add_status_line(404, error_404_title);
            add_headers(strlen(error_404_form));
            if(!add_content(error_404_form))
            {
                return false;
            }
            break;
        }
        case FORBIDDEN_REQUEST:
        {
            add_status_line(403, error_403_title);
            add_headers(strlen(error_403_form));
            if(!add_content(error_403_form))
            {
                return false;
            }
            break;
        }
        case FILE_REQUEST:
        {
            add_status_line(200, ok_200_title);
            if(m_file_stat.st_size != 0)
            {
                add_headers(m_file_stat.st_size);
                m_iv[0].iov_base = m_write_buf;
                m_iv[0].iov_len = m_write_idx;
                m_iv[1].iov_base = m_file_address;
                m_iv[1].iov_len = m_file_stat.st_size;
                m_iv_count = 2;
                return true;
            }
            else
            {
                const char* ok_string = "<html><body></body></html>";
                add_headers(strlen(ok_string));
                if(!add_content(ok_string))
                {
                    return false;
                }
            }
        }
        default:
        {
            return false;
        }
    }
    m_iv[0].iov_base = m_write_buf;
    m_iv[0].iov_len = m_write_idx;
    m_iv_count = 1;
    return true;
}

/*由线程池中的工作线程调用 处理HTTP请求的入口函数*/
void http_conn::process()
{
    HTTP_CODE read_ret = process_read();
    //NO_REQUEST 表示请求不完整，需要继续接受请求数据
    if(read_ret == NO_REQUEST)
    {
        modfd(m_epollfd, m_sockfd, EPOLLIN);
        return;
    }
    //调用process_write完成报文响应
    bool write_ret = process_write(read_ret);
    if(!write_ret)
    {
        close_conn();
    }
    modfd(m_epollfd, m_sockfd, EPOLLOUT);
}