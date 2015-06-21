/**********************************************************************************
*
*  Change List:
*     1. HW1 - task2 - include the current working directory in the prompt
*     2. HW1 - task3 - add cd command
*     3. HW1 - task4 - exec external program
*     4. HW1 - task5 - path resolution
*     5. HW1 - task6 - process bookkeeping
*     6. HW1 - task7 - signal handling
*  
***********************************************************************************/

#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include <stdlib.h>
#include <errno.h>
#include <unistd.h>
#include <termios.h>
#include <fcntl.h>
#include <assert.h>
#include <signal.h>
#include <sys/stat.h>
#include <sys/wait.h>

#define FALSE 0
#define TRUE 1
#define INPUT_STRING_SIZE  80
#define MAX_FILE_SIZE 1024

#include "io.h"
#include "parse.h"
#include "process.h"
#include "shell.h"

process* find_process_by_pid(pid_t pid);
void add_process(process* p);


int cmd_quit(tok_t arg[]) {
  printf("Bye\n");
  exit(0);
  return 1;
}

int cmd_cd(tok_t arg[]) {
  if(chdir(arg[0]) == -1) {
	fprintf(stdout, "Can not access directory %s\n", arg[0]);
	return -1;		
  }

  return 1;
}


int cmd_exec(tok_t arg[]) {
   int wstatus;
   int pid;
   char pathname[MAX_FILE_SIZE+1];

   if(arg == NULL || arg[0] == NULL) {
	perror("cmd_exec: invalide argument\n");
	return -1;
   }

   // create a process bookkeeping
   process* cur_process = (process*)malloc(sizeof(process));
   // cur_process->argc = argc;
   cur_process->argv = arg;
   cur_process->stdin = cur_process->stdout = -1;
   cur_process->tmodes = shell_tmodes;
   (cur_process->tmodes).c_lflag |= (IEXTEN | ISIG | ICANON ); 
   cur_process->background = 1; // foreground process by default

   if(path_resolution(arg[0], pathname, MAX_FILE_SIZE) != 0) 
      strncpy(pathname, arg[0], MAX_FILE_SIZE); 

   // printf("exec file: %s\n", filename);
   if(io_redirection(cur_process, arg) != 0) {
      fprintf(stderr, "Failed to process input/output redirection\n");
      exit(-1);
   }


   add_process(cur_process);

   pid = fork();

   // child process
   if(pid == 0) {
   	cur_process->pid = getpid();

        launch_process(cur_process);
	if(execv(pathname, arg) == -1) { 
		fprintf(stderr, "Failed to exec: %s\n", arg[0]);
                exit(-2);
	}
   }
   else if(pid < 0) {
	fprintf(stderr, "Failed to exec: %s\n", arg[0]);
  	return -1;
   }
   // parent process
   else {
	// sync pgid info in parent child
	cur_process->pid = pid;
	setpgid(pid, pid);

	if(!cur_process->background) {
		put_process_in_foreground(cur_process, 0);
	}
	else {
		put_process_in_background(cur_process, 0);
	}
   }

   return 1;		
}

int cmd_help(tok_t arg[]);


// 06/16/2015 added by thinkhy
// first  arg[IN]:  arguments
// function: parse input/output redirection syntax 
// and replace stdin or stdout with specific file descriptor
// Note: FDs will not be closed on failures, because 
// we are in a child process, when this function fails, the child process
// would be terminated with all the IO resources released.
int io_redirection(process *p, tok_t arg[]) {
     int index = 1; // first argument is the program itself
     int outfd, infd;
     int ret;
     while(index < MAXTOKS && arg[index]) {
	switch(arg[index][0]) {
	    case '>':
		outfd = open(arg[++index], O_WRONLY|O_CREAT, S_IRWXU|S_IRWXG|S_IROTH);
		if(outfd == -1) {
		    fprintf(stderr, "Failed to open %s\n",arg[index]);
		    return -1;
		}
		// close(1);
		// ret = dup2(outfd, 1);
                // if(ret < 0) {
		//  fprintf(stderr, "Failed to invoke dup2 for stdout, return code is %d\n", ret);
		//    return -1;
	        //}
		p->stdout = outfd;
		arg[index-1] = arg[index] = NULL;
                break;
	    case '<':
		infd = open(arg[++index], O_RDONLY);
		if(infd == -1) {
		    fprintf(stderr, "Failed to open %s\n",arg[index]);
		    return -1;
		}
		// close(0);
		// ret = dup2(infd, 0);
                // if(ret < 0) {
		//     fprintf(stderr, "Failed to invoke dup2 for stdin, return code is %d\n", ret);
		//     return -1;
	        // }
		p->stdout = infd;
		arg[index-1] = arg[index] = NULL;
                break;
        }
	index++;
     }
     return 0;
}

// 06/15/2015 added by thinkhy
// first  arg[IN]:  executable file
// second arg[OUT]: valid path
int path_resolution(const char* filename, 
	            char pathname[], 
		    int size) {
    
   int pos;
   int i, j;
   char * cur_path; 
   
   if(filename == NULL ||filename[0] == '/' 
	|| filename[0] == '.') {
	return 1;	
   }

   // get PATH env var
   char * path_env = getenv("PATH");

   // for(cur_path = strtok(path_env, ":"); 
   //	cur_path != NULL; cur_path = strtok(NULL, ":")) {
   
   for(pos = 0; path_env[pos]; pos++) {
        for(i = 0; 
	     i < size && path_env[pos+i] && path_env[pos+i] != ':'; i++) 
			pathname[i] = path_env[pos+i];
	if(path_env[pos+i] == '\0')
		break;
	else
		pos += i;

	// strip the ending slash
	if(pathname[i-1] != '/') pathname[i++] = '/';

	// concatenate path and file 
	for(j = 0; i < size && filename[j]; i++, j++)
		 pathname[i] = filename[j];

	if(i < size) 
		pathname[i] = '\0';
	else {
		fprintf(stderr, "size: %d, i: %d\n", size, i);
		perror("path_resolution failed: pathname over bound");
	}

	// if file is exectualbe, return OK
	if(access(pathname, X_OK) == 0) {
		return 0;
        }
   }

   return 1; 
}


/* Command Lookup table */
typedef int cmd_fun_t (tok_t args[]); /* cmd functions take token array and return int */
typedef struct fun_desc {
  cmd_fun_t *fun;
  char *cmd;
  char *doc;
} fun_desc_t;

fun_desc_t cmd_table[] = {
  {cmd_help, "?", "show this help menu"},
  {cmd_quit, "quit", "quit the command shell"},
  {cmd_cd,   "cd",   "change current working directory"},
};

int cmd_help(tok_t arg[]) {
  int i;
  for (i=0; i < (sizeof(cmd_table)/sizeof(fun_desc_t)); i++) {
    printf("%s - %s\n",cmd_table[i].cmd, cmd_table[i].doc);
  }
  return 1;
}

int lookup(char cmd[]) {
  int i;
  for (i=0; i < (sizeof(cmd_table)/sizeof(fun_desc_t)); i++) {
    if (cmd && (strcmp(cmd_table[i].cmd, cmd) == 0)) return i;
  }
  return -1;
}

void init_shell()
{
  /* Check if we are running interactively */
  shell_terminal = STDIN_FILENO;

  /** Note that we cannot take control of the terminal if the shell
      is not interactive */
  shell_is_interactive = isatty(shell_terminal);

  if(shell_is_interactive){

    /* Force into foreground */
    while(tcgetpgrp (shell_terminal) != (shell_pgid = getpgrp()))
      kill( - shell_pgid, SIGTTIN);

    /* Ignore interactive and job-control signals. [150618 thinkhy] */
    signal(SIGINT, SIG_IGN);   
    signal(SIGQUIT, SIG_IGN);   
    signal(SIGTSTP, SIG_IGN);   
    signal(SIGTTIN, SIG_IGN);   
    signal(SIGTTOU, SIG_IGN);   
    
    // If parent process's SIGCHLD is ignored,   
    // waitpid will return -1 with errorno set to ECHILD
    // Refer to: http://linux.die.net/man/2/waitpid
    // Commented by thinkhy, 6/21/2015 
    //signal(SIGCHLD, SIG_IGN);   

    shell_pgid = getpid();
    /* Put shell in its own process group */
    if(setpgid(shell_pgid, shell_pgid) < 0){
      perror("Couldn't put the shell in its own process group");
      exit(1);
    }

    /* Take control of the terminal */
    tcsetpgrp(shell_terminal, shell_pgid);

    /* Save default terminal attributes for shell. */
    tcgetattr(shell_terminal, &shell_tmodes);

  }
	
  /** YOUR CODE HERE */
	
}

/**
 * Add a process to our process list
 */
void add_process(process* p)
{   
    assert(first_process != NULL);

    if(p == NULL) return;
    // 150617 added by thinkhy 
    p->next = first_process->next;
    if(first_process->next != NULL)
	    first_process->next->prev = p;
    p->prev = first_process;
    first_process->next = p;
}

/**
 * Remove a process from our process list
 */
void remove_process(process* p) {
    assert(first_process != NULL);
    assert(first_process != p);

    if(p == NULL) return;
    p->prev->next = p->next;
    if (p->next)
	    p->next->prev = p->prev;
}

/**
 * creates a process given the inputstring from stdin
 */
process* create_process(char* inputString)
{
  /** YOUR CODE HERE */
  return NULL;
}


/**
 * find a process by process ID
 * 150617 added by thinkhy
 */
process* find_process_by_pid(pid_t pid) {
  process *p = first_process;
  while(p && p->pid != pid) {
	p = p->next;
  }

  return p;
}


int shell (int argc, char *argv[]) {
  char *s = malloc(INPUT_STRING_SIZE+1);			/* user input string */
  char cwd[MAX_FILE_SIZE+1];
  tok_t *t;			/* tokens parsed from input */
  int lineNum = 0;
  int fundex = -1;
  pid_t pid = getpid();		/* get current processes PID */
  pid_t ppid = getppid();	/* get parents PID */
  pid_t cpid, tcpid, cpgid;


  init_shell();

  // create the head node and initialize it
  first_process = (process*)malloc(sizeof(process));
  first_process->pid = pid;
  first_process->status = 0;
  first_process->background = 0;
  first_process->tmodes = shell_tmodes;
  first_process->argc = argc;
  first_process->argv = argv;
  first_process->stdin = STDIN_FILENO;
  first_process->stdout = STDOUT_FILENO;
  first_process->stderr = STDERR_FILENO;
  first_process->next = first_process->prev = NULL;

  printf("%s running as PID %d under %d\n",argv[0],pid,ppid);

  getcwd(cwd, MAX_FILE_SIZE);
  lineNum=0;
  fprintf(stdout, "%s - %d: ", cwd, lineNum);
  while ((s = freadln(stdin))){
    t = getToks(s); /* break the line into tokens */
    fundex = lookup(t[0]); /* Is first token a shell literal */
    if(fundex >= 0) cmd_table[fundex].fun(&t[1]);
    else if(t && t[0]) {
       cmd_exec(&t[0]);
      /* fprintf(stdout, "This shell only supports built-ins. Replace this to run programs as commands.\n"); */
    }

    do_job_notification(0);
    getcwd(cwd, MAX_FILE_SIZE);
    fprintf(stdout, "%s - %d: ", cwd, lineNum);
  }

  return 0;
}


