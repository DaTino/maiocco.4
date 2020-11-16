/*
Alberto Maiocco
CS4760 Project 3
10/20/2020
*/

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <getopt.h>
#include <sys/ipc.h>
#include <sys/shm.h>
#include <sys/msg.h>
#include <sys/wait.h>
#include <sys/types.h>
#include <time.h>
#include <math.h>
#include <string.h>
#include "p4header.h"


FILE* outfile;

typedef struct msgBuffer {
  long mtype;
  char msgData[32];
}message;

static void interruptHandler();

//this struct might not be a great idea...
typedef struct shareClock{
  int secs;
  int nano;
}shareClock;

//kk make pcb...gonna try implementing share clock struct instead of
//straight up int pointers to shm
typedef struct PCB {
  shareClock totalCPUTime;
  shareClock totalSystemTime;
  shareClock timeUsedLastBurst;
  int simPID;
  int priority;
}pcbType;


int main(int argc, char *argv[]) {

  //struct timespec tstart={0,0}, tend={0,0};
  //clock_gettime(CLOCK_MONOTONIC, &tstart);

  int maxProc = 5;
  char* filename = "log.txt";
  int maxSecs = 20;

  int optionIndex;
  while ((optionIndex = getopt(argc, argv, "hc:l:t:")) != -1) {
    switch (optionIndex) {
      case 'h':
          printf("Welcome to the Valid Argument Usage Dimension\n");
          printf("- = - = - = - = - = - = - = - = - = - = - = - = -\n");
          printf("-h            : Display correct command line argument Usage\n");
          printf("-c <int>      : Indicate the maximum total of child processes spawned. (Default 5)\n");
          printf("-l <filename> : Indicate the number of children allowed to exist in the system at the same time. (Default 2)\n");
          printf("-t <int>      : The time in seconds after which the process will terminate, even if it has not finished. (Default 20)\n");
          printf("Usage: ./oss [-h | -c x -l filename -t z]\n");
          exit(0);
        break;

      case 'c':
        maxProc = atoi(optarg);
        if (maxProc <= 0) {
          perror("master: maxProc <= 0. Aborting.");
          exit(1);
        }
        if (maxProc > 20) maxProc = 20;
        break;

      case 'l':
        if (optarg == NULL) {
          optarg = "log.txt";
        }
        filename = optarg;
        break;

      case 't':
        maxSecs = atoi(optarg);
        if (maxSecs <= 0) {
          perror("master: maxSecs <= 0. Aborting.");
          exit(1);
        }
        break;

      case '?':
        if(isprint(optopt)) {
          fprintf(stderr, "Uknown option `-%c`.\n", optopt);
          perror("Error: Unknown option entered.");
          return 1;
        }
        else {
          fprintf (stderr, "Unkown option character `\\x%x`.\n", optopt);
          perror("Error: Unknown option character read.");
          return 1;
        }
        return 1;

      default:
        abort();

    }
  }
  printf("getopt test: -c: %d -l: %s -t: %d\n", maxProc, filename, maxSecs);

  //open log file for editing
  outfile = fopen(filename, "a+");
  if (!outfile) {
    perror("oss: error opening output log.");
    exit(1);
  }

  //next step: set up shared memory!
  //We'll do share clock with test values 612 and 6633
  int shmid;
  key_t key = 1337;
  int* shm; // <- this should be the shareClock, right? or its the shared int?
  //create shared mem segment...
  //sizeof int for seconds and nanoseconds... dont need a struct for clock?
  //added size to hold shmPID
  if ((shmid = shmget(key, 3*sizeof(int), IPC_CREAT | 0666)) < 0) {
    perror("oss: error created shared memory segment.");
    exit(1);
  }
  //attach segment to dataspace...
  if ((shm = shmat(shmid, NULL, 0)) == (int*) -1) {
    perror("oss: error attaching shared memory.");
    exit(1);
  }
  //here we'll have to write to shared memory...
  *(shm+0) = 0; //sec
  *(shm+1) = 0; //nsec
  *(shm+2) = 0; //shared int

  //create message queue
  message mb;
  mb.mtype = 1;
  strcpy(mb.msgData, "Please work...");
  int msqid;
  key_t msgKey = 612;

  if ((msqid = msgget(msgKey, 0666 | IPC_CREAT)) == -1) {
    perror("oss: error creating message queue.");
    exit(1);
  }

  //Using the interupt handlers...
  // alarm for max time and ctrl-c
  signal(SIGALRM, interruptHandler);
  signal(SIGINT, interruptHandler);
  alarm(2);

  //main loop with crit sec parts
  pid_t childpid = 0;
  int status = 0;
  int pid = 0;
  int total = 0;
  int proc_count = 0;
  int nsec = 1000000;
  //send the initial message to get everything going
  if (msgsnd(msqid, &mb, sizeof(mb.msgData), 0) == -1) {
    perror("oss: Message failed to send.");
    exit(1);
  }

  //main looperino right here!
  while (total < 100 && *(shm+0) < maxSecs) {
    if((childpid = fork()) < 0) {
      perror("./oss: ...it was a stillbirth.");
      if (msgctl(msqid, IPC_RMID, NULL) == -1) {
           perror("oss: msgctl failed to kill the queue");
           exit(1);
       }
       shmctl(shmid, IPC_RMID, NULL);
      exit(1);
    } else if (childpid == 0) {
      *(shm+1) += nsec;
      if (nsec > 1e9) {
        *(shm+0)+=nsec/1e9;
        *(shm+1)-=1e9;
      }
      //printf("oss: Creating new child pid %d at my time %d.%d\n", getpid(), total_sec, nsec_part);
      fprintf(outfile,"oss: Creating new child pid %d at my time %d.%d\n", getpid(), *(shm+0), *(shm+1));
      char *args[]={"./user", NULL};
      execvp(args[0], args);
    } else {
      total++;
      proc_count++;
    }

    //don't want to destroy shm too fast, so we wait for child to finish.
    if(proc_count >= maxProc) {
      do {
        if (*(shm+2) != 0) {
          printf("killed %d\n", *(shm+2));
          fprintf(outfile, "oss: Child pid %d terminated at system clock time %d.%d\n", *(shm+2), *(shm+0), *(shm+1));
          proc_count--;
	      }
      } while(*(shm+2) == 0);
      *(shm+2) = 0;
    }
 }


  printf("total: %d, time: %d.%d\n", total, *(shm+0), *(shm+1));
  //de-tach and de-stroy shm..
  printf("And we're back! shm contains %ds and %dns.\n", *(shm+0), *(shm+1));
  //detach shared mem
  shmdt((void*) shm);
  //delete shared mem
  shmctl(shmid, IPC_RMID, NULL);
  //printf("shm has left us for Sto'Vo'Kor\n");
  if (msgctl(msqid, IPC_RMID, NULL) == -1) {
       perror("oss: msgctl failed to kill the queue");
       exit(1);
   }

  printf("fin.\n");
  return 0;
}

static void interruptHandler() {
  key_t key = 1337;
  int* shm;
  int shmid;
  if ((shmid = shmget(key, 2*sizeof(int), IPC_CREAT | 0666)) < 0) {
    perror("oss: error created shared memory segment.");
    exit(1);
  }
  if ((shm = shmat(shmid, NULL, 0)) == (int*) -1) {
    perror("oss: error attaching shared memory.");
    exit(1);
  }

  int msqid;
  key_t msgKey = 612;

  if ((msqid = msgget(msgKey, 0666 | IPC_CREAT)) == -1) {
    perror("oss: error creating message queue.");
    exit(1);
  }

  //close file...
  fprintf(outfile, "Interrupt in yo face @ %ds, %dns\n", *(shm+0), *(shm+1));
  fclose(outfile);
  //cleanup shm...
  shmctl(shmid, IPC_RMID, NULL);
  //cleanup shared MEMORY
  if (msgctl(msqid, IPC_RMID, NULL) == -1) {
       perror("oss: msgctl failed to kill the queue");
       exit(1);
   }
  //eliminate any witnesses...
  kill(0, SIGKILL);
  exit(0);
}
