#include "param.h"
#include "types.h"
#include "stat.h"
#include "user.h"
#include "fs.h"
#include "fcntl.h"
#include "syscall.h"
#include "traps.h"
#include "memlayout.h"

void test1();
void test2();
void test3();
void test4();
void customfunc();
int stdout = 1;

int
main(int argc, char *argv[])
{
  printf(1, "sanitytest starting\n");

  //run tests here
  test1();
  test2();
  test3();
  test4();

  exit();
}

void
test1(void)
{
  int pid;

  printf(stdout, "kill SIGSTOP test\n");

  if((pid = fork()) == 0) {
    sleep(10);
    printf(stdout, "SIGSTOP failed\n\n");
    exit();
  }
  printf(stdout, "sending stop signal\n");
  kill(pid, SIGSTOP);
  printf(stdout, "SIGSTOP ok\n\n");
}

void
test2(void)
{
  int pid;

  printf(stdout, "kill SIGCONT test\n");

  if((pid = fork()) == 0){
    //printf(stdout, "child running\n");
    sleep(30);
    exit();
  }
  printf(stdout, "sending stop signal\n");
  kill(pid, SIGSTOP);
  sleep(30);
  printf(stdout, "sending cont signal\n");
  kill(pid, SIGCONT);
  wait();
  printf(stdout, "SIGCONT ok\n\n");
}

void
test3(void)
{
  int pid;

  printf(stdout, "kill SIGKILL test\n");

  if((pid = fork()) == 0){
    //printf(stdout, "child running\n");
    sleep(50);
    printf(stdout, "SIGKILL failed\n\n");
    exit();
  }
  printf(stdout, "sending kill signal\n");
  kill(pid, SIGKILL);  
  wait();
  printf(stdout, "SIGKILL ok\n\n");
}

/*void
test4(void)
{
  int pid;

  printf(stdout, "kill custom SIGSTOP test\n");

  if((pid = fork()) == 0){
    signal(SIGSTOP, customfunc);
    sleep(50);
    //printf(stdout, "custom SIGSTOP failed\n\n");
    exit();
  }
  printf(stdout, "changing stop signal\n");
  //signal(SIGSTOP, customfunc);
  printf(stdout, "sending stop signal\n");
  kill(pid, SIGSTOP);
  wait();
  printf(stdout, "SIGSTOP ok\n\n");
}*/

void test4(){
  int pid = fork();

  if(pid == 0){
    signal(SIGSTOP,customfunc);
    printf(stdout, "Son is running\n");
    kill(getpid(), SIGSTOP);
    printf(stdout, "Son handled singal\n");
    sleep(50); 
    signal(SIGSTOP, (sighandler_t)SIG_DFL);
    exit();
  }

  else{
    wait();
    printf(1,"exit after wating for child\n");
    printf(1, "Test OK\n");

  }
}

void
customfunc(int x)
{
  printf(stdout, "in custom signal function\n");
}
