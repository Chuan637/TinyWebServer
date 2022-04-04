#include <iostream>
#include <list>
#include <exception>
#include <pthread.h>
#include "../lock/myLock.h"

template<typename T>
class threadpool {
public:
    /* thread_number是线程池中线程的数量，max_requests是请求队列中最多允许的、等待处理的请求数量*/
    threadpool(int actor_model, int thread_number = 8, int max_requests = 10000);
    ~threadpool();
    //向请求队列中插入任务请求
    bool append(T* request, int state);
    bool append_p(T* request);

private:
    /*工作线程运行的函数，它不断从工作队列中取出任务并执行*/
    static void* worker(void* arg);
    void run();

private:
    int m_thread_number;    //线程池中线程数
    int m_max_requests;     //请求队列中允许的最大请求数
    pthread_t *m_threads;   //描述线程池的数组，其大小为m_thread_number
    std::list<T*>m_workqueue;       //请求队列
    myMutex m_queuemutex;   //保护请求队列的互斥锁
    mySem m_queuestat;      //是否有任务需要处理
    int m_actor_model;      //模式切换
 };
template<typename T>
threadpool<T>::threadpool(int actor_model, int thread_number, int max_requests) : m_actor_model(actor_model), m_thread_number(thread_number), m_max_requests(max_requests), m_threads(NULL)
{
    if(thread_number <= 0 || max_requests <= 0)
        throw std::exception();
    m_threads = new pthread_t[m_thread_number];
    if(!m_threads)
        throw std::exception();
    for(int i = 0; i < thread_number; ++i)
    {
        if(pthread_create(m_threads + i, NULL, worker, this) != 0){
            //新创建的线程从第三个参数的函数的地址开始运行  该函数要求为静态函数/静态成员函数
            delete[] m_threads;
            throw std::exception();
        }
        if(pthread_detach(m_threads[i]) != 0){
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
}
template<typename T>
bool threadpool<T>::append(T *request, int state)
{
    m_queuemutex.lock();
    if(m_workqueue.size() >= m_max_requests){
        m_queuemutex.unlock();
        return false;
    }
    request->m_state = state;
    m_workqueue.push_back(request);
    m_queuemutex.unlock();
    m_queuestat.post();     //m_queuestat信号量 是否有任务处理
    return true;
}
template<typename T>
bool threadpool<T>::append_p(T *requst)
{
    m_queuemutex.lock();
    if(m_workqueue.size() >= m_max_requests){
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
    将参数强制转换为线程池类，调用成员方法
    threadpool *pool = (threadpool*)arg;
    pool->run();
    return pool;
}
template<typename T>
void threadpool<T>::run()
{
    while(true)
    {
        m_queuestat.wait();
        m_queuemutex.lock();
        if(m_workqueue.empty()){
            m_queuemutex.unlock();
            continue;
        }
        T *request = m_workqueue.front();
        m_workqueue.pop_front();
        m_queuemutex.unlock();
        if(!request)
            continue;
        if(m_actor_model == 1)
        {
            if(request->m_state == 0)
            {
                if(request->read_once())
                {
                    request->improv = 1;
                    //process处理  模板类中的方法 这里是http类
                    request->process();
                }
                else
                {
                    request.improv = 1;
                    request->timer_flag = 1;
                }
            }
            else
            {
                if(request->write())
                {
                    request.improv = 1;
                }
                else
                {
                    request.improv = 1;
                    request->timer_flag = 1;
                }
            }
        }
        else
        {
            request->process();
        }
    }
}