#ifndef LOCKER_H
#define LOCKER_H

#include<exception>
#include<pthread.h>
#include<semaphore.h>
//POSIX信号量
class sem{
    public:
        sem()
        {
            if(sem_init(&m_sem,0,0)!=0){
                throw std::exception();
            }
        }
        ~sem()
        {
            sem_destroy(&m_sem);
        }
        bool wait()//从信号量的值减去一个“1”，相当于上锁
        {
            return sem_wait(&m_sem)==0;//sem_wait成功，返回0
        }
        bool post()//把指定的信号量 sem 的值加 1，唤醒正在等待该信号量的任意线程。相当于解锁
        {
            return sem_post(&m_sem)==0;
        }
    private:
        sem_t m_sem;//信号量
};
//互斥量
class locker
{
    public:
        locker()
        {
            if(pthread_mutex_init(&m_mutex,NULL)!=0)
            {
                throw std::exception();
            }
        }
        ~locker()
        {
            pthread_mutex_destroy(&m_mutex);
        }
        bool lock()
        {
            return pthread_mutex_lock(&m_mutex)==0;
        }
        bool unlock()
        {
            return pthread_mutex_unlock(&m_mutex)==0;
        }
    private:
        pthread_mutex_t m_mutex;
};
//条件变量
class cond
{
public:
    cond(){
        if(pthread_mutex_init(&m_mutex,NULL)!=0){
            throw std::exception();
        }
        if(pthread_cond_init(&m_cond,NULL)!=0){
            pthread_mutex_destroy(&m_mutex);
            throw std::exception();
        }
    }
    ~cond(){
        pthread_mutex_destroy(&m_mutex);
        pthread_cond_destroy(&m_cond);
    }
    bool wait()
    {
        int ret=0;
        pthread_mutex_lock(&m_mutex);
        ret=pthread_cond_wait(&m_cond,&m_mutex);
        pthread_mutex_unlock(&m_mutex);
        return ret==0;
    }
    bool signal()//唤醒至少一个阻塞在条件变量上的线程
    {
        return pthread_cond_signal(&m_cond)==0;
    }
private:
    pthread_mutex_t m_mutex;
    pthread_cond_t m_cond;
};
#endif
