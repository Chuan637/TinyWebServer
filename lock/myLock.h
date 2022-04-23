#ifndef _MYLOCK_H_
#define _MYLOCK_H_

#include  <pthread.h>
#include <semaphore.h>

class myMutex {
private:
    pthread_mutex_t mutex;

public:
    myMutex()
    {
        pthread_mutex_init(&mutex, NULL);
    }

    ~myMutex()
    {
        pthread_mutex_destroy(&mutex);
    }

    pthread_mutex_t* get()
    {
        return &mutex;
    }

    bool lock()
    {
        return pthread_mutex_lock(&mutex) == 0;
    }

    bool unlock()
    {
        return pthread_mutex_unlock(&mutex);
    }
};

class mySem {
private:
    sem_t sem;

public:
    mySem()
    {
        //sem_init  进程内由多个线程共享的一个基于内存的信号量
        //sem_wait创建有名信号量，既可用于线程间又可用于进程间
        sem_init(&sem, 0, 0);
    }

    mySem(int num)
    {
        sem_init(&sem, 0, num);
    }

    ~mySem()
    {
        sem_destroy(&sem);
    }

    bool wait()
    {
        return sem_wait(&sem) == 0;
    }

    bool post()
    {
        return sem_post(&sem) == 0;
    }
};

class myCond {
private:
    pthread_cond_t cond;

public:
    myCond()
    {
        pthread_cond_init(&cond, NULL);
    }

    ~myCond()
    {
        pthread_cond_destroy(&cond);
    }

    bool wait(pthread_mutex_t* mutex)
    {
        int ret = 0;
        ret = pthread_cond_wait(&cond, mutex);
        return ret == 0;
    }

    bool timewait(pthread_mutex_t* mutex, struct timespec t)
    {
        int ret = 0;
        ret = pthread_cond_timedwait(&cond, mutex, &t);
        return ret == 0;
    }

    bool broadcast()
    {
        return pthread_cond_broadcast(&cond) == 0;
    }

    bool signal()
    {
        return pthread_cond_signal(&cond) == 0;
    }
};

#endif