#include <errno.h>
#include <fcntl.h>
#include <libgen.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

typedef struct {
  char *command;
  pid_t pid;
} Job;

Job sus_jobs[100];
int num_jobs = 0;

void appendJob(char *input, pid_t pid) {
  sus_jobs[num_jobs].command = strdup(input);
  sus_jobs[num_jobs].pid = pid;
  num_jobs++;
}

void printJobs() {
  for (int i = 0; i < num_jobs; i++) {
    printf("[%d] %s\n", i + 1, sus_jobs[i].command);
  }
  fflush(stdout);
}

int system_sub(char *input, char *cwd) {
  signal(SIGINT, SIG_IGN);
  signal(SIGQUIT, SIG_IGN);
  signal(SIGTSTP, SIG_IGN);
  // copy input
  char *input_cp = strdup(input);
  // tokenize input
  char *command = strtok(input_cp, " ");
  char *args[100];
  int i = 0;
  while (command != NULL) {
    args[i] = command;
    // printf("Token %d: %s\n", i, args[i]);
    // fflush(stdout);
    i++;
    command = strtok(NULL, " ");
  }
  args[i] = NULL;

  // Build-in command
  if (strcmp(args[0], "cd") == 0) {
    if (i != 2) {
      fprintf(stderr, "Error: invalid command\n");
      return 1;
    } else if (chdir(args[1]) != 0) {
      fprintf(stderr, "Error: invalid directory\n");
      return 1;
    }
    return 0;
  } else if (strcmp(args[0], "jobs") == 0) {
    if (i != 1) {
      fprintf(stderr, "Error: invalid command\n");
      return 1;
    } else {
      printJobs();
      fflush(stdout);
      return 0;
    }
  } else if (strcmp(args[0], "exit") == 0) {
    if (i != 1) {
      fprintf(stderr, "Error: invalid command\n");
      return 1;
    } else if (num_jobs != 0) {
      fprintf(stderr, "Error: there are suspended jobs\n");
      return 1;
    } else {
      exit(0);
    }
  } else if (strcmp(args[0], "fg") == 0) {
    if (i != 2) {
      fprintf(stderr, "Error: invalid command\n");
      return 1;
    } else if (atoi(args[1]) > num_jobs || atoi(args[1]) <= 0) {
      fprintf(stderr, "Error: invalid job\n");
      return 1;
    } else {
      int index = atoi(args[1]) - 1;
          pid_t fgPid = sus_jobs[index].pid;
          char *fgJobName = strdup(sus_jobs[index].command); // duplicate the job name

          // Remove the job from the list as it's now foregrounded
          free(sus_jobs[index].command);
          for (int j = index; j < num_jobs - 1; j++) {
              sus_jobs[j] = sus_jobs[j + 1];
          }
          num_jobs--;

          // Send SIGCONT signal to the job's process to resume it
          if (kill(fgPid, SIGCONT) == -1) {
              perror("Error sending SIGCONT");
              return 1;
          }

          int status;
          waitpid(fgPid, &status, WUNTRACED);
          if (WIFSTOPPED(status)) {
              // If the job gets suspended again, append it back to the end of the list
              appendJob(fgJobName, fgPid);
          }
          free(fgJobName);
          return 0;
      
    }
  }

  // search for output redirection symbol
  char *redirect_output = NULL;
  char *redirect_input = NULL;
  int append = 0;

  for (int j = 0; j < i; j++) {
    // check for output redirection
    if (strcmp(args[j], ">") == 0 || strcmp(args[j], ">>") == 0) {
      if (args[j + 1] == NULL) {
        fprintf(stderr, "Error: No output file specified\n");
        return 1;
      }
      redirect_output = args[j + 1];
      append = strcmp(args[j], ">>") == 0;
      args[j] = NULL;
      j++;
    } else if (strcmp(args[j], "<") == 0) {
      // check for input redirection
      if (args[j + 1] == NULL) {
        fprintf(stderr, "Error: No input file specified\n");
        return 1;
      }
      redirect_input = args[j + 1];
      args[j] = NULL;
    }
  }

  // child execution
  pid_t pid = fork();
  if (pid < 0) {
    exit(EXIT_FAILURE);
  } else if (pid == 0) {
    // redirect output
    if (redirect_output != NULL) {
      int open_f = O_WRONLY | O_CREAT;
      if (append) {
        open_f |= O_APPEND;
      } else {
        open_f |= O_TRUNC;
      }
      int file = open(redirect_output, open_f, 0666);
      if (file == -1) {
        perror("Error opening file for redirection");
        exit(EXIT_FAILURE);
      }
      dup2(file, STDOUT_FILENO);
      close(file);
    }

    if (redirect_input != NULL) {
      int file = open(redirect_input, O_RDONLY);
      if (file == -1) {
        fprintf(stderr, "Error: invalid file\n");
        exit(EXIT_FAILURE);
      }
      dup2(file, STDIN_FILENO);
      close(file);
    }

    // Detecting pipes and storing their locations
    int pipe_indices[1000];
    int pipe_count = 0;
    for (int j = 0; j < i; j++) {
        if (strcmp(args[j], "|") == 0) {
            pipe_indices[pipe_count] = j;
            pipe_count++;
        }
    }

    // If there are pipes
    if (pipe_count > 0) {
        int fd[2];
        int start_idx = 0;

        for (int j = 0; j < pipe_count; j++) {
            pipe(fd);
            if (!fork()) {
                dup2(fd[1], STDOUT_FILENO);
                close(fd[0]);
                close(fd[1]);
                args[pipe_indices[j]] = NULL;
                if (execvp(args[start_idx], &args[start_idx]) == -1) {
                    fprintf(stderr, "Error: invalid program\n");
                    exit(1);
                }
            } else {
                wait(NULL);
                dup2(fd[0], STDIN_FILENO);
                close(fd[1]);
                start_idx = pipe_indices[j] + 1;
            }
        }

        if (execvp(args[start_idx], &args[start_idx]) == -1) {
            fprintf(stderr, "Error: invalid program\n");
        }
        return 1; // return an arbitrary non-zero value to indicate pipeline execution
    }else{







    

    // getting full path
    char *program;
    char path[1001];
    if (args[0][0] == '/') {
      program = args[0];
    } else if (strstr(args[0], "/")) {
      snprintf(path, sizeof(path), "%s/%s", cwd, args[0]);
      program = path;
    } else {
      snprintf(path, sizeof(path), "/usr/bin/%s", args[0]);
      program = path;
    }

    // execute
    execv(program, args);
    if (errno == ENOENT) {
      fprintf(stderr, "Error: invalid program\n");
    } else {
      fprintf(stderr, "Error: Invalid command\n");
    }
    }
  } else {
    int sus;
    waitpid(-1, &sus, WUNTRACED);
    if (WIFSTOPPED(sus)) {
      appendJob(input, pid);
      fflush(stdout);
    }
  }
  return 0;
}

void prompt() {
  while (1) {
    char *cwd;
    char buf[1024];
    size_t len = 1001;
    char *input = malloc(len);
    ssize_t char_read;

    // get current directory
    cwd = getcwd(buf, sizeof(buf));

    // solving current directory be null;

    // print prompt
    printf("[nyush %s]$ ", basename(cwd));
    fflush(stdout);

    // get user input
    char_read = getline(&input, &len, stdin);
    // null input
    if (char_read == -1) {
      free(input);
      exit(0);
    } else if (char_read == 1) {
      free(input);
      continue;
    }
    input[char_read - 1] = '\0';
    system_sub(input, cwd);
    free(input);
  }
}

int main(void) {

  prompt();
  return 0;
}
