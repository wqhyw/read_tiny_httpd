//
// Created by leo on 2018/2/6.
//

#ifndef TINY_HTTPD_HTTPD_H
#define TINY_HTTPD_HTTPD_H

#define ISspace(x) isspace((int)(x))

#define SERVER_STRING "Server: jdbhttpd/0.1.0\r\n"

void accept_request(int);

void bad_request(int);

void cat(int, FILE *);

void cannot_execute(int);

void error_die(const char *);

void execute_cgi(int, const char *, const char *, const char *);

int get_line(int, char *, int);

void headers(int, const char *);

void not_found(int);

void serve_file(int, const char *);

int startup(u_short *);

void unimplemented(int);

#endif //TINY_HTTPD_HTTPD_H
