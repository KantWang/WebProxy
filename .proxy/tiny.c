/* $begin tinymain */
/*
 * tiny.c - A simple, iterative HTTP/1.0 Web server that uses the
 *     GET method to serve static and dynamic content.
 *
 * Updated 11/2019 droh
 *   - Fixed sprintf() aliasing issue in serve_static(), and clienterror().
 */
#include "csapp.h"

void doit(int fd);
void echo(int connfd); // homework 11.6-A
void read_requesthdrs(rio_t *rp);
int parse_uri(char *uri, char *filename, char *cgiargs);
// void serve_static(int fd, char *filename, int filesize);
void serve_static(int fd, char *filename, int filesize, char *method); // homework 11.11 HEAD 메소드 지원
void get_filetype(char *filename, char *filetype);
// void serve_dynamic(int fd, char *filename, char *cgiargs);
void serve_dynamic(int fd, char *filename, char *cgiargs, char *method); // homework 11.11 HEAD 메소드 지원
void clienterror(int fd, char *cause, char *errnum, char *shortmsg,
                 char *longmsg);

int main(int argc, char **argv) { // port number가 인자로 들어옴. argv[1]에 저장. argc는 argv의 길이
  int listenfd, connfd;
  char hostname[MAXLINE], port[MAXLINE];
  socklen_t clientlen;
  struct sockaddr_storage clientaddr;

  /* Check command line args */
  if (argc != 2) {
    fprintf(stderr, "usage: %s <port>\n", argv[0]);
    exit(1);
  }

  // printf("hihi!"); 개행문자 없으면 버퍼에 쌓이고 출력이 안된다
  // fputs("what?!!?!?", stdout);

  listenfd = Open_listenfd(argv[1]);
  while (1) {
    clientlen = sizeof(clientaddr);
    connfd = Accept(listenfd, (SA *)&clientaddr, &clientlen); // client 소켓 주소 addr은 원래 ip:port의 형태인데 sockaddr_in과 같은 타입으로 캐스팅되어 날아온다. 그 정보를 토대로 clientaddr, clientlen 포인터에 값이 저장되는 것
    Getnameinfo((SA *)&clientaddr, clientlen, hostname, MAXLINE, port, MAXLINE, 0); // hostname에는 client의 ip주소가, port에는 client의 port주소가 저장. client 소켓 주소 ip:port에서의 ip, port라고 생각하면 된다. 현재 유동ip라 ip는 계속 변함
    printf("Accepted connection from (%s, %s)\n", hostname, port);
    doit(connfd);
    // echo(connfd); // homework 11.6-A   
    Close(connfd);
  }
  // printf("where?"); print 출력이 어떻게 될지 시험해 본 흔적
}

/* homework 11.6-A */
void echo(int connfd) {
  size_t n;
  char buf[MAXLINE];
  rio_t rio;

  Rio_readinitb(&rio, connfd);
  while((n = Rio_readlineb(&rio, buf, MAXLINE)) != 0) {
    // printf("server received %d bytes\n", (int)n);
    if (strcmp(buf, "\r\n") == 0)
      break;
    Rio_writen(connfd, buf, n);
    // Rio_writen(connfd, "Hello, World!\r\n", 15); // connfd 스트림이 향하는 곳에 출력 
    // printf("where 2\n"); // stdout이 향하는 곳에 출력
  }
}

void doit(int fd){ // connfd가 들어옴
  int is_static; // 정적컨텐츠면 1, 동적컨텐츠면 0
  struct stat sbuf; // 파일의 상태 및 정보를 저장할 구조체
  /*
  struct stat {
    dev_t st_dev; // ID of device containing file
    ino_t st_ino; // inode number
    ...
    off_t st_size; // 파일의 크기(bytes)
    ...
  }
  */
  char buf[MAXLINE], method[MAXLINE], uri[MAXLINE], version[MAXLINE];
  /*
    예를들면,
    *buf == "GET /cgrbin/adder?15000&213 HTTP/1.1" HEAD /cgi-bin/adder?15000&213 HTTP/1.1
    *method == "GET"
    *url == "/cgrbin/adder?15000&213"
    *uri == "15000&213"
    *version == "HTTP/1.1"
  */
  char filename[MAXLINE], cgiargs[MAXLINE];

  // rio_readlineb를 위해 rio_t 타입(구조체)의 읽기 버퍼를 선언
  rio_t rio;
  // fd가 가리키는 메모리 공간을 rio_t 구조체로 초기화
  Rio_readinitb(&rio, fd);
  // Rio_readlineb를 통해 fd가 가리키는 공간의 모든 문자열을 buf에 저장
  Rio_readlineb(&rio, buf, MAXLINE);
  printf("Request headers : \n");
  printf("%s", buf); // buf에 저장되어 있는 모든 문자열을 출력 // 여기서 3번째, version 정보를 확인하면 homework 11.6-B 
  sscanf(buf, "%s %s %s", method, uri, version); // buf에서 문자열을 읽어와 method, uri, version에 저장
  // printf("  method: %s, uri: %s, version: %s\n", method, uri, version); // 저장 값 확인한 번 해본 흔적

  // TINY는 GET method만 지원하기에 클라이언트가 다른 메소드를 요청하면 에러메시지 출력 후 doit함수 종료
  // homework 11.11 HTTP HEAD 메소드 추가
  if (strcasecmp(method, "GET") && strcasecmp(method, "HEAD")){ // strcasecmp 두 개 문자열 인자를 소문자 변환 후 비교, 같으면 0 반환
    clienterror(fd, method, "501", "Not implemented", "GET 요청만 받을 수 있습니다."); // homework 11.11 HEAD 메소드 추가
    return;
  }
  // get method 라면 읽어들이고, 다른 요청 헤더들을 무시한다.
  read_requesthdrs(&rio); // 버퍼에서 헤더 문자열 \r\n 나올 때까지 한 줄 한 줄 꺼내서 제거 

  // parse uri form GET request
  // uri는 "/" 일수도 있고(정적) "/cgi-bin/adder?15000&213" 이럴 수도 있고(동적)
  is_static = parse_uri(uri, filename, cgiargs); // uri가 static이면 1, 아니면 0. filename과 cgiargs에 적절한 값이 저장됨. uri가 위 우측일 경우 filename == "/cgi-bin/adder", cgiargs = "15000&213"

  if (stat(filename, &sbuf) < 0){ // sbuf에 file의 상태 및 정보를 저장. filename은 포인터. 정상적으로 file을 가리키지 않으면 에러가 뜨겠지
    clienterror(fd, filename, "404", "Not Found", "파일을 찾을 수 없습니다.");
    return;
  }

  // 정적 컨텐츠라면
  if (is_static){
    if(!(S_ISREG(sbuf.st_mode)) || !(S_IRUSR & sbuf.st_mode)){ // S_ISREG: 일반파일인지 여부, S_IRUSR: 접근권한값 00400
      clienterror(fd, filename, "403", "Forbidden", "파일을 읽을 수 없습니다."); // 보통파일이 아니거나 읽기권한이 없는 경우 에러
      return;
    }
    // 보통파일이고 읽기권한 있는 경우에 connfd에 정적 컨텐츠 제공
    serve_static(fd, filename, sbuf.st_size, method); // homework 11.11 HEAD 메소드 추가
  }
  // 동적 컨텐츠라면
  else{
    if (!(S_ISREG(sbuf.st_mode)) || !(S_IXUSR & sbuf.st_mode)){ // S_IXUSR: 실행권한값 00100
      clienterror(fd, filename, "403", "Forbidden", "파일을 읽을 수 없습니다");
      return;
    }
    // 보통파일이고 실행권한 있는 경우에 connfd에 동적 컨텐츠 제공
    serve_dynamic(fd, filename, cgiargs, method); // homework 11.11 HEAD 메소드 추가
  } 
}

/* 명백한 오류에 대해 클라이언트에 보고하는 함수. HTTP응답을 응답 라인에 적절한 상태 코드와 상태 메시지와 함께 클라이언트에 보낸다.*/
void clienterror(int fd, char *cause, char *errnum, char *shortmsg, char *longmsg){
  char buf[MAXLINE], body[MAXBUF];
  /* BUILD the HTTP respons body
    브라우저 사용자에게 에러를 설명하는 응답 본체에 HTMLeh 함께 보낸다.
    HTML 응답은 본체에서 컨텐츠의 크기와 타입을 나타내야 하기에, HTML 컨텐츠를 한 개의 스트링으로 만든다.
    이는 sprintf를 통해 body는 인자에 스택되어 하나의 긴 스트링에 저장된다.*/

    sprintf(body, "<html><title>ERROR</title>");
    sprintf(body, "%s<body bgcolor=""ffffff"">\r\n", body);
    sprintf(body, "%s%s: %s\r\n", body, errnum, shortmsg);
    sprintf(body, "%s<p>%s: %s\r\n", body, longmsg, cause);
    sprintf(body, "%s<hr><em>The Tiny Web server</em>\r\n", body);

    //print the HTTP response
    sprintf(buf, "HTTP/1.1 %s %s\r\n", errnum, shortmsg);           
    Rio_writen(fd, buf, strlen(buf));                               // HTTP/1.1 403 Forbidden

    sprintf(buf, "Content-type: test/html\r\n");
    Rio_writen(fd, buf, strlen(buf));                               // Content-type: test/html
                                                                    // 
    sprintf(buf, "Content-length: %d\r\n\r\n", (int)strlen(body));  
    Rio_writen(fd, buf, strlen(buf));                               // Content-length: %d
    Rio_writen(fd, body, strlen(body));                             // <html><title>ERROR</title>...
}

/* tiny는 요청 헤더 내의 어떤 정보도 사용하지 않아서, 헤더를 무시하기 위해 이 함수 호출 */
void read_requuesthdrs(rio_t *rp){
  char buf[MAXLINE];
  Rio_readlineb(rp, buf, MAXLINE); // 텍스트를 rp에서 읽고 메모리위치 buf에 MAXLINE 만큼 복사 (\r\n을 기점으로 한 줄 까지만 read)
  
  while (strcmp(buf, "\r\n")){ // 한 줄 읽어온 것이 \r\n일 때까지, 즉 헤더 데이터 다 날릴 때까지
    Rio_readlineb(rp, buf, MAXLINE);
    printf("%s", buf);
  }
  return;
}

/* Parse URI from GET request
 * URI를 파일 이름과 비어 있을 수 도 있는 CGI 인자 스트링으로 분석하고
 * 요청이 정적 또는 동적 컨텐츠를 위한 것인지 나타내는 플래그를 설정한다.*/
int parse_uri(char *uri, char *filename, char *cgiargs){
  char *ptr;

  // uri 맨 앞은 "/"이므로 strstr(uri, "cgi-bin")이 0을 리턴할 일은 없다(찾으면 무조건 1 이상의 정수)
  // 따라서 참이되는 경우는 cgi-bin을 찾지 못해 NULL을 리턴하는 경우
  if (!strstr(uri, "cgi-bin")){ // 참이면 정적 콘텐츠
    strcpy(cgiargs, "");
    strcpy(filename,".");
    strcat(filename, uri);
    // 결과 cgiargs="" 공백 문자열, filename="./~~  or ./home.html
    // uri 문자열 끝이 /일 경우 허전하지 말라고 home.html을 filename에 붙혀준다.

    if(uri[strlen(uri)-1]=='/'){ // uri의 마지막 문자가 "/"라면 filename을 ./home.html으로
      strcat(filename, "home.html"); // 예를들어 "/index.html"이었다면 위의 strcat만으로도 filename이 "./index.html"로 완성되니 충분
    }
    return 1;
  } 
  else { // 동적 컨텐츠라면
    ptr=index(uri,'?'); // uri에서 '?'의 위치값 반환
    
    if(ptr){ // ptr > 0, 즉 ?가 uri에 존재
      strcpy(cgiargs,ptr+1); // ? 다음 문자열부터 cgiargs에 복사
      *ptr='\0'; // '?'를 NULL로 변경
    }
    else{ // ?가 uri에 존재하지 않으면
      strcpy(cgiargs,"");
    }
    strcpy(filename,".");
    strcat(filename,uri); // 예를들어 filename은 ./cgi-bin/adder     왜? NULL에서 strcat이 끊긴다
    return 0;
  }
}

  /* get_filetype - Derive file type from filename
   * strstr 두번째 인자가 첫번째 인자에 들어있는지 확인
   */
void get_filetype(char *filename, char *filetype){
  if(strstr(filename, ".html")){
    strcpy(filetype, "text/html");
  }else if(strstr(filename, ".gif")){
    strcpy(filetype, "image/gif");
  }else if(strstr(filename, ".png")){
    strcpy(filetype,"image/png");
  }else if(strstr(filename,".jpg")){
    strcpy(filetype,"image/jpeg");
  }else if(strstr(filename, ".mp4")){
    strcpy(filetype, "video/mp4");
  }else if(strstr(filename, ".mpeg")){ // homework 11.7
    strcpy(filetype, "video/mpeg");
  }else if(strstr(filename, ".MOV")){
    strcpy(filetype, "video/MOV");
  }else{
    strcpy(filetype, "text/plain");
  }
}

void serve_static(int fd, char *filename, int filesize, char *method){
  int srcfd;
  char *srcp, filetype[MAXLINE], buf[MAXBUF],*fbuf;
  /* Send response headers to client */
  get_filetype(filename, filetype);
  sprintf(buf, "HTTP/1.1 200 OK\r\n");
  sprintf(buf, "%sServer: Tiny Web Server\r\n",buf);
  sprintf(buf, "%sConnection: close\r\n", buf);
  sprintf(buf, "%sContent-length: %d\r\n", buf, filesize);
  sprintf(buf, "%sContent-type: %s\r\n\r\n",buf,filetype);
  
  /* writen = client 쪽에 */
  Rio_writen(fd,buf,strlen(buf));
  /* 서버쪽에 출력 */
  printf("Response headers:\n");
  printf("%s", buf);

  if (strcasecmp(method, "HEAD") == 0) // homework 11.11 HEAD 메소드 추가. HEAD 메소드는 body 출력 X
    return;

  /* Send response body to client */
  srcfd = Open(filename, O_RDONLY, 0);
  // srcp = Mmap(0, filesize, PROT_READ, MAP_PRIVATE, srcfd, 0); // 해당 코드는 메모리 할당 + 복사까지 한 번에 이뤄짐
  /* 
    Mmap 함수는 요청한 파일을 가상메모리 영역으로 매핑. 
    void * mmap (void *address, size_t length, int protect, int flags, int filedes, off_t offset)
      protect: 보호모드. 매핑한 데이터를 읽기만 할지 쓰거나 실행도 허용할지 결정
        - PROT_READ: 매핑된 파일을 읽기만 한다.
        - ...
      flags: 매핑된 데이터를 처리하기 위한 다른 정보를 지정.
        - MAP_PRIVATE: 데이터의 변경 내용을 공유하지 않는다.
        - ...
      filedes: 매핑할 파일의 file descriptor
      offset: 매핑할 파일의 file offset - 파일의 시작지점부터 현재 커서의 위치까지 얼마나 떨어져 있는지 정수로 보여주는 것
  */
  srcp = (char*)Malloc(filesize); // 동적할당 방식은 메모리 할당, 복사를 따로 진행
  Rio_readn(srcfd, srcp, filesize); // filesize만큼 read. 즉 전부 다 srcp에 복사
  Close(srcfd); // 메모리 누수를 막기위한 free.
  Rio_writen(fd, srcp, filesize); 
  // Munmap(srcp, filesize); // 메모리 누수를 막기위한 free. mmap 함수로 메모리 매핑 하지 않았으니 주석
  free(srcp); // free
}

void read_requesthdrs(rio_t *rp){
  char buf[MAXLINE];
  Rio_readlineb(rp, buf, MAXLINE);
  /* strcmp 두 문자열을 비교하는 함수 */
  /* 헤더의 마지막 줄은 비어있기에 \r\n 만 buf에 담겨있다면 while문을 탈출한다.*/
  while(strcmp(buf, "\r\n")){
    Rio_readlineb(rp, buf, MAXLINE);
    printf("%s", buf);
    //멈춘 지점까지 출력하고 다시 while
  }
  return;
}

void serve_dynamic(int fd, char *filename, char *cgiargs, char *method){
  char buf[MAXLINE], *emptylist[]={NULL};
  /* Return first part of HTTP response */
  sprintf(buf, "HTTP/1.1 200 OK\r\n");
  Rio_writen(fd, buf, strlen(buf));
  sprintf(buf, "Server: Tiny Web Server\r\n");
  Rio_writen(fd, buf, strlen(buf));

  if (Fork()==0){ // 자식 프로세스 성공 시 자식에게 0을 반환
    /* Real server would set all CGI vars hear */
    setenv("QUERY_STRING", cgiargs, 1); // 예를들어, cgiargs == "15000&213"
    setenv("REQUEST_METHOD", method, 1); // homework 11.11 HEAD 메소드 추가
    /*
      int setenv(const char *name, const char *value, int overwrite);
      환경변수 name이 존재하고 overwrite가 0이 아니면 환경변수 값을 value로 바꾼다
      즉, 환경변수 "QUERY_STRING" 값을 cgiargs로 바꾼다
      여기서 바꿔주고 아래 Execve로 adder.c를 실행하기 때문에 adder.c 안에서 getenv 함수로 cgiargs를 가져올 수 있는 것
    */
    Dup2(fd, STDOUT_FILENO); // 자식프로세스의 표준 출력을 연결 파일 식별자로 재지정
    Execve(filename, emptylist, environ); // fd로 스트림이 연결되고 실행되는 프로그램이므로 adder.c에서의 표준출력은 client를 향한다
    /*
      int execve(const char *filename, char *const argv[], char *const envp[]);
      filename이 가리키는 파일을 메모리에 로딩하여 실행(바이너리 or 스크립트 파일) 후 자신은 종료
      main(int argc, char *argv[])에서의 argv와 비슷한 역할. execve에는 argc가 없으므로 argv의 맨 끝 NULL을 활용
      environ: key=value 형식의 환경 변수 문자열 배열리스트로 마지막은 NULL
               만약 설정된 환경 변수를 사용하려면 environ 전역변수를 그냥 사용
    */
  }
  Wait(NULL); /* Parent waits for and reaps child, 부모 프로세스는 자식 프로세스가 종료될때까지 대기 */
}