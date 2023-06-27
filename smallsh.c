// hicksa2_smallsh
#include <sys/wait.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <sys/types.h>
#include <signal.h>
#include <errno.h>
#include <fcntl.h>

// global variables
static sig_atomic_t gSignalStatus = 0;
volatile int storedPid;
volatile int smallStatus = 0;
volatile int statusExit = 1;
volatile int statusSignal = 0;


// function for setting status
void setExit(int statusNum, int exitVal)
{
  if(exitVal == 1)
  {
    statusExit = 1;
    statusSignal = 0;
  }
  else
  {
    statusExit = 0;
    statusSignal = 1;
  }
  smallStatus = statusNum;
}


// function for printing most recent status of foreground child
// if no foreground function have been run, returns default exit value 0
void printStatus(void)
{
  if(statusExit == 1)
  {
    printf("exit value %d\n", smallStatus);
  }
  else{
    printf("terminated by signal %d\n", smallStatus);
  }
}


// signal handler for catching background child processes
static void sigchldHandler(int sig)
{
  int status;
  pid_t childPid;
  while ((childPid = waitpid(-1, &status, WNOHANG)) > 0)
  {
    if(storedPid == childPid)
    {
      if(WIFSIGNALED(status))
      {
        printf("background pid %d is done: terminated by signal %d\n", childPid, WTERMSIG(status));
      }
      else
      {
        printf("\nBackground pid %d is done: exit value %d\n", childPid, status);
        printf(": ");
        storedPid = 0;
      }
    }
  }
  if(childPid == -1 && errno != ECHILD)
  {
    sleep(5);
  }
}


// signal handler for when ^C is entered in a foreground child process
void handle_SIGINT(int signo)
{
  char* message = "terminated by signal 2\n";
  write(STDOUT_FILENO, message, 24);
}


// signal handler for when ^Z is entered the first time
void handle_SIGTSTPin(int signo)
{
  char* message = "\nEntering foreground-only mode (& is now ignored)\n";
  write(STDOUT_FILENO, message, 50);
  char* newLine = ": ";
  write(STDOUT_FILENO, newLine, 3);
  gSignalStatus = 1;
}


// signal handler for when ^Z is entered a second time
void handle_SIGTSTPout(int signo)
{
  char* message = "\nExiting foreground-only mode\n";
  write(STDOUT_FILENO, message, 30);
  char* newLine = ": ";
  write(STDOUT_FILENO, newLine, 3);
  gSignalStatus = 0;
}


// function to count the number of "words" in command line by counting the number of spaces
int countWords(char commandStr[2048])
{
  int i = 0;
  int words = 1;
  while(commandStr[i] != '\0')
  {
    // if commandStr[i] == &, skip next space and do not count the symbol in word count
    if(commandStr[i] == '&')
    {
      words--;
      break;
    }
    if(commandStr[i] == ' ')
    {
      words++;
    }
    i++;
  }
  return words;
}


// function to see if there is an input or output file that needs to be read
// if isFile returns 0, there are no files
// if isFile returns 1, there is an input file
// if isFile returns 2, there is an output file
// if isFile returns 3, there is both an input and output file
int isFile(char commandStr[2048])
{
  int inputFile = 0;
  int outputFile = 0;
  int i = 0;
  while(commandStr[i] != '\0')
  {
    if(commandStr[i] == '<')
    {
      inputFile = 1;
    }
    if(commandStr[i] == '>')
    {
      outputFile = 2;
    }
    i++;
    }
  int fileCount = inputFile + outputFile;
  return fileCount;
}


// function to get files for redirection
// finds place where '<' or '>' is, truncates the original string, and returns the file name
char *getFile(char *src, const char *delim)
{
  char *p = strstr(src, delim);
  *p = '\0';
  return p + strlen(delim);
}


// function to replace instances of '$$' with parent pid
char* replace_pid(const char *src, const char *newW)
{
  char* result;
  int i, cnt = 0;
  const char* oldW = "$$";
  int oldWlen = strlen(oldW);
  int newWlen = strlen(newW);
  for (i = 0; src[i] != '\0'; i++)
  {
    if (strstr(&src[i], oldW) == &src[i])
    {
      cnt++;
      i += oldWlen - 1;
    }
  }
  result = (char*)malloc(i + cnt * (newWlen - oldWlen) + 1);
  i = 0;
  while (*src)
  {
    if(strstr(src, oldW) == src)
    {
      strcpy(&result[i], newW);
      i += newWlen;
      src += oldWlen;
    }
    else
      result[i++] = *src++;
  }
  result[i] = '\0';
  return result;
}


int main(void)
{
  char uinputbuf[2048]; //buffer array for user input
  char uinput[2048]; // user input once $$ has been parsed
  char userExit[] = "exit"; //exit term
  char hash[] = "#"; // to check against for comments
  char uempty[] = ""; // to check against for blank lines
  char userCommand[100]; // buffer for command
  pid_t pidVal = getpid(); // parent pid
  int targetFD; // output file descriptor
  int sourceFD; // input file descriptor
  int openSourceFD = 0;
  int openTargetFD = 0;
  int status = 0;
  signal(SIGCHLD, sigchldHandler);

  struct sigaction SIGINT_action, SIGTSTP_action, ignore_action;
  ignore_action.sa_handler = SIG_IGN;
  sigaction(SIGINT, &ignore_action, NULL);


  while(1)
  {
    // setting signal action for ^Z
    if(gSignalStatus == 0)
    {
      SIGTSTP_action.sa_handler = handle_SIGTSTPin;
      sigaction(SIGTSTP, &SIGTSTP_action, NULL);
    }
    else
    {
      SIGTSTP_action.sa_handler = handle_SIGTSTPout;
      sigaction(SIGTSTP, &SIGTSTP_action, NULL);
    }
    // closing any files that were left open before next loop
    if(openSourceFD == 1)
    {
      close(sourceFD);
      openSourceFD = 0;
    }
      if(openTargetFD == 1)
    {
      close(targetFD);
      openTargetFD = 0;
    }
    printf(": ");
    // will be set to 1 if & symbol is at end of command AND not in foreground only mode
    int inBackground = 0;
    fflush(stdout);

    // getting user input
    fgets(uinputbuf, 2048, stdin);

    // removing '\n' from the end of the user input so strcmp works correctly
    uinputbuf[strcspn(uinputbuf, "\n")] = '\0';

    // if user entered nothing, or something starting with #, ignored by program
    if(strcmp(uinputbuf, uempty) == 0)
    {
      continue;
    }
    else if(strncmp(uinputbuf, hash, 1) == 0)
    {
      continue;
    }

    // if user enters "exit" program exits while loop
    else if(strcmp(uinputbuf, userExit) == 0)
    {
      break;
    }
    else
    {
      // replacing all '$$' values with parent pid
      char pid[6];
      sprintf(pid, "%d", pidVal);
      char* result = NULL;
      result = replace_pid(uinputbuf, pid);
      strcpy(uinput, result);
      free(result);

      // checks to see if last character is '&'
      int len = strlen(uinput);
      const char *last_char = &uinput[len-1];
      if(strcmp(last_char, "&") == 0)
      {
        // if not in foreground only mode, sets inBackground to 1
        if(gSignalStatus == 0)
        {
        inBackground = 1;
        }
        else
        {
          inBackground = 0;
        }
        // remove ' &' from the end of the string
        uinput[strlen(uinput)-1] = '\0';
        uinput[strlen(uinput)-1] = '\0';
      }

      // gets first word as command
      sscanf(uinput, "%s", userCommand);
      // catches cd and status, only other built in commands besides exit
      if(strcmp(userCommand, "cd") == 0 || strcmp(userCommand, "status") == 0)
      {
        if(strcmp(userCommand, "cd") == 0)
        {
          // if user enters "cd" with no arguments, cd to HOME
          if(countWords(uinput) == 1)
          {
            printf("Changing working directory to %s\n", getenv("HOME"));
            fflush(stdout);
            chdir(getenv("HOME"));
            continue;
          }
          // if user enters "cd" with new directory path, cd to new directory
          else
          {
            char newDir[2048];
            sscanf(uinput, "%s %s", userCommand, newDir);
            printf("Changing working directory to %s\n", newDir);
            fflush(stdout);
            chdir(newDir);
            continue;
          }
        }
        // print exit status or terminating singnal of last foreground process run
        else
        {
          printStatus();
          fflush(stdout);
          continue;
        }
      }
      // executing other commands
      // checking to see if there are files to be processed
      if(isFile(uinput) > 0)
      {
        if(isFile(uinput) == 1)
        {
          // only input file
          const char *inArrow = " < ";
          char *inFile;
          inFile = getFile(uinput, inArrow);
          sourceFD = open(inFile, O_RDONLY);
          if(sourceFD == -1)
          {
            perror("source open()");
            setExit(1, 1);
            continue;
          }
          // setting openSourceFD to 1 so sourceFD will be closed on term of child/next loop
          openSourceFD = 1;
          // no idea if this does anything for my program
          // leaving it in just in case
          fcntl(sourceFD, F_SETFD, FD_CLOEXEC);
        }
        else if(isFile(uinput) == 2)
        {
          // only output file
          const char *outArrow = " > ";
          char *outFile;
          outFile = getFile(uinput, outArrow);
          targetFD = open(outFile, O_WRONLY | O_CREAT | O_TRUNC, 0644);
          if(targetFD == -1)
          {
            perror("target open()");
            setExit(1, 1);
            continue;
          }
          // setting openTargetFD to 1 so targetFD will be closed on term of child/next loop
          openTargetFD = 1;
          // no idea if this actually does anything for my program
          // leaving it in just in case
          fcntl(targetFD, F_SETFD, FD_CLOEXEC);

        }
        else
        {
          // both input and output files
          const char *outArrow= " > ";
          char *outFile;
          const char *inArrow = " < ";
          char *inFile;
          // arbitrarily finding infile first
          inFile = getFile(uinput, inArrow);
          // checking to see if " > " and the outfile is infile portion or uinput portion
          // if outFile is still in uinput
          if(isFile(uinput) == 2)
          {
            outFile=getFile(uinput, outArrow);
          }
          // else outFile is part of inFile
          else
          {
            outFile = getFile(inFile, outArrow);
          }
          // same thing as previous ifs but combined
          sourceFD = open(inFile, O_RDONLY);
          if(sourceFD == -1)
          {
            perror("source open()");
            setExit(1, 1);
            continue;
          }
          targetFD = open(outFile, O_WRONLY | O_CREAT | O_TRUNC, 0644);
          if(targetFD == -1)
          {
            perror("target open()");
            setExit(1, 1);
            continue;
          }
          // setting openSourceFD/openTargetFD to 1 so sourceFD/targetFD will be closed on term of child/next loop
          openSourceFD = 1;
          openTargetFD = 1;
        }
      }

      // set the length of the array of arguments equal to words entered + 1
      // all file values and delimiters have been removed
      int inputArgvLen = countWords(uinput)+ 1;
      // allocate space for uinputArgv
      int i = 0;
      char **uinputArgv;
      uinputArgv = malloc(inputArgvLen * sizeof(char*));
      for (int i = 0; i < inputArgvLen; i++)
      {
        uinputArgv[i] = malloc((100)*sizeof(char));
      }
      // with space allocated, parse through uinput to get args
      char * token = strtok(uinput, " ");
      while(token != NULL)
      {
         uinputArgv[i] = token;
         i++;
         token = strtok(NULL, " ");
      }
      uinputArgv[inputArgvLen - 1] = NULL;
      // gets first word for command
      char *cmd = userCommand;

      // beginning of forking
      int childStatus;

      // if child should be foreground process
      if (inBackground == 0)
      {
        pid_t spawnPid = fork();
        switch(spawnPid)
        {
          case -1:
            perror("fork() failed!");
            exit(1);
            break;
          case 0:
            //child process
            // setting signal handlers for foreground child
            SIGINT_action.sa_handler = handle_SIGINT;
            sigfillset(&SIGINT_action.sa_mask);
            SIGINT_action.sa_flags = 0;
            sigaction(SIGINT, &SIGINT_action, NULL);
            sigaction(SIGTSTP, &ignore_action, NULL);

            // dup2 files that need to be opened
            if(openSourceFD == 1)
            {
              int res = dup2(sourceFD, 0);
              if(res == -1)
              {
                perror("source dup2()");
                exit(2);
              }
            }
            if(openTargetFD == 1)
            {
              int res = dup2(targetFD, 1);
              if(res == -1)
              {
                perror("target dup2()");
                exit(2);
              }
            }
            // pass arguments to execvp
            if(execvp(cmd, uinputArgv) < 0)
            {
              perror("execvp error()");
              setExit(1, 1);
            }
            // close files
            if(openSourceFD == 1)
            {
              close(sourceFD);
              openSourceFD = 0;
            }
            if(openTargetFD == 1)
            {
              close(targetFD);
              openTargetFD = 0;
            }
            // free memory
            for(int k = 0; k < inputArgvLen; k++)
            {
              free(uinputArgv[k]);
            }
            _exit(EXIT_SUCCESS);
            break;
          default:
            // parent process
            // wait for child to terminate
            spawnPid = waitpid(spawnPid, &childStatus, 0);
            break;
        }
      }
      // if command ended in '&' child runs in the background
      else
      {
        pid_t childPid = fork();
        switch(childPid)
        {
          case -1:
            perror("fork() failed!");
            exit(1);
            break;
          case 0:
            // child process
            // dup2 files that need to be opened
            if(openSourceFD == 1)
            {
              int res = dup2(sourceFD, 0);
              if(res == -1)
              {
                perror("source dup2()");
                exit(2);
              }
            }
            if(openTargetFD == 1)
            {
              int res = dup2(targetFD, 1);
              if(res == -1)
              {
                perror("target dup2()");
                exit(2);
              }
            }
            // pass arguments to execvp
            execvp(cmd, uinputArgv);
            // free memory
            for(int k = 0; k < inputArgvLen; k++)
            {
              free(uinputArgv[k]);
            }
            _exit(EXIT_SUCCESS);
            break;
          default:
            // parent process
            // doesn't wait for child to terminate
            printf("background pid is %d\n", childPid);
            storedPid = childPid;
            childPid = waitpid(childPid, &childStatus, WNOHANG);
            fflush(stdout);
            break;
          }
      }
    }
  }
  // when user enters "exit", program exits while loop, kills all processes and terminates
  printf("Goodbye!\n");
  kill(0, SIGKILL);
  return 0;
}
