/* 
 * tsh - A tiny shell program with job control
 * 
 * Ryan Hoo - u1511875
 */
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <ctype.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <errno.h>

/* 
 * csapp.c - Functions for the CS:APP3e book
 *
 * Updated 10/2016 reb:
 *   - Fixed bug in sio_ltoa that didn't cover negative numbers
 *
 * Updated 2/2016 droh:
 *   - Updated open_clientfd and open_listenfd to fail more gracefully
 *
 * Updated 8/2014 droh: 
 *   - New versions of open_clientfd and open_listenfd are reentrant and
 *     protocol independent.
 *
 *   - Added protocol-independent inet_ntop and inet_pton functions. The
 *     inet_ntoa and inet_aton functions are obsolete.
 *
 * Updated 7/2014 droh:
 *   - Aded reentrant sio (signal-safe I/O) routines
 * 
 * Updated 4/2013 droh: 
 *   - rio_readlineb: fixed edge case bug
 *   - rio_readnb: removed redundant EINTR check
 */

ssize_t sio_puts(char s[]);
ssize_t sio_putl(long v);
static size_t sio_strlen(char s[]);
static void sio_ltoa(long v, char s[], int b);
static void sio_reverse(char s[]);

/* Misc manifest constants */
#define MAXLINE    1024   /* max line size */
#define MAXARGS     128   /* max args on a command line */
#define MAXJOBS      16   /* max jobs at any point in time */
#define MAXJID    1<<16   /* max job ID */

/* Job states */
#define UNDEF 0 /* undefined */
#define FG 1    /* running in foreground */
#define BG 2    /* running in background */
#define ST 3    /* stopped */

/* 
 * Jobs states: FG (foreground), BG (background), ST (stopped)
 * Job state transitions and enabling actions:
 *     FG -> ST  : ctrl-z
 *     ST -> FG  : fg command
 *     ST -> BG  : bg command
 *     BG -> FG  : fg command
 * At most 1 job can be in the FG state.
 */

/* Global variables */
extern char **environ;      /* defined in libc */
char prompt[] = "tsh> ";    /* command line prompt (DO NOT CHANGE) */
int verbose = 0;            /* if true, print additional output */
int nextjid = 1;            /* next job ID to allocate */
char sbuf[MAXLINE];         /* for composing sprintf messages */

struct job_t {              /* The job struct */
  pid_t pid;              /* job PID */
  int jid;                /* job ID [1, 2, ...] */
  int state;              /* UNDEF, BG, FG, or ST */
  char cmdline[MAXLINE];  /* command line */
};
struct job_t jobs[MAXJOBS]; /* The job list */
volatile sig_atomic_t pipe_pids[2] = {0, 0}; /* children in a foreground pipe */
/* End global variables */


/* Function prototypes */

/* Here are the functions that you will implement */
void eval(char *cmdline);
int builtin_cmd(char **argv);
void do_bg(int jid);
void do_fg(int jid);
void waitfg(pid_t pid);

void sigchld_handler(int sig);
void sigtstp_handler(int sig);
void sigint_handler(int sig);

/* Here are helper routines that we've provided for you */
int parseline(const char *cmdline, char **argv, int cmdnum); 
void sigquit_handler(int sig);

void clearjob(struct job_t *job);
void initjobs(struct job_t *jobs);
int maxjid(struct job_t *jobs); 
int addjob(struct job_t *jobs, pid_t pid, int state, char *cmdline);
int deletejob(struct job_t *jobs, pid_t pid); 
struct job_t *getjobpid(struct job_t *jobs, pid_t pid);
struct job_t *getjobjid(struct job_t *jobs, int jid); 
int pid2jid(pid_t pid); 
void listjobs(struct job_t *jobs);

void usage(void);
void unix_error(char *msg);
void app_error(char *msg);
typedef void handler_t(int);
handler_t *Signal(int signum, handler_t *handler);
void block_signals(sigset_t *oldmask);
void restore_signals(sigset_t *oldmask);
void signal_error(char *msg);
void run_command(char **argv, sigset_t *oldmask);

/*
 * main - The shell's main routine 
 */
int main(int argc, char **argv) 
{
  char c;
  char cmdline[MAXLINE];
  int emit_prompt = 1; /* emit prompt (default) */

  /* Redirect stderr to stdout (so that driver will get all output
   * on the pipe connected to stdout) */
  if (dup2(1, 2) < 0)
    unix_error("dup2 error");

  /* Parse the command line */
  while ((c = getopt(argc, argv, "hvp")) != EOF) {
    switch (c) {
    case 'h':             /* print help message */
      usage();
      break;
    case 'v':             /* emit additional diagnostic info */
      verbose = 1;
      break;
    case 'p':             /* don't print a prompt */
      emit_prompt = 0;  /* handy for automatic testing */
      break;
    default:
      usage();
    }
  }

  /* Install the signal handlers */

  /* These are the ones you will need to implement */
  Signal(SIGINT,  sigint_handler);   /* ctrl-c */
  Signal(SIGTSTP, sigtstp_handler);  /* ctrl-z */
  Signal(SIGCHLD, sigchld_handler);  /* Terminated or stopped child */

  /* This one provides a clean way to kill the shell */
  Signal(SIGQUIT, sigquit_handler); 

  /* Initialize the job list */
  initjobs(jobs);

  /* Execute the shell's read/eval loop */
  while (1) {

    /* Read command line */
    if (emit_prompt) {
      printf("%s", prompt);
      fflush(stdout);
    }
    if ((fgets(cmdline, MAXLINE, stdin) == NULL) && ferror(stdin))
      app_error("fgets error");
    if (feof(stdin)) { /* End of file (ctrl-d) */
      fflush(stdout);
      exit(0);
    }

    /* Evaluate the command line */
    eval(cmdline);
    fflush(stdout);
    fflush(stdout);
  } 

  exit(0); /* control never reaches here */
}
  
/* 
 * eval - Evaluate the command line that the user has just typed in
 * 
 * If the user has requested a built-in command (quit, jobs, bg or fg)
 * then execute it immediately. Otherwise, fork a child process and
 * run the job in the context of the child. If the job is running in
 * the foreground, wait for it to terminate and then return.  Note:
 * each child process must have a unique process group ID so that our
 * background children don't receive SIGINT (SIGTSTP) from the kernel
 * when we type ctrl-c (ctrl-z) at the keyboard.  
 */
void eval(char *cmdline) 
{
  char *argv1[MAXARGS]; /* argv for execve() */
  char *argv2[MAXARGS]; /* argv for second command execve() */
  int bg;               /* should the job run in bg or fg? */
  pid_t pid;
  sigset_t oldmask;
  int fd[2];
  int i;

  /* If the line contains two commands, split into two strings */
  char* cmd2 = strchr(cmdline, '|');
  
  if(cmd2 != NULL && strlen(cmd2) >= 3 && (cmd2 - cmdline) >= 2){
    // Terminate the first command with newline and null character
    cmd2--;
    cmd2[0] = '\n';
    cmd2[1] = '\0';
    // Set the second command to start after the next space
    cmd2 += 3;
  }

  /* Parse command line */
  bg = parseline(cmdline, argv1, 1); 
  if (argv1[0] == NULL)  
    return;   /* ignore empty lines */

  if(cmd2 != NULL)
    parseline(cmd2, argv2, 2);

  if (cmd2 == NULL && builtin_cmd(argv1))
    return;

  /* Keep a child from being reaped before we record its PID. */
  block_signals(&oldmask);
  if (cmd2 != NULL) {
    if (argv2[0] == NULL) {
      restore_signals(&oldmask);
      return;
    }
    if (pipe(fd) < 0)
      unix_error("pipe error");
    for (i = 0; i < 2; i++) {
      if ((pid = fork()) < 0)
        unix_error("fork error");
      if (pid == 0) {
        /* The first command writes to the second command's input. */
        if (dup2(fd[i == 0 ? 1 : 0], i == 0 ? STDOUT_FILENO : STDIN_FILENO) < 0)
          unix_error("dup2 error");
        if (close(fd[0]) < 0 || close(fd[1]) < 0)
          unix_error("close error");
        run_command(i == 0 ? argv1 : argv2, &oldmask);
      }
      pipe_pids[i] = pid;
    }
    /* Closing both ends here lets the reader see EOF. */
    if (close(fd[0]) < 0 || close(fd[1]) < 0)
      unix_error("close error");
    while (pipe_pids[0] != 0 || pipe_pids[1] != 0) {
      if (sigsuspend(&oldmask) < 0 && errno != EINTR)
        unix_error("sigsuspend error");
    }
    restore_signals(&oldmask);
    return;
  }

  if ((pid = fork()) < 0)
    unix_error("fork error");
  if (pid == 0)
    run_command(argv1, &oldmask);
  /* Also set the group in the parent before signals can be forwarded. */
  if (setpgid(pid, pid) < 0 && errno != EACCES && errno != ESRCH)
    unix_error("setpgid error");
  if (!addjob(jobs, pid, bg ? BG : FG, cmdline)) {
    if (kill(-pid, SIGKILL) < 0 && errno != ESRCH)
      unix_error("kill error");
    restore_signals(&oldmask);
    return;
  }
  if (bg) {
    printf("[%d] (%d) %s", pid2jid(pid), pid, cmdline);
    fflush(stdout);
  }
  restore_signals(&oldmask);
  if (!bg)
    waitfg(pid);

  return;
}

/* 
 * parseline - Parse the command line and build the argv array.
 * 
 * Characters enclosed in single quotes are treated as a single
 * argument.  Return true if the user has requested a BG job, false if
 * the user has requested a FG job.  
 */
int parseline(const char *cmdline, char **argv, int cmdnum) 
{
  static char array1[MAXLINE]; /* holds local copy of command line */
  static char array2[MAXLINE]; /* holds local copy of 2nd command line */
  char *buf;                   /* ptr that traverses command line */
  char *delim;                 /* points to first space delimiter */
  int argc;                    /* number of args */
  int bg;                      /* background job? */
  
  if(cmdnum == 1)
    buf = array1;
  else
    buf = array2;
  
  strcpy(buf, cmdline);
  
  buf[strlen(buf)-1] = ' ';  /* replace trailing '\n' with space */
  while (*buf && (*buf == ' ')) /* ignore leading spaces */
    buf++;
  
  /* Build the argv list */
  argc = 0;
  if (*buf == '\'') {
    buf++;
    delim = strchr(buf, '\'');
  }
  else {
    delim = strchr(buf, ' ');
  }
  
  while (delim) {
    argv[argc++] = buf;
    *delim = '\0';
    buf = delim + 1;
    while (*buf && (*buf == ' ')) /* ignore spaces */
      buf++;
    
    if (*buf == '\'') {
      buf++;
      delim = strchr(buf, '\'');
    }
    else {
      delim = strchr(buf, ' ');
    }
  }
  argv[argc] = NULL;
  
  if (argc == 0)  /* ignore blank line */
    return 1;
  
  /* should the job run in the background? */
  if ((bg = (*argv[argc-1] == '&')) != 0) {
    argv[--argc] = NULL;
  }
  
  
  return bg;
}

/* 
 * builtin_cmd - If the user has typed a built-in command then execute
 *    it immediately.  
 */
int builtin_cmd(char **argv) 
{
  char *cmd = argv[0];

  if (!strcmp(cmd, "quit"))
    exit(0);
  if (!strcmp(cmd, "jobs")) {
    sigset_t oldmask;
    block_signals(&oldmask);
    listjobs(jobs);
    fflush(stdout);
    restore_signals(&oldmask);
    return 1;
  }

  if (!strcmp(cmd, "bg") || !strcmp(cmd, "fg")) { /* bg and fg commands */
      
    int jid;

    /* Ignore command if no argument */
    if (argv[1] == NULL) {
      printf("%s command requires a %%jobid argument\n", argv[0]);
      return 1;
    }

    if (argv[1][0] == '%') {
      jid = atoi(&argv[1][1]);
    }
    else {
      printf("%s: argument must be a %%jobid\n", argv[0]);
      return 1;
    }
      
    if(!strcmp(cmd, "bg"))
      do_bg(jid);
    else
      do_fg(jid);
    return 1;
  }

  if (!strcmp(cmd, "&")) { /* Ignore singleton & */
    return 1;
  }

  return 0;     /* not a builtin command */
}

/* 
 * do_bg - Execute the builtin bg command
 */
void do_bg(int jid) 
{
  sigset_t oldmask;
  struct job_t *job;

  block_signals(&oldmask);
  job = getjobjid(jobs, jid);
  if (job == NULL)
    printf("%%%d: No such job\n", jid);
  else {
    if (kill(-job->pid, SIGCONT) < 0 && errno != ESRCH)
      unix_error("kill error");
    job->state = BG;
    printf("[%d] (%d) %s", job->jid, job->pid, job->cmdline);
  }
  fflush(stdout);
  restore_signals(&oldmask);
  return;
}

/* 
 * do_fg - Execute the builtin fg command
 */
void do_fg(int jid) 
{
  sigset_t oldmask;
  struct job_t *job;
  pid_t pid;

  block_signals(&oldmask);
  job = getjobjid(jobs, jid);
  if (job == NULL) {
    printf("%%%d: No such job\n", jid);
    fflush(stdout);
    restore_signals(&oldmask);
    return;
  }
  pid = job->pid;
  job->state = FG;
  if (kill(-pid, SIGCONT) < 0 && errno != ESRCH)
    unix_error("kill error");
  restore_signals(&oldmask);
  waitfg(pid);
  return;
}

/* 
 * waitfg - Block until process pid is no longer the foreground process
 */
void waitfg(pid_t pid)
{
  sigset_t oldmask;
  struct job_t *job;

  block_signals(&oldmask);
  /* Check and sleep with no gap where SIGCHLD could be missed. */
  while ((job = getjobpid(jobs, pid)) != NULL && job->state == FG) {
    if (sigsuspend(&oldmask) < 0 && errno != EINTR)
      unix_error("sigsuspend error");
  }
  restore_signals(&oldmask);
  return;
}

/*****************
 * Signal handlers
 *****************/

/* 
 * sigchld_handler - The kernel sends a SIGCHLD to the shell whenever
 *     a child job terminates (becomes a zombie), or stops because it
 *     received a SIGSTOP or SIGTSTP signal. The handler reaps all
 *     available zombie children, but doesn't wait for any other
 *     currently running children to terminate.  
 */
void sigchld_handler(int sig) 
{
  int saved_errno = errno;
  int status;
  pid_t pid;
  struct job_t *job;

  (void)sig;
  while (1) {
    pid = waitpid(-1, &status, WNOHANG | WUNTRACED);
    if (pid == 0)
      break;
    if (pid < 0) {
      if (errno == EINTR)
        continue;
      if (errno != ECHILD)
        signal_error("waitpid error\n");
      break;
    }
    if (WIFEXITED(status) || WIFSIGNALED(status)) {
      if (pid == pipe_pids[0])
        pipe_pids[0] = 0;
      if (pid == pipe_pids[1])
        pipe_pids[1] = 0;
    }
    job = getjobpid(jobs, pid);
    if (job == NULL)
      continue;
    if (WIFSTOPPED(status) || WIFSIGNALED(status)) {
      sio_puts("Job [");
      sio_putl(job->jid);
      sio_puts("] (");
      sio_putl(pid);
      if (WIFSTOPPED(status)) {
        job->state = ST;
        sio_puts(") stopped by signal ");
        sio_putl(WSTOPSIG(status));
      }
      else {
        sio_puts(") terminated by signal ");
        sio_putl(WTERMSIG(status));
      }
      sio_puts("\n");
    }
    if (WIFEXITED(status) || WIFSIGNALED(status))
      deletejob(jobs, pid);
  }
  errno = saved_errno;
  return;
}

/* 
 * sigint_handler - The kernel sends a SIGINT to the shell whenver the
 *    user types ctrl-c at the keyboard.  Catch it and send it along
 *    to the foreground job.  
 */
void sigint_handler(int sig) 
{
  int saved_errno = errno;
  int i;

  for (i = 0; i < MAXJOBS; i++) {
    if (jobs[i].pid != 0 && jobs[i].state == FG) {
      if (kill(-jobs[i].pid, sig) < 0 && errno != ESRCH)
        signal_error("kill error\n");
      break;
    }
  }
  errno = saved_errno;
  return;
}

/*
 * sigtstp_handler - The kernel sends a SIGTSTP to the shell whenever
 *     the user types ctrl-z at the keyboard. Catch it and suspend the
 *     foreground job by sending a SIGTSTP.  
 */
void sigtstp_handler(int sig) 
{
  /* Both handlers forward their signal to the foreground group. */
  sigint_handler(sig);
  return;
}

/* Block signals while reading or changing the shared job list. */
void block_signals(sigset_t *oldmask)
{
  sigset_t mask;
  if (sigfillset(&mask) < 0)
    signal_error("sigfillset error\n");
  if (sigprocmask(SIG_BLOCK, &mask, oldmask) < 0)
    signal_error("sigprocmask error\n");
}

void restore_signals(sigset_t *oldmask)
{
  if (sigprocmask(SIG_SETMASK, oldmask, NULL) < 0)
    signal_error("sigprocmask error\n");
}

/* This error path is safe to use inside a handler or a child. */
void signal_error(char *msg)
{
  sio_puts(msg);
  _exit(1);
}

void run_command(char **argv, sigset_t *oldmask)
{
  if (setpgid(0, 0) < 0)
    signal_error("setpgid error\n");
  Signal(SIGINT, SIG_DFL);
  Signal(SIGTSTP, SIG_DFL);
  Signal(SIGCHLD, SIG_DFL);
  Signal(SIGQUIT, SIG_DFL);
  restore_signals(oldmask);
  if (execve(argv[0], argv, environ) < 0) {
    sio_puts(argv[0]);
    sio_puts(": Command not found\n");
    _exit(1);
  }
}

/*********************
 * End signal handlers
 *********************/

/***********************************************
 * Helper routines that manipulate the job list
 **********************************************/

/* clearjob - Clear the entries in a job struct */
void clearjob(struct job_t *job) {
  job->pid = 0;
  job->jid = 0;
  job->state = UNDEF;
  job->cmdline[0] = '\0';
}

/* initjobs - Initialize the job list */
void initjobs(struct job_t *jobs) {
  int i;

  for (i = 0; i < MAXJOBS; i++)
    clearjob(&jobs[i]);
}

/* maxjid - Returns largest allocated job ID */
int maxjid(struct job_t *jobs) 
{
  int i, max=0;

  for (i = 0; i < MAXJOBS; i++)
    if (jobs[i].jid > max)
      max = jobs[i].jid;
  return max;
}

/* addjob - Add a job to the job list */
int addjob(struct job_t *jobs, pid_t pid, int state, char *cmdline) 
{
  int i;
    
  if (pid < 1)
    return 0;

  for (i = 0; i < MAXJOBS; i++) {
    if (jobs[i].pid == 0) {
      jobs[i].pid = pid;
      jobs[i].state = state;
      jobs[i].jid = nextjid++;
      if (nextjid > MAXJOBS)
	nextjid = 1;
      strcpy(jobs[i].cmdline, cmdline);
      if(verbose){
	printf("Added job [%d] %d %s\n", jobs[i].jid, jobs[i].pid, jobs[i].cmdline);
      }
      return 1;
    }
  }
  printf("Tried to create too many jobs\n");
  return 0;
}

/* deletejob - Delete a job whose PID=pid from the job list */
int deletejob(struct job_t *jobs, pid_t pid) 
{
  int i;

  if (pid < 1)
    return 0;

  for (i = 0; i < MAXJOBS; i++) {
    if (jobs[i].pid == pid) {
      clearjob(&jobs[i]);
      nextjid = maxjid(jobs)+1;
      return 1;
    }
  }
  return 0;
}


/* getjobpid  - Find a job (by PID) on the job list */
struct job_t *getjobpid(struct job_t *jobs, pid_t pid) {
  int i;

  if (pid < 1)
    return NULL;
  for (i = 0; i < MAXJOBS; i++)
    if (jobs[i].pid == pid)
      return &jobs[i];
  return NULL;
}

/* getjobjid  - Find a job (by JID) on the job list */
struct job_t *getjobjid(struct job_t *jobs, int jid) 
{
  int i;

  if (jid < 1)
    return NULL;
  for (i = 0; i < MAXJOBS; i++)
    if (jobs[i].jid == jid)
      return &jobs[i];
  return NULL;
}

/* pid2jid - Map process ID to job ID */
int pid2jid(pid_t pid) 
{
  int i;

  if (pid < 1)
    return 0;
  for (i = 0; i < MAXJOBS; i++)
    if (jobs[i].pid == pid) {
      return jobs[i].jid;
    }
  return 0;
}

/* listjobs - Print the job list */
void listjobs(struct job_t *jobs) 
{
  int i;
  
  for (i = 0; i < MAXJOBS; i++) {
    if (jobs[i].pid != 0) {
      printf("[%d] (%d) ", jobs[i].jid, jobs[i].pid);
      switch (jobs[i].state) {
      case BG: 
	printf("Running ");
	break;
      case FG: 
	printf("Foreground ");
	break;
      case ST: 
	printf("Stopped ");
	break;
      default:
	printf("listjobs: Internal error: job[%d].state=%d ", 
	       i, jobs[i].state);
      }
      printf("%s", jobs[i].cmdline);
    }
  }
}
/******************************
 * end job list helper routines
 ******************************/


/***********************
 * Other helper routines
 ***********************/

/*
 * usage - print a help message
 */
void usage(void) 
{
  printf("Usage: shell [-hvp]\n");
  printf("   -h   print this message\n");
  printf("   -v   print additional diagnostic information\n");
  printf("   -p   do not emit a command prompt\n");
  exit(1);
}

/*
 * unix_error - unix-style error routine
 */
void unix_error(char *msg)
{
  fprintf(stdout, "%s: %s\n", msg, strerror(errno));
  exit(1);
}

/*
 * app_error - application-style error routine
 */
void app_error(char *msg)
{
  fprintf(stdout, "%s\n", msg);
  exit(1);
}

/*
 * Signal - wrapper for the sigaction function
 */
handler_t *Signal(int signum, handler_t *handler) 
{
  struct sigaction action, old_action;

  action.sa_handler = handler;  
  /* Keep the job-control handlers from interrupting each other. */
  if (sigfillset(&action.sa_mask) < 0)
    unix_error("sigfillset error");
  action.sa_flags = SA_RESTART; /* restart syscalls if possible */

  if (sigaction(signum, &action, &old_action) < 0)
    unix_error("Signal error");
  return (old_action.sa_handler);
}

/*
 * sigquit_handler - The driver program can gracefully terminate the
 *    child shell by sending it a SIGQUIT signal.
 */
void sigquit_handler(int sig) 
{
  sio_puts("Terminating after receipt of SIGQUIT signal\n");
  _exit(1);
}


/* Put string */
ssize_t sio_puts(char s[])
{
  size_t len = sio_strlen(s);
  size_t total = 0;
  ssize_t n;

  while (total < len) {
    n = write(STDOUT_FILENO, s + total, len - total);
    if (n < 0 && errno == EINTR)
      continue;
    if (n <= 0)
      _exit(1);
    total += n;
  }
  return total;
}


/* Put long */
ssize_t sio_putl(long v)
{
  char s[128];
  sio_ltoa(v, s, 10); /* Based on K&R itoa() */
  return sio_puts(s);
}


/* sio_strlen - Return length of string (from K&R) */
static size_t sio_strlen(char s[])
{
  int i = 0;
  while (s[i] != '\0')
    ++i;
  return i;
}


/* sio_ltoa - Convert long to base b string (from K&R) */
static void sio_ltoa(long v, char s[], int b)
{
  int c, i = 0;
  int neg = v < 0;

  if (neg)
    v = -v;

  do {
    s[i++] = ((c = (v % b)) < 10)  ?  c + '0' : c - 10 + 'a';
  } while ((v /= b) > 0);

  if (neg)
    s[i++] = '-';

  s[i] = '\0';
  sio_reverse(s);
}

/* sio_reverse - Reverse a string (from K&R) */
static void sio_reverse(char s[])
{
  int c, i, j;

  for (i = 0, j = sio_strlen(s)-1; i < j; i++, j--) {
    c = s[i];
    s[i] = s[j];
    s[j] = c;
  }
}
