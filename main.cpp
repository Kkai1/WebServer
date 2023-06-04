#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>
#include <sys/epoll.h>
#include "locker.h"
#include "threadpool.h"
#include <signal.h>
#include "http_conn.h"

#define MAX_FD 65535  //最大文件描述符个数
#define MAX_EVENT_NUMBER 10000 //监听的最大的事件数量

//添加信号捕捉 即捕捉到该信号做出什么处理  void(handler)(int)是函数指针 hander代表了调用函数
void addsig(int sig, void(handler)(int)){
    struct sigaction sa;
    memset(&sa,'\0',sizeof(sa));
    sa.sa_handler = handler;
    sigfillset(&sa.sa_mask);
    sigaction(sig,&sa,NULL);
}

//向epoll中添加文件描述符
extern void addfd(int epollfd,int fd,bool one_shot);
//从epoll中删除文件描述符
extern void removefd(int epollfd,int fd);
//修改文件描述符
extern void modfd(int epollfd,int fd,int ev);

int main(int argc,char* argv[]){
    //首先创建一个线程池，当有任务的时候，追加到队列当中，并且找一个合适的线程来处理任务
    if(argc <= 1){//argv数组没有传入端口号，默认长度为1 argc[0]是文件名
        printf("按照如下格式运行：%s port_number\n",basename(argv[0]));
        return 1;
    }

    //获取端口号
    int port = atoi(argv[1]);

    //对SIGPIPE信号进行处理 忽略
    addsig(SIGPIPE,SIG_IGN);

    //初始化线程池 初始化线程
    threadpool<http_conn> * pool = NULL;
    try
    {
        pool = new threadpool<http_conn>;
    }catch(...){
        exit(-1);
    }
    //创建一个数组用于保存所有的客户端信息
    http_conn * users = new http_conn[MAX_FD];

    //创建监听的套接字
    int listenfd = socket(PF_INET,SOCK_STREAM,0);

    //设置端口复用
    int reuse = 1;//1代表可以复用，0不能复用
    setsockopt(listenfd,SOL_SOCKET,SO_REUSEADDR,&reuse,sizeof(reuse));

    //绑定
    struct sockaddr_in address;
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY;
    address.sin_port = htons(port);
    bind(listenfd,(struct sockaddr*)&address,sizeof(address));
    
    //监听
    listen(listenfd,5);

    //创建epoll对象，事件的数组，添加
    epoll_event events[MAX_EVENT_NUMBER];
    int epofd = epoll_create(5);
    
    //监听的文件描述符添加到epoll中   第一个是监听文件描述符，后面进来的都是通信文件描述符
    addfd(epofd,listenfd,false);
    http_conn::m_epollfd = epofd;

    //循环检测事件的发生
    while(true){
        //epoll_wait() epofd文件描述符检测到的请求会存储到events数组中
        int num = epoll_wait(epofd,events,MAX_EVENT_NUMBER,-1);
        if((num < 0) && (errno != EINTR)){ //非人为中断 被信号中断后回来执行，wait函数不会阻塞
            printf("epoll failure\n");
            break;
        }
        //循环遍历 监听到num个请求
        for(int i = 0;i < num;i++){
            int sockfd = events[i].data.fd;
            //判断这个请求来自哪个套接字
            if(sockfd == listenfd){
                //有客户端要连接进来
                struct sockaddr_in client_address;
                //sockaddr_in和sockaddr的不同之处在于，
                //sockaddr_in结构中地址与端口号信息是分开的，便于人为修改，最终转换为sockaddr类型

                socklen_t client_addrelen = sizeof(client_address);
                int connfd = accept(listenfd,(struct sockaddr*)&client_address,&client_addrelen);
                if(http_conn::m_user_count >= MAX_FD) {
                    //目前连接数满了
                    //给客户端写一个信息，服务器内部正忙。
                    close(connfd);
                    continue;
                }
                //将新的客户的数据初始化，放到数组中
                users[connfd].init(connfd,client_address);
            }else if(events[i].events & (EPOLLHUP | EPOLLRDHUP | EPOLLERR)){
                //对方异常断开或者错误等事件
                users[sockfd].close_conn(); //关闭连接
            }else if(events[i].events & EPOLLIN){
                //读事件
                if(users[sockfd].read()){
                    //一次性把所有数据读完 将users[sockfd]事件交由线程池处理
                    pool->append(users + sockfd); //数组取地址有两种方式：a[5]  : 1. &a  2. &a + 1  就是取a数组第二个元素的地址
                }else{
                    users[sockfd].close_conn();
                }
            }else if(events[i].events & EPOLLOUT){
                if(!users[sockfd].write())  //一次性写完所有数据,写失败会返回-1，那么就进入if语句，断开连接
                {
                    users[sockfd].close_conn();
                }
            }
        }
    }

    close(epofd);
    close(listenfd);
    delete [] users;
    delete pool;

    return 0;
}