#ifndef _THREADPOOL_H_
#define _THREADPOOL_H_

#include <iostream>
#include <list>
#include <exception>
#include <pthread.h>
#include "../lock/myLock.h"

/*线程池类 定义为模板为了方便复用 T是任务类*/
template<typename T>
class threadpool {
public:
    /* thread_number是线程池中线程的数量，max_requests是请求队列中最多允许的、等待处理的请求数量*/
    threadpool(int thread_number = 8, int max_requests = 10000);
    ~threadpool();
    //向请求队列中插入任务请求
    bool append(T* request);

private:
    /*工作线程运行的函数，它不断从工作队列中取出任务并执行*/
    static void* worker(void* arg);
    void run();

private:
    int m_thread_number;    /*线程池中线程数*/
    int m_max_requests;     /*请求队列中允许的最大请求数*/
    pthread_t *m_threads;   /*描述线程池的数组，其大小为m_thread_number*/
    std::list<T*>m_workqueue;       /*请求队列*/
    myMutex m_queuemutex;   /*保护请求队列的互斥锁*/
    mySem m_queuestat;      /*是否有任务需要处理*/
    bool m_stop;            /*是否结束线程*/
};

template<typename T>
threadpool<T>::threadpool(int thread_number, int max_requests) : m_thread_number(thread_number), m_max_requests(max_requests), m_threads(nullptr)
{
    if(thread_number <= 0 || max_requests <= 0)
    {
        throw std::exception();
    }
    m_threads = new pthread_t[m_thread_number];
    if(!m_threads)
    {
        throw std::exception();
    }
    /*创建thread_number个线程 并将它们都设置为脱离线程*/
    for(int i = 0; i < thread_number; ++i)
    {
        printf("create the %dth thread\n", i);
        if(pthread_create(m_threads + i, NULL, worker, this) != 0)
        {
            //新创建的线程从第三个参数的函数的地址开始运行  该函数要求为静态函数/静态成员函数
            delete[] m_threads;
            throw std::exception();
        }
        if(pthread_detach(m_threads[i]) != 0)
        {
            //可分离的线程 不能被其他线程回收或杀死，其内存空间在它终止时由系统自动释放 不用单独对工作线程进行回收
            delete[] m_threads;
            throw std::exception();
        }
    }
}

template<typename T>
threadpool<T>::~threadpool()
{
    delete[] m_threads;
    m_stop = true;
}

template<typename T>
bool threadpool<T>::append(T *request)
{
    m_queuemutex.lock();
    if(m_workqueue.size() >= m_max_requests)
    {
        m_queuemutex.unlock();
        return false;
    }
    m_workqueue.push_back(request);
    m_queuemutex.unlock();
    m_queuestat.post();     //m_queuestat信号量 是否有任务处理
    return true;
}

template<typename T>
void* threadpool<T>::worker(void *arg)
{
    /*将类对象作为参数传递给线程工作函数 将线程参数设置为this指针 worker函数中获取该指针*/
    /*将参数强制转换为线程池类，调用成员方法*/
    threadpool *pool = static_cast<threadpool*>(arg);
    pool->run();
    return pool;
}

template<typename T>
void threadpool<T>::run()
{
    while(!m_stop)
    {
        m_queuestat.wait();
        m_queuemutex.lock();
        if(m_workqueue.empty())
        {
            m_queuemutex.unlock();
            continue;
        }
        T *request = m_workqueue.front();
        m_workqueue.pop_front();
        m_queuemutex.unlock();
        if(!request)
        {
            continue;
        }
        request->process();
    }
}

#endif