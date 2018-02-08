/* J. David's webserver
 * This is a simple webserver.
 * Created November 1999 by J. David Blackstone.
 * CSE 4344 (Network concepts), Prof. Zeigler
 * University of Texas at Arlington
 *
 * This program compiles for Sparc Solaris 2.6.
 * To compile for Linux:
 *  1) Comment out the #include <pthread.h> line.
 *  2) Comment out the line that defines the variable newthread.
 *  3) Comment out the two lines that run pthread_create().
 *  4) Uncomment the line that runs accept_request().
 *  5) Remove -lsocket from the Makefile.
 */
#include <stdio.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <ctype.h>
#include <strings.h>
#include <sys/stat.h>
//#include <pthread.h>
#include <sys/wait.h>
#include <stdlib.h>
#include "httpd.h"

/**********************************************************************
 * A request has caused a call to accept() on the server port to
 * return.  Process the request appropriately.
 * Parameters: the socket connected to the client
 **********************************************************************/
void accept_request(int client) {
    char buf[1024];
    int numchars;
    char method[255];
    char url[255];
    char path[512];
    size_t i; //临时指针
    size_t j; //BUF指针
    struct stat st;
    int cgi = 0; /* becomes true if server decides this is a CGI program */
    char *query_string = NULL;//URI字符串指针

    //读取请求报文中的第一行
    numchars = get_line(client, buf, sizeof(buf));

    i = 0;
    j = 0;

    //获取HTTP请求方法
    while (!ISspace(buf[j]) && (i < sizeof(method) - 1)) {
        method[i] = buf[j];
        i++;
        j++;
    }
    method[i] = '\0';

    //非GET和POST请求不与处理
    if (strcasecmp(method, "GET") && strcasecmp(method, "POST")) {
        unimplemented(client);
        return;
    }

    //POST请求交予CGI处理
    if (strcasecmp(method, "POST") == 0) {
        cgi = 1;
    }

    i = 0;
    //忽略空格
    while (ISspace(buf[j]) && (j < sizeof(buf))) {
        j++;
    }

    //获取请求的URI
    while (!ISspace(buf[j]) && (i < sizeof(url) - 1) && (j < sizeof(buf))) {
        url[i] = buf[j];
        i++;
        j++;
    }
    url[i] = '\0';

    //GET请求如果有参数, 请求路径与请求参数以'?'为界, query_string指向参数str
    if (strcasecmp(method, "GET") == 0) {
        query_string = url;
        while ((*query_string != '?') && (*query_string != '\0')) {
            query_string++;
        }

        if (*query_string == '?') {
            cgi = 1;
            *query_string = '\0';
            query_string++;
        }
    }

    sprintf(path, "htdocs%s", url);
    //根目录默认返回index.html
    if (path[strlen(path) - 1] == '/'){
        strcat(path, "index.html");
    }

    //请求资源不存在, 读取socket并抛弃内容, 返回404
    if (stat(path, &st) == -1) {
        while ((numchars > 0) && strcmp("\n", buf)) { /* read & discard headers */
            numchars = get_line(client, buf, sizeof(buf));
        }

        not_found(client);
    } else {
        //请求为目录, 则寻找目录下的index.html
        if ((st.st_mode & S_IFMT) == S_IFDIR) {
            strcat(path, "/index.html");
        }
        //请求文件当前用户可操作, 则执行CGI
        if ((st.st_mode & S_IXUSR) ||
            (st.st_mode & S_IXGRP) ||
            (st.st_mode & S_IXOTH)) {
            cgi = 1;
        }

        if (!cgi) {
            //非CGI处理直接返回请求的资源文件
            serve_file(client, path);
        }
        else {
            //执行CGI
            execute_cgi(client, path, method, query_string);
        }
    }

    close(client);
}

/**********************************************************************
 * Inform the client that a request it has made has a problem.
 * Parameters: client socket
**********************************************************************/

void bad_request(int client) {
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

/**********************************************************************
 * Put the entire contents of a file out on a socket.  This function
 * is named after the UNIX "cat" command, because it might have been
 * easier just to do something like pipe, fork, and exec("cat").
 * Parameters: the client socket descriptor
 *             FILE pointer for the file to cat
**********************************************************************/

void cat(int client, FILE *resource) {
    char buf[1024];

    fgets(buf, sizeof(buf), resource);
    while (!feof(resource)) {
        send(client, buf, strlen(buf), 0);
        fgets(buf, sizeof(buf), resource);
    }
}

/**********************************************************************
 * Inform the client that a CGI script could not be executed.
 * Parameter: the client socket descriptor.
**********************************************************************/

void cannot_execute(int client) {
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

/**********************************************************************
 * Print out an error message with perror() (for system errors; based
 * on value of errno, which indicates system call errors) and exit the
 * program indicating an error.
 **********************************************************************/
void error_die(const char *sc) {
    perror(sc);
    exit(1);
}

/**********************************************************************
 * Execute a CGI script.  Will need to set environment variables as
 * appropriate.
 * Parameters: client socket descriptor
 *             path to the CGI script
 **********************************************************************/
void execute_cgi(int client, const char *path,
                 const char *method, const char *query_string) {
    char buf[1024];
    int cgi_output[2];
    int cgi_input[2];
    pid_t pid;
    int status;
    int i;
    char c;
    int numchars = 1;
    int content_length = -1;

    buf[0] = 'A';
    buf[1] = '\0';
    //GET请求忽略请求头
    if (strcasecmp(method, "GET") == 0) {
        while ((numchars > 0) && strcmp("\n", buf)) { /* read & discard headers */
            numchars = get_line(client, buf, sizeof(buf));
        }
    }
    else /* POST */
    {
        //POST请求获取Content-Length
        numchars = get_line(client, buf, sizeof(buf));
        while ((numchars > 0) && strcmp("\n", buf)) {
            buf[15] = '\0';
            if (strcasecmp(buf, "Content-Length:") == 0) {
                content_length = atoi(&(buf[16]));
            }

            numchars = get_line(client, buf, sizeof(buf));
        }

        //Content-Length获取不到返回400
        if (content_length == -1) {
            bad_request(client);
            return;
        }
    }

    sprintf(buf, "HTTP/1.0 200 OK\r\n");
    send(client, buf, strlen(buf), 0);

    //创建CGI输出管道
    if (pipe(cgi_output) < 0) {
        cannot_execute(client);
        return;
    }
    //创建CGI输入管道
    if (pipe(cgi_input) < 0) {
        cannot_execute(client);
        return;
    }

    //fork()
    if ((pid = fork()) < 0) {
        cannot_execute(client);
        return;
    }

    //子进程执行CGI
    if (pid == 0) /* child: CGI script */
    {
        char meth_env[255];
        char query_env[255];
        char length_env[255];

        //重定向CGI输出管道写端为STDOUT
        dup2(cgi_output[1], 1);
        //重定向CGI输入管道读端为STDIN
        dup2(cgi_input[0], 0);
        //关闭CGI输出管道读端, 用于CGI程序输出
        close(cgi_output[0]);
        //关闭CGI输入管道写端, 用于CGI程序输入
        close(cgi_input[1]);
        /**管道建立的意义是让CGI程序通过标准输入输出实现前后台交互**/

        //请求方法放入环境变量REQUEST_METHOD中
        sprintf(meth_env, "REQUEST_METHOD=%s", method);
        putenv(meth_env);

        //GET请求将请求参数放入环境变量QUERY_STRING中
        if (strcasecmp(method, "GET") == 0) {
            sprintf(query_env, "QUERY_STRING=%s", query_string);
            putenv(query_env);
        } else { /* POST */
            //POST请求将请求体长度放入环境变量CONTENT_LENGTH中
            sprintf(length_env, "CONTENT_LENGTH=%d", content_length);
            putenv(length_env);
        }

        //调用execl执行cgi文件
        printf("excel [%s]\n", path);
        execl(path, path, NULL);

        exit(0);
    } else { /* parent */
        //关闭CGI输出管道写端, 用于接收CGI程序输出
        close(cgi_output[1]);
        //关闭CGI输入管道读端, 用于向CGI程序输入
        close(cgi_input[0]);

        //POST方法将请求体输入CGI程序
        if (strcasecmp(method, "POST") == 0) {
            for (i = 0; i < content_length; i++) {
                recv(client, &c, 1, 0);
                write(cgi_input[1], &c, 1);
            }
        }

        //将CGI程序输出写回客户端
        while (read(cgi_output[0], &c, 1) > 0) {
            send(client, &c, 1, 0);
        }

        //关闭CGI输出管道
        close(cgi_output[0]);
        //关闭CGI输入管道
        close(cgi_input[1]);

        //等待CGI执行子进程退出
        waitpid(pid, &status, 0);
    }
}

/**********************************************************************
 * Get a line from a socket, whether the line ends in a newline,
 * carriage return, or a CRLF combination.  Terminates the string read
 * with a null character.  If no newline indicator is found before the
 * end of the buffer, the string is terminated with a null.  If any of
 * the above three line terminators is read, the last character of the
 * string will be a linefeed and the string will be terminated with a
 * null character.
 * Parameters: the socket descriptor
 *             the buffer to save the data in
 *             the size of the buffer
 * Returns: the number of bytes stored (excluding null)
 **********************************************************************/
int get_line(int sock, char *buf, int size) {
    int i = 0;
    char c = '\0';
    ssize_t n;

    while ((i < size - 1) && (c != '\n')) {
        n = recv(sock, &c, 1, 0);
        /* DEBUG printf("%02X\n", c); */
        if (n > 0) {
            if (c == '\r') {
                n = recv(sock, &c, 1, MSG_PEEK); //查看但不读取下一个字符 即为PEEK
                /* DEBUG printf("%02X\n", c); */
                if ((n > 0) && (c == '\n')) { //'/r/n'为换行
                    recv(sock, &c, 1, 0);
                } else {
                    c = '\n';//'/r'亦视为换行
                }
            }
            buf[i] = c;
            i++;
        } else {
            c = '\n';
        }

    }
    buf[i] = '\0';//空buffer视为空串

    return (i);
}

/**********************************************************************
 * Return the informational HTTP headers about a file.
 * Parameters: the socket to print the headers on
 *             the name of the file
 **********************************************************************/
void headers(int client, const char *filename) {
    char buf[1024];
    (void) filename; /* could use filename to determine file type */

    strcpy(buf, "HTTP/1.0 200 OK\r\n");
    send(client, buf, strlen(buf), 0);
    strcpy(buf, SERVER_STRING);
    send(client, buf, strlen(buf), 0);
    sprintf(buf, "Content-Type: text/html\r\n");
    send(client, buf, strlen(buf), 0);
    strcpy(buf, "\r\n");
    send(client, buf, strlen(buf), 0);
}

/**********************************************************************
 * Give a client a 404 not found status message.
 **********************************************************************/
void not_found(int client) {
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

/**********************************************************************
 * Send a regular file to the client.  Use headers, and report
 * errors to client if they occur.
 * Parameters: a pointer to a file structure produced from the socket
 *              file descriptor
 *             the name of the file to serve
**********************************************************************/

void serve_file(int client, const char *filename) {
    FILE *resource = NULL;
    int numchars = 1;
    char buf[1024];

    buf[0] = 'A';
    buf[1] = '\0';
    while ((numchars > 0) && strcmp("\n", buf)) {/* read & discard headers */
        numchars = get_line(client, buf, sizeof(buf));
    }

    resource = fopen(filename, "r");
    //文件不存在返回404
    if (resource == NULL) {
        not_found(client);
    }
    else {
        //写response头
        headers(client, filename);
        //回写文件内容
        cat(client, resource);
    }
    fclose(resource);
}

/**********************************************************************
 * This function starts the process of listening for web connections
 * on a specified port.  If the port is 0, then dynamically allocate a
 * port and modify the original port variable to reflect the actual port.
 * Parameters: pointer to variable containing the port to connect on
 * Returns: the socket
 ***********************************************************************/
int startup(u_short *port) {
    int httpd = 0;
    struct sockaddr_in name;

    //创建套接字
    if ((httpd = socket(AF_INET, SOCK_STREAM, 0)) == -1)//现在为AF_INET, 实际上定义也是如此
    {
        error_die("socket");
    }

    //初始化服务端地址结构体
    memset(&name, 0, sizeof(name));
    name.sin_family = AF_INET;
    name.sin_port = htons(*port);
    name.sin_addr.s_addr = htonl(INADDR_ANY);

    //设置socket重用 -- add by zhangyu9
    int opt = 1;
    int len = sizeof(opt);
    setsockopt(httpd, SOL_SOCKET, SO_REUSEADDR, &opt, (socklen_t) len);

    //绑定
    if (bind(httpd, (struct sockaddr *) &name, sizeof(name)) < 0) {
        error_die("bind");
    }

    //不指定服务器端口的时候 获取系统分配的端口
    if (*port == 0) /* if dynamically allocating a port */
    {
        int namelen = sizeof(name);
        if (getsockname(httpd, (struct sockaddr *) &name, (socklen_t *) &namelen) == -1) {
            error_die("getsockname");
        }

        *port = ntohs(name.sin_port);
    }

    //监听
    if (listen(httpd, 5) < 0) {
        error_die("listen");
    }

    return (httpd);
}

/**********************************************************************
 * Inform the client that the requested web method has not been
 * implemented.
 * Parameter: the client socket
 **********************************************************************/
void unimplemented(int client) {
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

int main(void) {
    int server_sock;
    u_short port = 8111;
    int client_sock;
    struct sockaddr_in client_name;
    int client_name_len = sizeof(client_name);
//    pthread_t newthread;

    server_sock = startup(&port);
    printf("httpd running on port %d\n", port);

    while (1) {
        if ((client_sock = accept(server_sock,
                                  (struct sockaddr *) &client_name,
                                  (socklen_t *) &client_name_len)) == -1) {
            error_die("accept");
        }

        accept_request(client_sock);

//        if (pthread_create(&newthread, NULL,
//                           (void *(*)(void *)) accept_request,
//                           (void *) &client_sock) != 0) {
//            perror("pthread_create");
//        }
    }

//    close(server_sock);

//    return (0);
}
