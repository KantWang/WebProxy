/***********************
******* case 03. *******
***********************/

#include <stdio.h>
#include "csapp.h"

/* Recommended max cache and object sizes */
#define MAX_CACHE_SIZE 1049000
#define MAX_OBJECT_SIZE 102400

/* 
  Least Recently Used
  LRU: 가장 오랫동안 참조되지 않은 페이지를 교체하는 기법
*/ 
#define LRU_MAGIC_NUMBER 9999
#define CACHE_OBJS_COUNT 10

/* You won't lose style points for including this long line in your code */
static const char *user_agent_hdr =
    "User-Agent: Mozilla/5.0 (X11; Linux x86_64; rv:10.0.3) Gecko/20120305 "
    "Firefox/10.0.3\r\n";
static const char *requestline_hdr_format = "GET %s HTTP/1.0\r\n";
static const char *endof_hdr = "\r\n";
static const char *host_hdr_format = "Host: %s\r\n";
static const char *conn_hdr = "Connection: close\r\n";
static const char *prox_hdr = "Proxy-Connection: close\r\n";

static const char *host_key = "Host";
static const char *connection_key = "Connection";
static const char *proxy_connection_key = "Proxy-Connection";
static const char *user_agent_key = "User-Agent";

void *thread(void *vargs);
void doit(int connfd);
void parse_uri(char *uri, char *hostname, char *path, int *port);
void build_http_header(char *http_header, char *hostname, char *path, int port, rio_t *client_rio);
int connect_endServer(char *hostname, int port, char *http_header);

// cache function
void cache_init();
int cache_find(char *url);
void cache_uri(char *uri, char *buf);

void readerPre(int i);
void readerAfter(int i);

typedef struct {
  char cache_obj[MAX_OBJECT_SIZE];
  char cache_url[MAXLINE];
  int LRU; // least recently used 가장 최근에 사용한 것의 우선순위를 뒤로 미움 (캐시에서 삭제할 때)
  int isEmpty; // 이 블럭에 캐시 정보가 들었는지 empty인지 아닌지 체크

  int readCnt;  // count of readers
  sem_t wmutex;  // protects accesses to cache 세마포어 타입. 1: 사용가능, 0: 사용 불가능
  sem_t rdcntmutex;  // protects accesses to readcnt
} cache_block; // 캐쉬블럭 구조체로 선언


typedef struct {
  cache_block cacheobjs[CACHE_OBJS_COUNT];  // ten cache blocks
  int cache_num; // 캐시(10개) 넘버 부여
} Cache;

Cache cache; // 전역으로 cache 선언. 10개의 캐시블락을 가지고 있고 index인 cache_num을 가지고 있다

/* proxy server 실행 명령 "./proxy 5000" */
int main(int argc, char **argv) { // argv[1] == "5000"
  int listenfd, *connfd;
  socklen_t clientlen;
  char hostname[MAXLINE], port[MAXLINE];
  pthread_t tid;
  struct sockaddr_storage clientaddr; // sockaddr_storage로 해야 모든 유형의 sockaddr_@와 호환됨. 28byte size

  cache_init(); // 모든 캐시가 사용 가능하고 잠기지 않았다고 설정

  if (argc != 2) { // 포트넘버가 제대로 들어오지 않았을 경우
    fprintf(stderr, "usage: %s <port> \n", argv[0]); // fprintf: 출력을 파일에다 씀. strerr: 파일 포인터
    exit(1); // exit(1): 에러 시 강제 종료
  }
  Signal(SIGPIPE, SIG_IGN); 
  /*
    SISGPIPE는 절단된 네트워크 소켓 등에 데이터를 쓰려고 했을 때 전달되는 UNIX 시그널. 
    별도의 설정이 없을 시 프로세스는 SIGPIPE를 받으면 종료된다 (전체가 다).
    Signal(SIGPIPE, SIG_IGN)은 커널에게 만약 SIGPIPE 시그널이 발생하게 되면 무시하라는 명령어.
    SIGPIPE: 시그널의 종류, IGN: Ignore Signal
    Signal(시그널 종류, 대응방식) 이라고 해석하면 될듯하다
    SIGPIPE 상세 들어가보면 Historical signals specified by POSIX. 여러 유형을 확인해볼 것
  */

  listenfd = Open_listenfd(argv[1]); // proxy server의 listenfd를 생성
  while (1) {
    clientlen = sizeof(clientaddr);

    /*
      네트워크 상황에 따라 Accept의 처리가 pthread_create를 돌고 나서 끝날 수도 있고, pthread_create에 도착하기 전에 끝날 수도 있다.
      우리가 원하는 상황은 Accept가 pthread_create를 돌고 난 후에 끝나는 것.
      만약 그 전에 끝나게 되면 분리된 두 쓰레드가 같은 connfd에 대한 처리를 하게 된다.
      이렇게 되면 쓰레드를 써서 어떤 결과가 나올 지 예측할 수 없다.
      이를 방지하기 위해 각각의 연결 식별자를 동적으로 할당할 필요가 있다.
      단지 변수로 선언하면 여러 쓰레드가 참조하는 connfd는 하나가 되지만
      동적 할당을 하게 되면 여러 쓰레드가 참조하는 connfd는 서로 다른 변수가 된다.
    */
    connfd = (int *)Malloc(sizeof(int)); // 피어 스레드 분리 시 경쟁 상태를 피하기 위한 동적 할당.
    *connfd = Accept(listenfd, (SA *)&clientaddr, &clientlen);
    /*
      Accept에서 수신할 때까지 대기
      client가 "GET http://3.35.209.121:8000/home.html HTTP/1.1"와 같은 명령 입력
      Open_Clientfd는 목적지 server의 hostname과 port 정보를 가진 소켓 식별자를 리턴 --> clientfd
      connect 함수를 통해 cliendfd와 클라이언트 소켓 정보를 담은 요청이 accept에 접수, 이후 connfd 반환
    */

    Getnameinfo((SA *)&clientaddr, clientlen, hostname, MAXLINE, port, MAXLINE, 0);
    printf("Accepted connection from (%s %s).\n", hostname, port); // 여기서 hostname과 port는 tiny server의 host, port

    Pthread_create(&tid, NULL, thread, (void *)connfd);
    /*
      extern int pthread_create (
        pthread_t *__restrict __newthread,                     // 쓰레드 식별자
        const pthread_attr_t *__restrict __attr,               // 쓰레드 특성, 일반적으로 0
        void *(*__start_routine) (void *),                     // pthread_create가 시작되었을 때 실행할 함수 이름
        void *__restrict __arg) __THROWNL __nonnull ((1, 3));  // 실행될 함수에 보낼 입력 파라미터
    */
    // doit(connfd);
    // Close(connfd);
  }
  return 0;
}

void *thread(void *vargs) { // main에서 동적할당한 connfd가 들어온다(주소 값)
  int connfd = *((int *)vargs); // file descriptor 값을 쓰레드의 connfd에 저장
  Pthread_detach(pthread_self());
  /*
    int pthread_detach(pthread_t th);
    pthread_self()는 자기 자신
    분리시키지 않으면 쓰레드가 종료되어도 사용했던 모든 자원이 해제되지 않음
    분리 시 pthread_join() 명령 없이도 쓰레드 종료 시 모든 자원 해제
  */
  Free(vargs); // 메모리 누수를 피하기 위해 server에서 동적할당한 connfd 메모리 반환
  doit(connfd);
  Close(connfd); // 사용을 마친 file descriptor Close
  return NULL;
}

void doit(int connfd) {
  int end_serverfd; // tiny server

  char buf[MAXLINE], method[MAXLINE], uri[MAXLINE], version[MAXLINE];
  char endserver_http_header[MAXLINE];
  char hostname[MAXLINE], path[MAXLINE];
  int port;

  // rio: client's rio / server_rio: tiny's rio
  rio_t rio, server_rio;

  Rio_readinitb(&rio, connfd);
  Rio_readlineb(&rio, buf, MAXLINE);
  sscanf(buf, "%s %s %s", method, uri, version);  // read the client reqeust line

  if (strcasecmp(method, "GET")) { // 메소드가 GET이 아니면 return
    printf("Proxy does not implement the method");
    return;
  }

  char url_store[100];
  strcpy(url_store, uri); // doit으로 받아온 connfd가 들고있는 uri("http://3.35.209.121:8000/home.html")를 넣어준다 

  /* uri가 캐시에 들어 있다면 바로 꺼내온다 */
  int cache_index;
  // in cache then return the cache content
  // cache_find는 10개의 캐시블럭에서 못찾으면 -1 반환, 찾으면 index 반환
  if ((cache_index=cache_find(url_store)) != -1) { 
    readerPre(cache_index); // 캐시 뮤텍스를 풀어줌 (열어줌 0->1)
    Rio_writen(connfd, cache.cacheobjs[cache_index].cache_obj, strlen(cache.cacheobjs[cache_index].cache_obj));
    // 캐시에서 찾은 값을 connfd에 쓰고, 캐시에서 그 값을 바로 보내게 됨
    /*
      ssize_t rio_writen(int fd, void *usrbuf, size_t n);
      usrbuf에서 찾은 값을 fd로 전송
    */
    readerAfter(cache_index); // 닫아줌 1->0 doit 끝
    return;
  }

  // parse the uri to get hostname, file path, port
  parse_uri(uri, hostname, path, &port); // uri 정보를 토대로 나머지 3개 변수 값 저장

  // build the http header which will send to the end server
  build_http_header(endserver_http_header, hostname, path, port, &rio); // endserver로 보낼 http header 빌드

  // connect to the end server
  end_serverfd = connect_endServer(hostname, port, endserver_http_header); // 연결 실패 시 -1 return
  if (end_serverfd < 0) {
    printf("connection failed\n");
    return;
  }

  Rio_readinitb(&server_rio, end_serverfd);
  // write the http header to endserver
  Rio_writen(end_serverfd, endserver_http_header, strlen(endserver_http_header));

  // recieve message from end server and send to the client
  char cachebuf[MAX_OBJECT_SIZE];
  int sizebuf = 0;
  size_t n; // 캐시에 없을 때 찾아주는 과정?
  while ((n=Rio_readlineb(&server_rio, buf, MAXLINE)) != 0) {
    // printf("proxy received %ld bytes, then send\n", n);
    sizebuf += n;
    /* proxy거쳐서 서버에서 response오는데, 그 응답을 저장하고 클라이언트에 보냄 */
    if (sizebuf < MAX_OBJECT_SIZE) // 작으면 response 내용을 적어놈
      strcat(cachebuf, buf); // cachebuf에 buf(response값) 다 이어붙혀놓음(캐시내용)
    Rio_writen(connfd, buf, n); // buf에서 읽은 n byte 스트링을 클라이언트에게 write
  }
  Close(end_serverfd); 

  // store it
  if (sizebuf < MAX_OBJECT_SIZE) {
    cache_uri(url_store, cachebuf); // url_store에 cachebuf 저장
  }
}

void build_http_header(char *http_header, char *hostname, char *path, int port, rio_t *client_rio) {
  char buf[MAXLINE], request_hdr[MAXLINE], other_hdr[MAXLINE], host_hdr[MAXLINE];

  // request line
  sprintf(request_hdr, requestline_hdr_format, path);

  // get other request header for client rio and change it
  while (Rio_readlineb(client_rio, buf, MAXLINE) > 0) { // rio_readlineb는 개행문자 전까지 읽어온다. 그 안에 포인터가 다음 줄 맨 앞을 가리키도록 이동
    if (strcmp(buf, endof_hdr) == 0) // 읽어온 놈이 "\r\n"이라면 다 읽어온 것
      break;  // EOF

    /*
      Host
      최대 strlen("Host") byte까지 비교
      buf 끝은 "\r\n"이기 때문에 끝까지 비교하면 다른 문자가 되어 0을 리턴하지 않음
    */
    if (!strncasecmp(buf, host_key, strlen(host_key))) {
      strcpy(host_hdr, buf); // host_hdr에 buf 복사
      continue;
    }

    /* Connection */
    if (strncasecmp(buf, connection_key, strlen(connection_key))
        &&strncasecmp(buf, proxy_connection_key, strlen(proxy_connection_key))
        &&strncasecmp(buf, user_agent_key, strlen(user_agent_key))) {
        strcat(other_hdr, buf);
      }
  }
  /* 위에서 host_hdr에 아무것도 저장되지 않은 경우 */
  if (strlen(host_hdr) == 0)
    sprintf(host_hdr, host_hdr_format, hostname);
  
  sprintf(http_header, "%s%s%s%s%s%s%s",
    request_hdr, // GET {path} HTTP/1.0\r\n
    host_hdr, // Host: {hostname}\r\n
    conn_hdr, // Connection: close\r\n
    prox_hdr, // Proxy-Connection: close\r\n
    user_agent_hdr, // User-Agent: Mozilla/5.0 (X11; Linux x86_64; rv:10.0.3) Gecko/20120305 Firefox/10.0.3\r\n
    other_hdr,
    endof_hdr); // \r\n
  return;
}

// Connect to the end server
inline int connect_endServer(char *hostname, int port, char *http_header) {
  char portStr[100];
  sprintf(portStr, "%d", port);
  return Open_clientfd(hostname, portStr);
}

// parse the uri to get hostname, file path, port
void parse_uri(char *uri, char *hostname, char *path, int *port) {
  *port = 80;
  char *pos = strstr(uri, "//");

  pos = pos!=NULL? pos+2:uri;

  char *pos2 = strstr(pos, ":");
  // sscanf(pos, "%s", hostname);
  if (pos2 != NULL) {
    *pos2 = '\0';
    sscanf(pos, "%s", hostname);
    sscanf(pos2+1, "%d%s", port, path);
  } else {
    pos2 = strstr(pos, "/");
    if (pos2 != NULL) {
      *pos2 = '\0';  // 중간에 끊으려고
      sscanf(pos, "%s", hostname);
      *pos2 = '/';
      sscanf(pos2, "%s", path);
    } else {
      scanf(pos, "%s", hostname);
    }
  }
  return;
}

void cache_init() {
  cache.cache_num = 0; // CACHE_OBJS_COUNT == 10이므로  cache_num은 0 ~ 9 
  int i;
  for (i=0; i<CACHE_OBJS_COUNT; i++) {
    cache.cacheobjs[i].LRU = 0; // 우선순위 모두 0으로 초기화
    cache.cacheobjs[i].isEmpty = 1; // 모두 비어 있음을 의미하는 1로 초기화

    /*
      int sem_init(sem_t *sem, int pshared, unsigned int value);
      sem: 세마포어 포인터. 위 경우 cache의 cache block[i]의 wmutex, rdcntmutex
      pshared(process shared): 세마포어 공유 설정. 0이면 쓰레드 간 공유 / 0이 아니면 프로세스 간 공유. 위 경우 모두 쓰레드 간 공유
      value: 초기 값. wmutex == 1이면 접근 가능, rdcntmutex == 1이면 접근 가능
    */
    Sem_init(&cache.cacheobjs[i].wmutex, 0, 1); // wmutex : 캐시에 접근하는 것을 프로텍트해주는 뮤텍스
    Sem_init(&cache.cacheobjs[i].rdcntmutex, 0, 1); // read count mutex : readCnt에 접근하는걸 프로텍트해주는 뮤텍스

    cache.cacheobjs[i].readCnt = 0; // cash block의 read count까지 모두 0으로 초기화
  }
}

void readerPre(int i) { // i번 cache block
  /*
    P(S):
      wait(S) {
        while (S <= 0); <-- S가 음수면 무한루프. 누군가 S++ 해서 양수 만들어줘야 탈출이 가능
        S--;
      }

    V(S):
      signal(S) {
        S++; <-- 여기서 S를 양수 만들어 준다
      }
  */

  /* 
    rdcntmutex 초기값 == 1 (접근 가능)
    rdcntmutex로 특정 cache block에 접근하고 rdcntmutex -= 1 
    rdntmutex == 0이면 무한루프가 실행되고 V 함수 실행 전까지 sem_wait (이진 세마포어)
  */
  P(&cache.cacheobjs[i].rdcntmutex); // P연산(locking):정상인지 검사, 기다림
  cache.cacheobjs[i].readCnt++;

  /*
    처음 read하는 쓰레드가 도착하면 write 못하게 P(wmutex) 실행
  */
  if (cache.cacheobjs[i].readCnt == 1) 
    P(&cache.cacheobjs[i].wmutex); // write mutex 뮤텍스를 풀고(캐시에 접근)

  /*
    read lock 해제
    rdcntmutex += 1 --> rdcntmutex == 0이 되어 앞선 P(rdcntmutex) while문 탈출
  */
  V(&cache.cacheobjs[i].rdcntmutex);
}

void readerAfter(int i) {
  /*
    readCnt 보호를 위해 다시 lock 
    V(rdcntmutex) 실행 후 무한루프 탈출. 다음 쓰레드 진입 가능
  */
  P(&cache.cacheobjs[i].rdcntmutex);
  cache.cacheobjs[i].readCnt--;

  /*
    대기하는 다음 쓰레드가 없다면, V(wmutex)를 통해 wmutex += 1 -> P(wmutex) 무한루프 탈출 -> write 가능
  */
  if (cache.cacheobjs[i].readCnt == 0)
    V(&cache.cacheobjs[i].wmutex);

  V(&cache.cacheobjs[i].rdcntmutex);
}

/*
  for문 돌아 같은 녀석 발견할 시 캐시적중, 해당 캐시 블록 index return
*/
int cache_find(char *url) {
  int i;
  for (i=0; i<CACHE_OBJS_COUNT; i++) { 
    readerPre(i); // 캐시 write lock
    if ((cache.cacheobjs[i].isEmpty == 0) && (strcmp(url, cache.cacheobjs[i].cache_url) == 0)) // 캐시가 비어 있지 않고 cache에 저장된 url과 인자 url이 일치하는 경우 캐시 적중
      break;
    readerAfter(i); // 캐시 write lock 해제
  }
  if (i >= CACHE_OBJS_COUNT) // 끝까지 못 찾은 경우
    return -1;
  return i;
}

/*
  빈 캐시가 존재할 경우 빈 캐시의 index 반환
  존재하지 않을 경우 LRU 값이 가장 작은 캐시 블록의 index 반환
*/
int cache_eviction() {
  int min = LRU_MAGIC_NUMBER;
  int minindex = 0;
  int i;
  for (i=0; i<CACHE_OBJS_COUNT; i++) {
    readerPre(i);
    if (cache.cacheobjs[i].isEmpty == 1) {
      minindex = i;
      readerAfter(i);
      break;
    }
    if (cache.cacheobjs[i].LRU < min) {
      minindex = i;
      min = cache.cacheobjs[i].LRU;
      readerAfter(i);
      continue;
    }
    readerAfter(i);
  }
  return minindex;
}

void writePre(int i) {
  P(&cache.cacheobjs[i].wmutex);
}

void writeAfter(int i) {
  V(&cache.cacheobjs[i].wmutex);
}

// update the LRU number except the new cache one
void cache_LRU(int index) {
  int i;
  // 인자로 들어온 캐시 블록 제외 LRU를 모두 감소 시킨다
  for (i=0; i<CACHE_OBJS_COUNT; i++) {
    if (i == index)
      continue;
    
    writePre(i);
    if (cache.cacheobjs[i].isEmpty == 0 && i != index)
      cache.cacheobjs[i].LRU--;
    writeAfter(i);
  }
}

// cache the uri and content in cache
void cache_uri(char *uri, char *buf) {
  int i = cache_eviction(); // 빈 캐시 블럭을 찾는 첫번째 index
  writePre(i); // lock
  /*
    target 캐시 블록에 writing
  */
  strcpy(cache.cacheobjs[i].cache_obj, buf);
  strcpy(cache.cacheobjs[i].cache_url, uri);
  cache.cacheobjs[i].isEmpty = 0; // 0: 비어 있지 않다
  cache.cacheobjs[i].LRU = LRU_MAGIC_NUMBER; // 가장 최근에 했으니 우선순위 9999로
  cache_LRU(i); // i 제외 모든 캐시 블록의 LRU--

  writeAfter(i); // lock 해제
}