/***********************
******* case 01. *******
***********************/

#include <stdio.h>
#include "csapp.h"

/* Recommended max cache and object sizes */
#define MAX_CACHE_SIZE 1049000
#define MAX_OBJECT_SIZE 102400

/* You won't lose style points for including this long line in your code */
static const char *user_agent_hdr =
    "User-Agent: Mozilla/5.0 (X11; Linux x86_64; rv:10.0.3) Gecko/20120305 "
    "Firefox/10.0.3\r\n";
static const char *conn_hdr = "Connection: close\r\n";
static const char *prox_hdr = "Proxy-Connection: close\r\n";
static const char *host_hdr_format = "Host: %s\r\n";
static const char *requestlint_hdr_format = "GET %s HTTP/1.0\r\n";
static const char *endof_hdr = "\r\n";    // 요청의 끝부분을 의미.

static const char *connection_key = "Connection";
static const char *user_agent_key = "User-Agent";
static const char *proxy_connection_key = "Proxy-Connection";
static const char *host_key = "Host";

void doit(int connfd);
void parse_uri(char *uri, char *hostname, char *path, int *port);
void build_http_header(char *http_header, char *hostname, char *path, int port, rio_t *client_rio);
int connect_endServer(char *hostname, int port, char *http_header);

int main(int argc, char **argv) {
  int listenfd, connfd;
  socklen_t clientlen;
  char hostname[MAXLINE], port[MAXLINE];

  struct sockaddr_storage clientaddr; // sockaddr_storage로 해야 모든 유형의 sockaddr_@와 호환됨. 28byte size

  if (argc != 2) { // 포트넘버가 제대로 들어오지 않았을 경우
    fprintf(stderr, "usage : %s <port> \n", argv[0]);
    exit(1);
  }

  listenfd = Open_listenfd(argv[1]);
  while (1) {
    clientlen = sizeof(clientaddr);
    connfd = Accept(listenfd, (SA *)&clientaddr, &clientlen); // listenfd와 clientaddr을 합쳐 connfd 제작, 소켓 구조체에 정보 저장

    Getnameinfo((SA *)&clientaddr, clientlen, hostname, MAXLINE, port, MAXLINE, 0); // 소켓 구조체를 대응되는 호스트와 서비스이름 스트링으로 변환
    printf("Accepted connection from %s %s.\n", hostname, port);

    doit(connfd);
    Close(connfd); // 소켓 닫기
  }
  return 0;
}

/* handle the client HTTP transaction */
void doit(int fd) {
  int end_serverfd; // new

  char buf[MAXLINE], method[MAXLINE], uri[MAXLINE], version[MAXLINE];
  char endserver_http_header[MAXLINE];
  /* store the request line arguments */
  char hostname[MAXLINE], path[MAXLINE]; // new
  int port;

  rio_t rio, server_rio; // rio: client's rio, server_rio: endserver's rio
  Rio_readinitb(&rio, fd);
  Rio_readlineb(&rio, buf, MAXLINE);
  sscanf(buf, "%s %s %s", method, uri, version);

  if (strcasecmp(method, "GET")) {
    printf("Proxy does not implement the method");
    return;
  }

  /* pause the uri to get hostname, file path, port */
  parse_uri(uri, hostname, path, &port); // uri 정보를 토대로 나머지 3개 변수 값 저장

  /* build the http header which will send to the end server */
  build_http_header(endserver_http_header, hostname, path, port, &rio); // endserver로 보낼 http header 빌드

  /* connect to the end server */
  end_serverfd = connect_endServer(hostname, port, endserver_http_header); // 연결 실패 시 -1 return
  if (end_serverfd < 0) { 
    printf("connection failed\n");
    return;
  }

  Rio_readinitb(&server_rio, end_serverfd); 
  /* write the http header to endserver */
  Rio_writen(end_serverfd, endserver_http_header, strlen(endserver_http_header));

  /* receive message from end server and send to client */
  size_t n; 
  while ((n = Rio_readlineb(&server_rio, buf, MAXLINE)) != 0) { // Rio_readlineb는 byte size 반환
    printf("proxy received %ld bytes, then send\n", n); // 
    Rio_writen(fd, buf, n); // endserver에서 읽은 n byte 스트링을 클라이언트에게 write
  }
  Close(end_serverfd); // endserver file descriptor close
}

void build_http_header(char *http_header, char *hostname, char *path, int port, rio_t *client_rio) {
  char buf[MAXLINE], request_hdr[MAXLINE], other_hdr[MAXLINE], host_hdr[MAXLINE];

  /* request lint */
  sprintf(request_hdr, "GET %s HTTP/1.0\r\n", path); // request_hdr에 path 반영한 header 저장

  /* get other request header for client rio and change it */
  while (Rio_readlineb(client_rio, buf, MAXLINE) > 0) { // rio_readlineb는 개행문자 전까지 읽어온다. 그 안에 포인터가 다음 줄 맨 앞을 가리키도록 이동
    if (strcmp(buf, "\r\n") == 0) // 읽어온 놈이 "\r\n"이라면 다 읽어온 것
      break;

    /* Host */
    if (!strncasecmp(buf, "Host", strlen("Host"))) { // 최대 strlen("Host") byte까지 비교. buf 끝은 "\r\n"이기 때문에 끝까지 비교하면 다른 문자가 되어 0을 리턴하지 않음
      strcpy(host_hdr, buf); // host_hdr에 buf 복사
      continue;
    }

    /* Connection */
    if (strncasecmp(buf, "Connection", strlen("Connection")) && strncasecmp(buf, "Proxy-Connection", strlen("Proxy-Connection")) && strncasecmp(buf, "User-Agent", strlen("User-Agent"))) // 셋 중 하나도 아닌 경우
      strcat(other_hdr, buf);
    

    /* 위에서 host_hdr에 아무것도 저장되지 않은 경우 */
    if (strlen(host_hdr) == 0) 
      sprintf(host_hdr, "Host: %s\r\n", hostname); // host_hdr에 "Host: {hostname}"
      
    sprintf(http_header, "%s%s%s%s%s%s%s", 
        request_hdr,     // GET {path} HTTP/1.0\r\n
        host_hdr,        // Host: {hostname}
        conn_hdr,        // Connection: close\r\n
        prox_hdr,        // Proxy-Connection: close\r\n
        user_agent_hdr,  // User-Agent: Mozilla/5.0 (X11; Linux x86_64; rv:10.0.3) Gecko/20120305 Firefox/10.0.3\r\n
        other_hdr,       // 무언가..
        endof_hdr);      // \r\n
  }
  return;
}

inline int connect_endServer(char *hostname, int port, char *http_header) {
  char portStr[100];
  sprintf(portStr, "%d", port); // portStr에 port 값 저장
  return Open_clientfd(hostname, portStr); // proxy 서버를 client로 하는 file descriptor가 리턴된다
}

/* parse the uri to get hostname, file path, port */
void parse_uri(char *uri, char *hostname, char *path, int *port) {
  *port = 8000; // 디플트 값.
  char *pos = strstr(uri, "//");    // 문자열에 담긴 '//'을 찾고 그 위치를 반환. 못 찾으면 NULL. http'//' 인 경우에 무시하려고 넣은 코드인듯
  
  pos = pos != NULL ? pos+2 : uri;  // pos안에 '//'가 있으면 포인터를 슬래시 다음으로, 없으면 pos = uri

  char *pos2 = strstr(pos, ":");  // ':' 찾고 포인터 반환
  if (pos2 != NULL) {   // ':'가 있다면 ex) localhost:8000/home.html
    *pos2 = '\0';   // 이게 무슨 뜻이지? : 자리에 \0이 들어온다. 스트링을 끊어주는 역할.
    sscanf(pos, "%s", hostname);  // localhost
    sscanf(pos2+1, "%d%s", port, path); // port : 8000, path = /home.html
  } else {    // ':'가 없다면 무슨 경우? 포트를 입력하지 않았을 때. 디폴트 포트 8000 사용
    pos2 = strstr(pos, "/");
    if (pos2 != NULL) {
      *pos2 = '\0';
      sscanf(pos, "%s", hostname);
      *pos2 = '/';
      sscanf(pos2, "%s", path);
    } else {
      sscanf(pos, "%s", hostname);
    }
  }
  return;
}