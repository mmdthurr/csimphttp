#include "picohttpparser.c"
#include "picohttpparser.h"
#include <fcntl.h>
#include <netinet/in.h>
#include <pthread.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/epoll.h>
#include <sys/errno.h>
#include <sys/sendfile.h>
#include <sys/signal.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

int sock_fd;

int MAX_T = 1;
char *BASE_DIR = "/var/www/html";
int PORT = 5051;

void setnonblocking(int sock_fd) {
  int flags = fcntl(sock_fd, F_GETFL, 0);
  flags |= O_NONBLOCK;
  fcntl(sock_fd, F_SETFL, flags);
}

void *epoll_wait_loop_accept() {

  int size_of_basedir = strlen(BASE_DIR) + 1;

  struct epoll_event *events;

  int epfd = epoll_create1(0);
  struct epoll_event ev;
  ev.data.fd = sock_fd;
  ev.events = EPOLLIN | EPOLLET;

  epoll_ctl(epfd, EPOLL_CTL_ADD, sock_fd, &ev);
  events = calloc(1024, sizeof(ev));

  while (1) {

    int n = epoll_wait(epfd, events, 1024, -1);
    for (int i = 0; i < n; i++) {
      struct epoll_event cev = events[i];

      if (cev.data.fd == sock_fd) {
        struct epoll_event ev;

        ev.events = EPOLLIN | EPOLLONESHOT;

        while (1) {
          int clisock_fd = accept(sock_fd, NULL, NULL);
          if (clisock_fd == -1)
            break;
          else {
            setnonblocking(clisock_fd);
            ev.data.fd = clisock_fd;
            epoll_ctl(epfd, EPOLL_CTL_ADD, clisock_fd, &ev);
          }
        }
      } else {

        int clisock_fd = cev.data.fd;

        char buf[4096], *method, *path;
        int pret, minor_version;
        struct phr_header headers[100];
        size_t method_len, path_len, num_headers;
        ssize_t rret;

        int rsize = read(clisock_fd, &buf, 4096);
        int hr = phr_parse_request(buf, rsize, (const char **)&method,
                                   &method_len, (const char **)&path, &path_len,
                                   &minor_version, headers, &num_headers, 0);

        // printf("path is %.*s\n", (int)path_len, path+1);
        // int ddd = strncmp(path, "/", (int)path_len);
        // int ddd2 = strncmp(path, ".", (int)path_len);

        if (strncmp(path, "/", (int)path_len) == 0) {

          struct stat st;
          int file_fd = open("index.html", O_NONBLOCK | O_RDONLY);
          fstat(file_fd, &st);

          char rspbufbase[200];
          // char rsptxt[] = "Hello guys";
          int sr = snprintf(rspbufbase, 200,
                            "HTTP/1.1 200 OK\r\nContent-Length: "
                            "%ld\r\nContent-Type: %s\r\n\r\n",
                            st.st_size, "text/html");

          send(clisock_fd, rspbufbase, sr, 0);

          int sf = sendfile(clisock_fd, file_fd, 0, st.st_size);
          if (sf <= 0) {
            printf("sf %d errno %d\n", sf, errno);
          }
          close(file_fd);
        } else {

          char path_of[path_len + size_of_basedir];
          // strncpy(path_of, path+1, path_len);
          snprintf(path_of, path_len + size_of_basedir, "%s/%.*s", BASE_DIR,
                   (int)path_len, path + 1);
          // printf("%s\n", path_of);

          int file_fd = open(path_of, O_NONBLOCK | O_RDONLY);

          char rspbufbase[200];

          if (file_fd > 0) {
            struct stat st;
            fstat(file_fd, &st);

            char *token = strtok(path_of, ".");
            token = strtok(NULL, ".");

            char *mimet;
            if (strcmp(token, "html") == 0) {
              mimet = "text/html";
            } else {
              if ((strcmp(token, "png") == 0) | (strcmp(token, "jpg") == 0) |
                  (strcmp(token, "jpeg") == 0)) {
                mimet = "image";
              } else {
                mimet = "application/octet-stream";
              }
            }

            int sr = snprintf(rspbufbase, 200,
                              "HTTP/1.1 200 OK\r\nContent-Length: "
                              "%ld\r\nContent-Type: %s\r\n\r\n",
                              st.st_size, mimet);

            send(clisock_fd, rspbufbase, sr, 0);

            int sf = sendfile(clisock_fd, file_fd, 0, st.st_size);
            if (sf <= 0) {
              printf("sf %d errno %d\n", sf, errno);
            }
            close(file_fd);
          } else {

            int sr = snprintf(rspbufbase, 200,
                              "HTTP/1.1 404 OK\r\nContent-Length: "
                              "%ld\r\nContent-Type: %s\r\n\r\n%s",
                              strlen("NOT FOUND"), "text/plain", "NOT FOUND");

            send(clisock_fd, rspbufbase, sr, 0);
          }
        }

        close(clisock_fd);
      }
    }
  }
}

int main(int argc, char *argv[]) {

  if (argc <= 1) {
    printf("default t size %d\n base dir %s\n=====\n", MAX_T, BASE_DIR);
  } else {

    MAX_T = atoi(argv[1]);
    BASE_DIR = argv[3];
    PORT = atoi(argv[2]);
    printf("t size is %d\n port is %d\nbase dir %s\n=====\n", MAX_T, PORT,
           BASE_DIR);
  }

  struct sockaddr_in srvaddr;

  srvaddr.sin_family = AF_INET;
  srvaddr.sin_addr.s_addr = htonl(INADDR_ANY);
  srvaddr.sin_port = htons(5051);

  sock_fd = socket(AF_INET, SOCK_STREAM, 0);
  setnonblocking(sock_fd);

  int b = bind(sock_fd, (struct sockaddr *)&srvaddr, sizeof(srvaddr));
  int l = listen(sock_fd, 0);
  printf("sock %d \nb %d \nl %d\n", sock_fd, b, l);
  if (b == -1 || l == -1) {
    return -1;
  }

  signal(SIGPIPE, SIG_IGN);
  // epoll_wait_loop_accept();
  pthread_t ts[MAX_T];
  for (int i = 0; i < MAX_T; i++) {
    pthread_create(&ts[i], NULL, &epoll_wait_loop_accept, NULL);
  }
  for (int i = 0; i < MAX_T; i++) {
    pthread_join(ts[i], NULL);
  }
  return 0;
}