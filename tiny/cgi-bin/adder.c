/*
 * adder.c - a minimal CGI program that adds two numbers together
 */
/* $begin adder */
#include "csapp.h"

int main(void) {
  char *buf, *p, *method; // homework 11.11 HEAD 메소드 추가
  char arg1[MAXLINE], arg2[MAXLINE], content[MAXLINE];
  int n1 = 0, n2 = 0;

  /* Extract the two arguments */
  if ((buf = getenv("QUERY_STRING")) != NULL) { // getenv 함수는 환경변수 값을 읽어온다. 앞서 serve_dynamic에서 setenv 함수로 값이 설정됨. 교재 919p
    p = strchr(buf, '&');
    *p = '\0';
    sscanf(buf, "first=%d", &n1);  // homework 11-10
    sscanf(p+1, "second=%d", &n2); //

    // strcpy(arg1, buf);
    // strcpy(arg2, p+1);    
    // n1 = atoi(arg1);
    // n2 = atoi(arg2);
  }
  // printf("buf: %s\r\n", buf); // 이런 저런 출력 시도의 흔적
  method = getenv("REQUEST_METHOD"); // homework 11.11 HEAD 메소드 추가

  /* Make the response body */
  sprintf(content, "QUERY_STRING=%s", buf);
  sprintf(content, "Welcome to add.com: ");
  sprintf(content, "%sTHE Internet addition portal.\r\n<p>", content);
  sprintf(content, "%sThe answer is: %d + %d = %d\r\n<p>", content, n1, n2, n1 + n2);
  sprintf(content, "%sThanks for visiting!\r\n", content);

  /* Generate the HTTP response */
  printf("Connection : close\r\n");
  printf("Content-length : %d\r\n", (int)strlen(content));
  printf("Content-type: text/html\r\n\r\n");
  // printf("abcde"); // 이런 쓸데없는 거 추가하면 buf 저장 문장을 쓸데없는 길이만큼 못읽는다. buf size만큼 읽어와달라고 하기 때문에
  if (strcasecmp(method, "HEAD") != 0) // homework 11.11 HEAD 메소드 추가
    printf("%s", content);             // HEAD 메소드가 아닐 때만 body 출력
  
  fflush(stdout);
  
  exit(0);
}

/* $end adder */
