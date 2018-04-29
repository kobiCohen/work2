#include "types.h"
#include "stat.h"
#include "user.h"
#include "param.h"


void 
sigHandler1(int signum)
{
	printf(1, "In sigHandler1: Recieved signal number: %d, pid: %d, returning...\n", signum , getpid());
	return;
}
// one child, uses SIGSTOP on itself with the original handler for sigstop, and then waits for the father
// process to wake him up, and exits.
// NOTE: out handler for SIGSTOP is using yield() until the flag for SIGCONT is up, so it's not even about a handler
// 		 for SIGCONT, it's more about whether the flag was sent.
void 
test1(){
	printf(1, "========== Test1: ==========\n");
	signal(SIGCONT, sigHandler1);

	int pid1 = fork();
	if(pid1 == 0){
		printf(1,"In child process, pid: %d\n",getpid());
		kill(getpid(), SIGSTOP);
		exit();
	} else{
		sleep(50); // get enough time for the child process to handle SIGSTOP.
		printf(1,"In father process, pid: %d\n",getpid());

		kill(pid1, SIGCONT);
		wait();
	}
	printf(1, "========== Test1: ==========\n\n");
}

void 
sigHandler2(int signum)
{
	printf(1, "In sigHandler2: Recieved signal number: %d, pid: %d, returning...\n", signum , getpid());
	return;
}
// Still one child, but now we override the SIGKILL with the handler, and then we stop, continue and exit.
void 
test2(){
	printf(1, "========== Test2: ==========\n");
	signal(SIGKILL, sigHandler2);

	int pid1 = fork();
	if(pid1 == 0){
		printf(1,"In child process, pid: %d\n",getpid());
		kill(getpid(), SIGKILL);

		kill(getpid(), SIGSTOP);

		exit();
	} else{
		sleep(100); // get enough time for the child process to handle SIGSTOP.
		printf(1,"In father process, pid: %d\n",getpid());

		kill(pid1, SIGCONT);
		wait();
	}
	printf(1, "========== Test2: ==========\n\n");
}

void 
sigHandler3(int signum)
{
	printf(1, "In sigHandler3: Recieved signal number: %d, pid: %d, returning...\n", signum , getpid());
	return;
}
// 10 childs, will use SIGSTOP and SIGCONT on each.
void 
test3(){
	printf(1, "========== Test3: ==========\n");
	signal(SIGCONT, sigHandler3);

	int childs[10] = {0};
	int currpid;

	for(int i = 0; i < 10; i++){
		currpid = fork();
		if(currpid != 0){
			childs[i] = currpid; // parent keeping the child's pid.
		}else{
			break;
		}
	}

	if(currpid == 0){
		sleep(25);
		printf(1,"In child process, pid: %d\n",getpid());		
		kill(getpid() , SIGSTOP); // each child will get here and signal itself.
		exit();
	} 

	// wait in the parent process for the 10 children.
	for(int i = 0; i < 10; i++){
		sleep(75);
		kill(childs[i] , SIGCONT);
		wait();
	}
	
	printf(1, "========== Test3: ==========\n\n");
}

void 
sigHandler41(int signum)
{
	printf(1, "In sigHandler4-1: Recieved signal number: %d, pid: %d, returning...\n", signum , getpid());
	return;
}

void 
sigHandler42(int signum)
{
	printf(1, "In sigHandler4-2: Recieved signal number: %d, pid: %d, returning...\n", signum , getpid());
	return;
}
// 50 children.
void 
test4(){
	printf(1, "========== Test4: ==========\n");
	signal(SIGCONT, sigHandler41);
	signal(SIGKILL, sigHandler42);

	int childs[50] = {0};
	int currpid;

	for(int i = 0; i < 50; i++){
		currpid = fork();
		if(currpid != 0){
			childs[i] = currpid; // parent keeping the child's pid.
		}else{
			if(i%2 !=0){
				sleep(15);
				// printf(1,"In child process, pid: %d\n",getpid());	
				kill(getpid(), SIGSTOP); // each child will get here and signal itself.
				exit();
			}else{
				sleep(15);
				// printf(1,"In child process, pid: %d\n",getpid());	
				kill(getpid(), SIGKILL);	
				exit();		
			}	
		}
	}

	// wait in the parent process for the 10 children.
	for(int i = 0; i < 50; i++){
		if(i%2 != 0){
			// printf(1,"In parent process, i = %d\n",i);			
			sleep(75);
			kill(childs[i] , SIGCONT);
		}
		wait();
	}
	
	printf(1, "========== Test4: ==========\n\n");
}


// SNIR 2.4 BIT NOTE: in xv6 you MUST explicitly exit() since no one handles it!
int main(void){
	printf(1, "==================== SANITY TESTS - BEGIN ====================\n\n");

	test1();
	test2();
	test3();
	test4();
		

	printf(1, "==================== SANITY TESTS - END ====================\n");
	// Exit main process:
	exit();
}