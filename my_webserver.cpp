#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <stdio.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <fcntl.h>
#include <stdlib.h>
#include <sys/epoll.h>
#include "locker.h"
#include "threadpool.h"
#include "http_conn.h"

#define MAX_FD 65536   // 最大的文件描述符个数
#define MAX_EVENT_NUMBER 10000  // 监听的最大的事件数量

// 添加文件描述符
extern void addfd( int epollfd, int fd, bool one_shot);
extern void removefd( int epollfd, int fd );

/*如果只写handler，那么编译器会认为它是一个普通的变量，而不是一个函数指针。
你必须指明它是一个指向函数的指针，以及这个函数的返回类型和参数类型。
这样才能让编译器正确地识别和调用这个函数指针。*/

// 添加信号捕捉
void addsig(int sig, void(*handler)(int)){
    assert(handler!=nullptr);
    struct sigaction sa;
    memset( &sa, '\0', sizeof( sa ) );
    sa.sa_handler = handler;
    sigfillset( &sa.sa_mask );
    assert( sigaction( sig, &sa, NULL ) != -1 );
}

int main( int argc, char* argv[] ) {
    
    if( argc <= 1 ) {
        printf( "usage: %s port_number\n", basename(argv[0]));
        return 1;
    }

    int port = atoi( argv[1] );
    addsig( SIGPIPE, SIG_IGN );// 假如客户端突然断开连接，忽略这时候的错误

    threadpool< http_conn >* pool = NULL;
    try {
        pool = new threadpool<http_conn>; // 线程池，处理对象为 http_conn
    } catch( ... ) {
        return 1;
    }

    http_conn* users = new http_conn[ MAX_FD ]; 

    int listenfd = socket( PF_INET, SOCK_STREAM, 0 );

    int ret = 0;
    struct sockaddr_in address;
    address.sin_addr.s_addr = INADDR_ANY;
    address.sin_family = AF_INET;
    address.sin_port = htons( port );

    // 端口复用
    int reuse = 1;
    setsockopt( listenfd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof( reuse ) );
    ret = bind( listenfd, ( struct sockaddr* )&address, sizeof( address ) );
    ret = listen( listenfd, 5 );

    // 创建epoll对象，和事件数组，添加
    epoll_event events[ MAX_EVENT_NUMBER ];
    int epollfd = epoll_create( 5 );
    // 添加到epoll对象中
    addfd( epollfd, listenfd, false);
    http_conn::m_epollfd = epollfd;

    while(true) {
        
        int number = epoll_wait( epollfd, events, MAX_EVENT_NUMBER, -1 );
        
        if ( ( number < 0 ) && ( errno != EINTR ) ) {//软中断返回后 函数不会阻塞，错误号为EINTR
            printf( "epoll failure\n" );
            break;
        }

        for ( int i = 0; i < number; i++ ) {
            
            int sockfd = events[i].data.fd;
            
            if( sockfd == listenfd ) {
                // 有新的链接的时候
                struct sockaddr_in client_address;
                socklen_t client_addrlength = sizeof( client_address );
                int connfd = accept( listenfd, ( struct sockaddr* )&client_address, &client_addrlength );
                
                if ( connfd < 0 ) {
                    printf( "errno is: %d\n", errno );
                    continue;
                } 

                if( http_conn::m_user_count >= MAX_FD ) {
                    close(connfd);
                    printf("too much user\n");
                    continue;
                }
                // 用对话的fd当作索引
                users[connfd].init( connfd, client_address);

            } else if( events[i].events & ( EPOLLRDHUP | EPOLLHUP | EPOLLERR ) ) {
                // 异常事件,对方断开或者其他异常
                users[sockfd].close_conn();

            } else if(events[i].events & EPOLLIN) { // 有数据到达

                if(users[sockfd].read()) { // 一次性读所有数据 ET模式，所以要非阻塞
                    pool->append(users + sockfd);// 添加需要处理的任务到线程池的请求队列之中
                } else {
                    users[sockfd].close_conn(); // 读到0个数据 那么就说明断开连接
                }

            }  else if( events[i].events & EPOLLOUT ) {
                
                printf("bytes_to_send \n");
                if( !users[sockfd].write() ) {
                    users[sockfd].close_conn();
                }
            }
        }
    }
    
    close( epollfd );
    close( listenfd );
    delete [] users;
    delete pool;
    return 0;
}