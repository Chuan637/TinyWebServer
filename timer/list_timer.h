#ifndef _LISTTIMER_H_
#define _LISTTIMER_H_

#include<time.h>
#define BUFFER_SIZE 64
class util_timer;

/*
用户数据结构：
    客户端socket地址
    socket文件描述符
    读缓存
    计时器
*/
struct client_data{
    struct sockaddr_in address;
    int sockfd;
    char buf[BUFFER_SIZE];
    util_timer* timer;
};

/*定时器类*/
class util_timer{
public:
    util_timer() : prev(nullptr), next(nullptr) {}
public:
    time_t expire;  /*任务超时时间(绝对时间)*/
    void (*cb_func)(client_data*);  /*任务回调函数*/
    /*回调函数处理的客户端连接 由定时器的执行者传递给回调函数*/
    client_data* user_data;
    util_timer* prev;
    util_timer* next;
};

/*定时器链表 升序双向链表 有头节点和尾节点*/
class sort_timer_lst{
public:
    sort_timer_lst() : head(nullptr), tail(nullptr) {}
    /*链表被销毁时 删除其中所有定时器*/
    ~sort_timer_lst()
    {
        util_timer* tmp = head;
        while(tmp)
        {
            head = tmp->next;
            delete tmp;
            tmp = head;
        }
    }
    /*将目标定时器timer添加到链表中*/
    void add_timer(util_timer* timer)
    {
        if(!timer)
        {
            return;
        }
        if(!head)
        {
            head = tail = timer;
            return;
        }
        if(timer->expire < head->expire)
        {
            timer->next = head;
            head->prev = timer;
            head = timer;
            return;
        }
        add_timer(timer, head);
    }
    /*某个定时任务发生变化时 调整对应定时器位置 仅考虑时间延长情况*/
    void adjust_timer(util_timer* timer)
    {
        if(!timer)
        {
            return;
        }
        util_timer* tmp = timer->next;
        if(!tmp || timer->expire < tmp->expire)
        {
            return;
        }
        /*将timer节点取出 并重新插入链表*/
        if(timer == head)
        {
            head = head->next;
            head->prev = nullptr;
            timer->next = nullptr;
            add_timer(timer, head);
        }
        else
        {
            timer->prev->next = timer->next;
            timer->next->prev = timer->prev;
            add_timer(timer, timer->next);
        }
    }
    
    void del_timer(util_timer* timer)
    {
        if(!timer)
        {
            return;
        }
        /*链表中只有一个定时器即timer*/
        if(timer == head && timer == tail)
        {
            delete timer;
            head = nullptr;
            tail = nullptr;
            return;
        }
        if(timer == head)
        {
            head = head->next;
            head->prev = nullptr;
            delete timer;
            return;
        }
        if(timer == tail)
        {
            tail = tail->prev;
            tail->next = nullptr;
            delete timer;
            return;
        }
        timer->prev->next = timer->next;
        timer->next->prev = timer->prev;
        delete timer;
    }
    /*SIGALRM信号每次被触发就执行一次tick函数 以处理链表上的到期任务*/
    void tick()
    {
        if(!head)
        {
            return;
        }
        printf("timer tick\n");
        time_t cur = time(NULL);    /*获得系统当前时间*/
        util_timer* tmp = head;
        while(tmp)
        {
            if(cur < tmp->expire)
            {
                break;
            }
            /*调用定时器的回调函数 执行定时任务*/
            tmp->cb_func(tmp->user_data);
            /*执行完定时器中的定时任务后 删除之 重置链表头结点*/
            head = tmp->next;
            if(head)
            {
                head->prev = nullptr;
            }
            delete tmp;
            tmp = head;
        }
    }

private:
    /*将timer添加到节点lst_head之后的部分链表中*/
    void add_timer(util_timer* timer, util_timer* lst_head)
    {
        util_timer* prev = lst_head;
        util_timer* tmp = prev->next;
        while(tmp)
        {
            if(tmp->expire > timer->expire)
            {
                prev->next = timer;
                timer->prev = prev;
                tmp->prev = timer;
                timer->next = tmp;
            }
            prev = tmp;
            tmp = prev->next;
        }
        if(!tmp)
        {
            prev->next = timer;
            timer->prev = prev;
            timer->next = nullptr;
            tail = timer;
        }
    }
private:
    util_timer* head;
    util_timer* tail;
};

#endif