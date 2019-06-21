#ifndef _CONNECTION_POOL_
#define _CONNECTION_POOL_

#include<stdio.h>
#include<list>
#include<mysql/mysql.h>
#include<error.h>
#include<string.h>
#include<iostream>
#include<string>

using namespace std;

class connection_pool
{
	public:
		//my_Pool(string IP,int Port,string User,string PassWord,string DataBaseName,unsigned int MaxConn);			//
		MYSQL * GetConnection();						//获取数据库连接
		bool ReleaseConnection(MYSQL* conn);			//释放连接
		void DestroyPool();			//销毁所有连接
		static connection_pool * GetInstance(string url,string User,string PassWord,string DataBaseName,int Port,unsigned int MaxConn);//单例模式获取一个连接
		int GetFreeConn();
	public:
		~connection_pool();

	private:
		unsigned int MaxConn;//最大连接数
		unsigned int CurConn;//当前已使用的连接数
		unsigned int FreeConn;//剩余未使用的连接数

	private:
		pthread_mutex_t lock;//互斥锁
		list<MYSQL *> connList;//连接池
		connection_pool * conn;//
		MYSQL *Con;//
		connection_pool(string url,string User,string PassWord,string DataBaseName,int Port,unsigned int MaxConn);//构造方法
		static connection_pool *connPool;//静态实例
	private:
		string url;//主机地址
		string User;//登陆数据库用户名
		string PassWord;//登陆数据库密码
		string DatabaseName;//使用数据库名
		string Port;//数据库端口号
};

#endif
