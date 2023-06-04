#ifndef THREADPOLL_H
#define THREADPOLL_H

#include <pthread.h>
#include <list>
#include "locker.h"
#include <exception>
#include <cstdio>

//从线程池中找一个线程去处理任务
//线程池类，定义成模板类为了代码复用，模板参数T是任务类
template<typename T>
class threadpool
{
public:
    threadpool(int thread_number = 8,int max_requests = 10000);
    ~threadpool();
    bool append(T* request);

private:
    static void* worker(void* arg);
    void run();

private:
    //线程的数量
    int m_thread_number;

    //线程池数组，大小为 m_thread_number
    pthread_t * m_threads;

    //请求队列中最多允许的，等待处理的请求数量
    int m_max_requests;

    //请求队列
    std::list<T*> m_workqueue;

    //互斥锁
    locker m_queuelocker;

    //信号量用来判断是否有任务需要处理
    sem m_queuestat;

    //是否结束线程
    bool m_stop;
};

//构造函数，初始化数据
template <typename T>
threadpool<T>::threadpool(int thread_number,int max_requests)://初始化
    m_thread_number(thread_number),m_max_requests(max_requests),
    m_stop(false),m_threads(nullptr){
    if((thread_number <= 0) || (max_requests <= 0)){
        throw std::exception();
    }

    m_threads = new pthread_t[m_thread_number];
    if(!m_threads){
        throw std::exception();
    }

    //创建thread_number个线程，并将他们设置为线程脱离
    for(int i = 0;i < thread_number;++i){
        printf("creat the %dth thread\n",i);

        //为线程池创建了thread_number个线程，回调函数是worker();
        if(pthread_create(m_threads + i,NULL,worker,this) != 0){
            delete [] m_threads;//释放数组
            throw std::exception();
        }

        //设置线程脱离
        if(pthread_detach(m_threads[i])){
            delete [] m_threads;//释放数组
            throw std::exception();
        }

    }

}

//析构
template <typename T>
threadpool<T>::~threadpool(){
    delete [] m_threads;
    m_stop = true;
}

//增加线程池
template <typename T>
bool threadpool<T>::append(T * request){
    m_queuelocker.lock();
    if(m_workqueue.size() > m_max_requests){
        m_queuelocker.unlock();
        return false;
    }

    m_workqueue.push_back(request);
    m_queuelocker.unlock();
    m_queuestat.post(); //信号量增加
    return true;
}

template <typename T>
void* threadpool<T>::worker(void* arg){
    threadpool * pool = (threadpool *) arg;
    pool->run();
    return pool;
}

template <typename T>
void threadpool<T>::run(){
    while (!m_stop)
    {   
        //创建线程池之后在此等待工作指令
        printf("waiting for mission\n");
        m_queuestat.wait();//阻塞等待
        m_queuelocker.lock();
        if(m_workqueue.empty()){
            m_queuelocker.unlock();
            continue;
        }
        T* request = m_workqueue.front();//获取第一个任务
        m_workqueue.pop_front();
        m_queuelocker.unlock();

        if(!request){
            continue;
        }
        request->process();//线程的任务
    }
    
}

#endif

