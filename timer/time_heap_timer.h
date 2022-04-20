#include<iostream>
#include<netinet/in.h>
#include<time.h>
using std::exception;

#define BUFFER_SIZE 64

class heap_timer;

/*绑定socket和定时器*/
struct client_data{
    struct sockaddr_in address;
    int sockfd;
    char buf[BUFFER_SIZE];
    heap_timer* timer;
};

/*定时器类*/
class heap_timer{
public:
    heap_timer(int delay)
    {
        expire = time(NULL) + delay;
    }

public:
    time_t expire;  /*定时器生效的绝对时间*/
    void (*cb_func)(client_data*);  /*定时器的回调函数*/
    client_data* user_data; /*用户数据*/
};

/*时间堆类*/
class time_heap{
public:
    time_heap(int cap) throw(std::exception) : capacity(cap), cur_size(0)
    {
        array = new heap_timer*[capacity];
        if(!array)
        {
            throw std::exception;
        }
        for(int i = 0; i < capacity; ++i)
        {
            array[i] = nullptr;
        }
    }
    time_heap(heap_timer** init_array, int size, int cap) throw(std::exception)
    : cur_size(size), capacity(cap)
    {
        if(capacity < size)
        {
            throw std::exception;
        }
        array = new heap_timer*[capacity];
        if(!array)
        {
            throw std::exception;
        }
        for(int i = 0; i < capacity; ++i)
        {
            array[i] = nullptr;
        }
        if(size != 0)
        {
            /*初始化堆数组*/
            for(int i = 0; i < size; ++i)
            {
                array[i] = init_array[i];
            }
            for(int i = (cur_size - 1) / 2; i >= 0; --i)
            {
                /*对非叶子节点执行下滤操作*/
                percolate_down(i);
            }
        }
    }

    ~time_heap()
    {
        for(int i = 0; i < cur_size; ++i)
        {
            delete array[i];
        }
        delete [] array;
    }
public:
    /*添加目标定时器timer*/
    void add_timer(heap_timer* timer) throw(std::exception)
    {
        if(!timer)
        {
            return;
        }
        if(cur_size >= capacity)
        {
            resize();   /*扩容1倍*/
        }
        int hole = cur_size++;
        int parent = 0;
        for(; hole > 0; hole = parent)
        {
            parent = (hole - 1) / 2;
            if(array[parent]->expire <= timer->expire)
            {
                break;
            }
            array[hole] = array[parent];
        }
        array[hole] = timer;
    }
    /*删除目标定时器timer*/
    void del_timer(heap_timer* timer)
    {
        if(!timer)
        {
            return;
        }
        /*仅仅将目标定时器的回调函数设置为空 即所谓的延迟销毁*/
        /*节省真正删除该定时器造成的开销 但可能是堆数组膨胀*/
        timer->cb_func = nullptr;
    }
    /*获得堆顶的定时器*/
    heap_timer* top() const
    {
        if(empty())
        {
            return nullptr;
        }
        return array[0];
    }
    /*删除堆顶部定时器*/
    void pop_timer()
    {
        if(empty())
        {
            return;
        }
        if(array[0])
        {
            delete array[0];
            array[0] = array[--cur_size];
            percolate_down(0);
        }
    }
    /*心搏函数*/
    void tick()
    {
        heap_timer* tmp = array[0];
        time_t cur = time(NULL);
        /*循环处理堆中到期的定时器*/
        while(!empty())
        {
            if(!tmp)
            {
                break;
            }
            /*如果堆顶定时器没到期 则退出循环*/
            if(tmp->expire > cur)
            {
                break;
            }
            if(array[0]->cb_func)
            {
                array[0]->cb_func(array[0]->user_data);
            }
            pop_timer();
            tmp = array[0];
        }
    }
    bool empty() const {return cur_size == 0;}
private:
    /*最小堆的下滤操作*/
    void percolate_down(int hole)
    {
        heap_timer* tmp = array[hole];
        int child = 0;
        for(; ((hole * 2 + 1) <= (cur_size - 1)); hole = child)
        {
            child = hole * 2 + 1;
            if(child < cur_size - 1 && array[child + 1]->expire < array[child]->expire)
            {
                ++child;
            }
            if(array[child]->expire < tmp->expire)
            {
                array[hole] = array[child];
            }
            else
            {
                break;
            }
        }
        array[hole] = tmp;
    }
    /*堆数组容量扩大1倍*/
    void resize() throw(std::exception)
    {
        heap_timer** tmp = new heap_timer*[2 * capacity];
        for(int i = 0; i < 2 * capacity; ++i)
        {
            tmp[i] = nullptr;
        }
        if(!tmp)
        {
            throw std::exception;
        }
        capacity = 2 * capacity;
        for(int i = 0; i < cur_size; ++i)
        {
            tmp[i] = array[i];
        }
        delete [] array;
        array = tmp;
    }
private:
    heap_timer** array; /*堆数组*/
    int capacity;   /*堆数组容量*/
    int cur_size;   /*堆数组当前包含元素个数*/
};