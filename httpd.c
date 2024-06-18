/* J. David's webserver */
/* This is a simple webserver.
 * Created November 1999 by J. David Blackstone.
 * CSE 4344 (Network concepts), Prof. Zeigler
 * University of Texas at Arlington
 */
/* This program compiles for Sparc Solaris 2.6.
 * To compile for Linux:
 *  1) Comment out the #include <pthread.h> line.
 *  2) Comment out the line that defines the variable newthread.
 *  3) Comment out the two lines that run pthread_create().
 *  4) Uncomment the line that runs accept_request().
 *  5) Remove -lsocket from the Makefile.
 */
#include <stdio.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <ctype.h>
#include <strings.h>
#include <string.h>
#include <sys/stat.h>
#include <pthread.h>
#include <sys/wait.h>
#include <stdlib.h>

// 宏定义，是否是空格
#define ISspace(x) isspace((int)(x))

#define SERVER_STRING "Server: jdbhttpd/0.1.0\r\n"

// 每次收到请求，创建一个线程来处理接受到的请求
// 把client_sock转成地址作为参数传入pthread_create
void accept_request(void *arg);

// 错误请求
void bad_request(int);

// 读取文件
void cat(int, FILE *);

// 无法执行
void cannot_execute(int);

// 错误输出
void error_die(const char *);

// 执行cig脚本
void execute_cgi(int, const char *, const char *, const char *);

// 得到一行数据,只要发现c为\n,就认为是一行结束，如果读到\r,再用MSG_PEEK的方式读入一个字符，如果是\n，从socket用读出
// 如果是下个字符则不处理，将c置为\n，结束。如果读到的数据为0中断，或者小于0，也视为结束，c置为\n
int get_line(int, char *, int);

// 返回http头
void headers(int, const char *);

// 没有发现文件
void not_found(int);

// 如果不是CGI文件，直接读取文件返回给请求的http客户端
void serve_file(int, const char *);

// 开启tcp连接，绑定端口等操作
int startup(u_short *);

// 如果不是Get或者Post，就报方法没有实现
void unimplemented(int);

// Http请求，后续主要是处理这个头
//
// GET / HTTP/1.1
// Host: 192.168.0.23:47310
// Connection: keep-alive
// Upgrade-Insecure-Requests: 1
// User-Agent: Mozilla/5.0 (Windows NT 6.1; WOW64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/55.0.2883.87 Safari/537.36
// Accept: text/html,application/xhtml+xml,application/xml;q=0.9,image/webp,*/*; q = 0.8
// Accept - Encoding: gzip, deflate, sdch
// Accept - Language : zh - CN, zh; q = 0.8
// Cookie: __guid = 179317988.1576506943281708800.1510107225903.8862; monitor_count = 5
//

// POST / color1.cgi HTTP / 1.1
// Host: 192.168.0.23 : 47310
// Connection : keep - alive
// Content - Length : 10
// Cache - Control : max - age = 0
// Origin : http ://192.168.0.23:40786
// Upgrade - Insecure - Requests : 1
// User - Agent : Mozilla / 5.0 (Windows NT 6.1; WOW64) AppleWebKit / 537.36 (KHTML, like Gecko) Chrome / 55.0.2883.87 Safari / 537.36
// Content - Type : application / x - www - form - urlencoded
// Accept : text / html, application / xhtml + xml, application / xml; q = 0.9, image / webp, */*;q=0.8
// Referer: http://192.168.0.23:47310/
// Accept-Encoding: gzip, deflate
// Accept-Language: zh-CN,zh;q=0.8
// Cookie: __guid=179317988.1576506943281708800.1510107225903.8862; monitor_count=281
// Form Data
// color=gray

/**********************************************************************/
/* 请求已导致对服务器端口上的 accept() 的调用
* 返回。适当地处理请求。
* 参数：连接到客户端的套接字 */
/**********************************************************************/
void accept_request(void *arg)
{
    // socket
    int client = (intptr_t)arg; //client套接字
    char buf[1024]; //存储从套接字中读取到的内容
    int numchars; //表示读取到的字节数
    char method[255]; //存储请求方法
    char url[255]; //存储url
    char path[512];//存储路径
    size_t i, j; //两个索引指针
    struct stat st;//存储网页文件的文件信息
    int cgi = 0; //cgi是否启用，如果设置为1，默认启用
    char *query_string = NULL; //请求参数
    // 根据上面的Get请求，可以看到这边就是取第一行
    // 这边都是在处理第一条http信息

    numchars = get_line(client, buf, sizeof(buf));//先从套接字读取一行数据到buf中，然后返回读取到的字节数
    i = 0;
    j = 0;

    // 第一行字符串提取Get
        //"GET / HTTP/1.1\n"
    while (!ISspace(buf[j]) && (i < sizeof(method) - 1))
    {
        //读到GET后的space后，循环就终止了，正好把GET读取到method数组中了
        method[i] = buf[j];
        i++;
        j++;
    }
    // 结束
    method[i] = '\0';

    // 如果两个都不是，直接返回是不支持的请求类型
    //strcasecmd 和 strcmp一样，相等返回0
    if (strcasecmp(method, "GET") && strcasecmp(method, "POST"))
    {
        unimplemented(client);
        return;
    }

    // 如果是POST，cgi置为1
    if (strcasecmp(method, "POST") == 0)
        cgi = 1;

    i = 0; // i指针归零
    // 跳过空格
    while (ISspace(buf[j]) && (j < sizeof(buf)))
        j++;

    // 得到 "/"   注意：如果你的http的网址为http://192.168.0.23:47310/index.html
    //                那么你得到的第一条http信息为GET /index.html HTTP/1.1，那么
    //                解析得到的就是/index.html
    while (!ISspace(buf[j]) && (i < sizeof(url) - 1) && (j < sizeof(buf)))
    {
        url[i] = buf[j];
        i++;
        j++;
    }
    url[i] = '\0';

    // 判断Get请求
    if (strcasecmp(method, "GET") == 0)
    {
        query_string = url;
        while ((*query_string != '?') && (*query_string != '\0'))
            query_string++;
        if (*query_string == '?')
        {
            cgi = 1;
            *query_string = '\0';
            query_string++;
        }
    }

    // 路径
    sprintf(path, "htdocs%s", url);

    // 默认地址，解析到的路径如果为/，则自动加上index.html
    if (path[strlen(path) - 1] == '/')
        strcat(path, "index.html");

    // 获得文件信息
    if (stat(path, &st) == -1)
    {
        // 把所有http信息读出然后丢弃
        while ((numchars > 0) && strcmp("\n", buf)) /* read & discard headers */
            numchars = get_line(client, buf, sizeof(buf));

        // 没有找到
        not_found(client);
    }
    else
    {
        mode_t file_type = st.st_mode & S_IFMT;
        if (file_type == S_IFDIR)
            strcat(path, "/index.html");
        // 如果你的文件默认是有执行权限的，自动解析成cgi程序，如果有执行权限但是不能执行，会接受到报错信号
        if ((st.st_mode & S_IXUSR) ||
            (st.st_mode & S_IXGRP) ||
            (st.st_mode & S_IXOTH))
            cgi = 1;
        if (!cgi)
            // 接读取文件返回给请求的http客户端
            serve_file(client, path);
        else
            // 执行cgi文件
            execute_cgi(client, path, method, query_string);
    }
    // 执行完毕关闭socket
    close(client);
}

/**********************************************************************/
/* Inform the client that a request it has made has a problem.
 * Parameters: client socket */
/**********************************************************************/
void bad_request(int client)
{
    char buf[1024];

    sprintf(buf, "HTTP/1.0 400 BAD REQUEST\r\n");
    send(client, buf, sizeof(buf), 0);
    sprintf(buf, "Content-type: text/html\r\n");
    send(client, buf, sizeof(buf), 0);
    sprintf(buf, "\r\n");
    send(client, buf, sizeof(buf), 0);
    sprintf(buf, "<P>Your browser sent a bad request, ");
    send(client, buf, sizeof(buf), 0);
    sprintf(buf, "such as a POST without a Content-Length.\r\n");
    send(client, buf, sizeof(buf), 0);
}

/**********************************************************************/
/* Put the entire contents of a file out on a socket.  This function
 * is named after the UNIX "cat" command, because it might have been
 * easier just to do something like pipe, fork, and exec("cat").
 * Parameters: the client socket descriptor
 *             FILE pointer for the file to cat */
/**********************************************************************/

// 得到文件内容，发送
void cat(int client, FILE *resource)
{
    char buf[1024];

    fgets(buf, sizeof(buf), resource);
    // 循环读
    while (!feof(resource))
    {
        send(client, buf, strlen(buf), 0);
        fgets(buf, sizeof(buf), resource);
    }
}

/**********************************************************************/
/* Inform the client that a CGI script could not be executed.
 * Parameter: the client socket descriptor. */
/**********************************************************************/
void cannot_execute(int client)
{
    char buf[1024];

    sprintf(buf, "HTTP/1.0 500 Internal Server Error\r\n");
    send(client, buf, strlen(buf), 0);
    sprintf(buf, "Content-type: text/html\r\n");
    send(client, buf, strlen(buf), 0);
    sprintf(buf, "\r\n");
    send(client, buf, strlen(buf), 0);
    sprintf(buf, "<P>Error prohibited CGI execution.\r\n");
    send(client, buf, strlen(buf), 0);
}

/**********************************************************************/
/* 使用 perror() 打印出错误消息（针对系统错误；基于 errno 的值，表示系统调用错误）并退出程序并指示错误。*/
/**********************************************************************/
void error_die(const char *sc)
{
    perror(sc);
    exit(1);
}

/**********************************************************************/
/* 执行 CGI 脚本。需要根据需要设置环境变量。
* 参数：客户端套接字描述符
* CGI 脚本的路径 */
/**********************************************************************/
void execute_cgi(int client, const char *path,
                 const char *method, const char *query_string)
{
    // 缓冲区
    char buf[1024];

    // 2根管道
    int cgi_output[2];
    int cgi_input[2];

    // 进程pid和状态
    pid_t pid;
    int status;

    int i;
    char c;

    // 读取的字符数
    int numchars = 1;

    // http的content_length
    int content_length = -1;

    // 默认字符
    buf[0] = 'A';
    buf[1] = '\0';

    // 忽略大小写比较字符串
    if (strcasecmp(method, "GET") == 0)
        // 读取数据，把整个header都读掉，以为Get写死了直接读取index.html，没有必要分析余下的http信息了
        while ((numchars > 0) && strcmp("\n", buf)) /* read & discard headers */
            numchars = get_line(client, buf, sizeof(buf));
    else /* POST */
    {
        numchars = get_line(client, buf, sizeof(buf));
        while ((numchars > 0) && strcmp("\n", buf))
        {
            // 如果是POST请求，就需要得到Content-Length，Content-Length：这个字符串一共长为15位，所以
            // 取出头部一句后，将第16位设置结束符，进行比较
            // 第16位置为结束
            buf[15] = '\0';
            if (strcasecmp(buf, "Content-Length:") == 0)
                // 内存从第17位开始就是长度，将17位开始的所有字符串转成整数就是content_length
                content_length = atoi(&(buf[16]));
            numchars = get_line(client, buf, sizeof(buf));
        }
        if (content_length == -1)
        {
            bad_request(client);
            return;
        }
    }

    sprintf(buf, "HTTP/1.0 200 OK\r\n");
    send(client, buf, strlen(buf), 0);
    // 建立output管道
    if (pipe(cgi_output) < 0)
    {
        cannot_execute(client);
        return;
    }

    // 建立input管道
    if (pipe(cgi_input) < 0)
    {
        cannot_execute(client);
        return;
    }
    //       fork后管道都复制了一份，都是一样的
    //       子进程关闭2个无用的端口，避免浪费
    //       ×<------------------------->1    output
    //       0<-------------------------->×   input

    //       父进程关闭2个无用的端口，避免浪费
    //       0<-------------------------->×   output
    //       ×<------------------------->1    input
    //       此时父子进程已经可以通信

    // fork进程，子进程用于执行CGI
    // 父进程用于收数据以及发送子进程处理的回复数据
    if ((pid = fork()) < 0)
    {
        cannot_execute(client);
        return;
    }
    if (pid == 0) /* child: CGI script */
    {
        char meth_env[255];
        char query_env[255];
        char length_env[255];

        // 子进程输出重定向到output管道的1端
        dup2(cgi_output[1], 1);
        // 子进程输入重定向到input管道的0端
        dup2(cgi_input[0], 0);

        // 关闭无用管道口
        close(cgi_output[0]);
        close(cgi_input[1]);

        // CGI环境变量
        sprintf(meth_env, "REQUEST_METHOD=%s", method);
        putenv(meth_env);
        if (strcasecmp(method, "GET") == 0)
        {
            sprintf(query_env, "QUERY_STRING=%s", query_string);
            putenv(query_env);
        }
        else
        { /* POST */
            sprintf(length_env, "CONTENT_LENGTH=%d", content_length);
            putenv(length_env);
        }
        // 替换执行path
        execl(path, path, NULL);
        // int m = execl(path, path, NULL);
        // 如果path有问题，例如将html网页改成可执行的，但是执行后m为-1
        // 退出子进程，管道被破坏，但是父进程还在往里面写东西，触发Program received signal SIGPIPE, Broken pipe.
        exit(0);
    }
    else
    { /* parent */

        // 关闭无用管道口
        close(cgi_output[1]);
        close(cgi_input[0]);
        if (strcasecmp(method, "POST") == 0)
            for (i = 0; i < content_length; i++)
            {
                // 得到post请求数据，写到input管道中，供子进程使用
                recv(client, &c, 1, 0);
                write(cgi_input[1], &c, 1);
            }
        // 从output管道读到子进程处理后的信息，然后send出去
        while (read(cgi_output[0], &c, 1) > 0)
            send(client, &c, 1, 0);

        // 完成操作后关闭管道
        close(cgi_output[0]);
        close(cgi_input[1]);

        // 等待子进程返回
        waitpid(pid, &status, 0);
    }
}

/********************************************/
/*
 * GET / HTTP/1.1
 * POST / color1.cgi HTTP / 1.1
 * 从套接字获取一行，不论该行以换行符、
 * 回车符或CRLF组合结尾。读取的字符串以
 * 空字符结尾。如果在缓冲区末尾之前未找到
 * 换行符指示符，则字符串将以空字符结尾。
 * 如果读到以上三种换行终止符之一，字符串的
 * 最后一个字符将是换行符，并且字符串将以
 * 空字符结尾。
 * 参数：套接字描述符
 *        保存数据的缓冲区
 *        缓冲区的大小
 * 返回：存储的字节数（不包括空字符）
 */
/********************************************/
int get_line(int sock, char *buf, int size)
{
    int i = 0;     // 缓冲区的索引
    char c = '\0'; // 从套接字读取的字符
    int n;         // recv() 函数读取的字符数

    // 循环读取字符，直到缓冲区满或遇到换行符
    while ((i < size - 1) && (c != '\n'))
    {
        // 从套接字读取一个字符
        n = recv(sock, &c, 1, 0);

        if (n > 0)
        {
            // 如果字符是回车符（'\r'），需要检查下一个字符
            if (c == '\r')
            {
                // 偷窥下一个字符，但不从输入队列中移除它
                n = recv(sock, &c, 1, MSG_PEEK);

                // 如果下一个字符是换行符（'\n'），从套接字读取它
                if ((n > 0) && (c == '\n'))
                    recv(sock, &c, 1, 0);
                else
                    // 如果下一个字符不是换行符，将回车符视为换行符
                    c = '\n';
            }

            // 将字符存储在缓冲区中
            buf[i] = c;
            i++;
        }
        else
            // 如果没有读取到字符，视为换行符以结束行
            c = '\n';
    }

    // 空字符终止缓冲区，使其成为有效的 C 字符串
    buf[i] = '\0';

    // 返回存储在缓冲区中的字符数，不包括空字符
    return (i);
}

/**********************************************************************/
/* 返回有关文件的信息性 HTTP 标头。 */
/* 参数：打印标头的套接字
* 文件的名称 */
/**********************************************************************/
// 加入http的headers
void headers(int client, const char *filename)
{
    char buf[1024];
    (void)filename; /* 可以使用文件名来确定文件类型 */

    strcpy(buf, "HTTP/1.0 200 OK\r\n");
    send(client, buf, strlen(buf), 0);
    strcpy(buf, SERVER_STRING);
    send(client, buf, strlen(buf), 0);
    sprintf(buf, "Content-Type: text/html\r\n");
    send(client, buf, strlen(buf), 0);
    strcpy(buf, "\r\n");
    send(client, buf, strlen(buf), 0);
}

/**********************************************************************/
/* 向客户端发出 404 未找到状态消息。 */
/**********************************************************************/

// 如果资源没有找到得返回给客户端下面的信息
void not_found(int client)
{
    char buf[1024];

    sprintf(buf, "HTTP/1.0 404 NOT FOUND\r\n");
    send(client, buf, strlen(buf), 0);
    sprintf(buf, SERVER_STRING);
    send(client, buf, strlen(buf), 0);
    sprintf(buf, "Content-Type: text/html\r\n");
    send(client, buf, strlen(buf), 0);
    sprintf(buf, "\r\n");
    send(client, buf, strlen(buf), 0);
    sprintf(buf, "<HTML><TITLE>Not Found</TITLE>\r\n");
    send(client, buf, strlen(buf), 0);
    sprintf(buf, "<BODY><P>The server could not fulfill\r\n");
    send(client, buf, strlen(buf), 0);
    sprintf(buf, "your request because the resource specified\r\n");
    send(client, buf, strlen(buf), 0);
    sprintf(buf, "is unavailable or nonexistent.\r\n");
    send(client, buf, strlen(buf), 0);
    sprintf(buf, "</BODY></HTML>\r\n");
    send(client, buf, strlen(buf), 0);
}

/**********************************************************************/
/* 向客户端发送常规文件。使用标头，并向客户端报告
* 错误（如果发生）。
* 参数：指向套接字生成的文件结构的指针
* 文件描述符
* 要提供的文件的名称 */
/**********************************************************************/

// 如果不是CGI文件，直接读取文件返回给请求的http客户端
void serve_file(int client, const char *filename)
{
    FILE *resource = NULL;
    int numchars = 1;
    char buf[1024];

    // 默认字符
    buf[0] = 'A';
    buf[1] = '\0';
    while ((numchars > 0) && strcmp("\n", buf)) /* read & discard headers */
        numchars = get_line(client, buf, sizeof(buf));

    resource = fopen(filename, "r");
    if (resource == NULL)
        not_found(client);
    else
    {
        headers(client, filename);
        cat(client, resource);
    }
    fclose(resource);
}

/**********************************************************************/
/* 此函数启动在指定端口上监听 Web 连接的过程。如果端口为 0，
则动态分配一个端口并修改原始端口变量以反映实际端口。
参数：指向包含要连接的端口的变量的指针返回套接字 */
/**********************************************************************/
int startup(u_short *port)
{
    int httpd = 0;
    struct sockaddr_in name;

    httpd = socket(PF_INET, SOCK_STREAM, 0);
    if (httpd == -1)
        error_die("socket");
    memset(&name, 0, sizeof(name));
    name.sin_family = AF_INET;
    name.sin_port = htons(*port);
    name.sin_addr.s_addr = htonl(INADDR_ANY);
    // 绑定socket
    if (bind(httpd, (struct sockaddr *)&name, sizeof(name)) < 0)
        error_die("bind");
    // 如果端口没有设置，提供个随机端口
    if (*port == 0) /* if dynamically allocating a port */
    {
        socklen_t namelen = sizeof(name);
        if (getsockname(httpd, (struct sockaddr *)&name, &namelen) == -1)
            error_die("getsockname");
        *port = ntohs(name.sin_port);
    }
    // 监听
    if (listen(httpd, 5) < 0)
        error_die("listen");
    return (httpd);
}

/**********************************************************************/
/* 通知客户端请求的 Web 方法尚未实现。
* 参数：客户端套接字 */
/**********************************************************************/

// 如果方法没有实现，就返回此信息
void unimplemented(int client)
{
    char buf[1024];

    sprintf(buf, "HTTP/1.0 501 Method Not Implemented\r\n");
    send(client, buf, strlen(buf), 0);
    sprintf(buf, SERVER_STRING);
    send(client, buf, strlen(buf), 0);
    sprintf(buf, "Content-Type: text/html\r\n");
    send(client, buf, strlen(buf), 0);
    sprintf(buf, "\r\n");
    send(client, buf, strlen(buf), 0);
    sprintf(buf, "<HTML><HEAD><TITLE>Method Not Implemented\r\n");
    send(client, buf, strlen(buf), 0);
    sprintf(buf, "</TITLE></HEAD>\r\n");
    send(client, buf, strlen(buf), 0);
    sprintf(buf, "<BODY><P>HTTP request method not supported.\r\n");
    send(client, buf, strlen(buf), 0);
    sprintf(buf, "</BODY></HTML>\r\n");
    send(client, buf, strlen(buf), 0);
}

/**********************************************************************/

int main(void)
{
    int server_sock = -1;
    u_short port = 0;
    int client_sock = -1;
    struct sockaddr_in client_name;

    // 这边要为socklen_t类型
    socklen_t client_name_len = sizeof(client_name);
    pthread_t newthread;

    server_sock = startup(&port);
    printf("httpd running on port %d\n", port);

    while (1)
    {
        client_sock = accept(server_sock,
                             (struct sockaddr *)&client_name,
                             &client_name_len);
        if (client_sock == -1)
            error_die("accept");
        // 每次收到请求，创建一个线程来处理接受到的请求
        // 把client_sock转成地址作为参数传入pthread_create
        if (pthread_create(&newthread, NULL, (void *)accept_request, (void *)(intptr_t)client_sock) != 0)
            perror("pthread_create");
    }

    close(server_sock);

    return (0);
}