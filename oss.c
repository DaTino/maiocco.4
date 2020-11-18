/*
Alberto Maiocco
CS4760 Project 4
11//2020
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
#include "queue.h"


FILE* outfile;

typedef struct msgBuffer {
  long mtype;
  //char msgData[32]; //<- this needs to change to time slice
  int timeSlice;
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

pcbType newPCB(shareClock sysClock, int simPID);

int main(int argc, char *argv[]) {

  int maxProc = 18;
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

  //gonna need two bits of shared memory now, for pcb and shareclock
  int scSMid;
  key_t scSMkey = 1337;
  shareClock* scSM; // <- this should be the shareClock, right? or its the shared int?
  //create shared mem segment...
  if ((scSMid = shmget(scSMkey, sizeof(shareClock), IPC_CREAT | 0666)) < 0) {
    perror("oss: error creating shareClock shared memory segment.");
    exit(1);
  }
  //attach segment to dataspace...
  if ((scSM = shmat(scSMid, NULL, 0)) == (int*) -1) {
    perror("oss: error attaching shareClock memory.");
    exit(1);
  }
  //here we'll have to write to shared memory...
  (*scSM).secs = 0; //sec
  (*scSM).nano = 0; //nsec

  //start up our system clock, might not need this since errythang in shared mem
  shareClock sysClock;
  sysClock.secs = 0;
  sysClock.nano = 0;
  //start up clock for random times
  shareClock randClock;
  randClock.secs = 0;
  randClock.nano = 0;

  //*(shm+2) = 0; //shared int <- this mess will get replaced by simPid in PBC

  //shared mem for PCB...
  int pcbSMid;
  key_t pcbSMkey = 80085;
  shareClock* pcbSM;
  if ((pcbSMid = shmget(pcbSMkey, sizeof(shareClock), IPC_CREAT | 0666)) < 0) {
    perror("oss: error creating PCB shared memory segment.");
    exit(1);
  }
  if ((scSM = shmat(scSMid, NULL, 0)) == (int*) -1) {
    perror("oss: error attaching PCB shared memory.");
    exit(1);
  }

  //create message queue
  message mb;
  mb.mtype = 1;
  //strcpy(mb.msgData, "Please work..."); //<- lol, print this mess out everywhere
  mb.timeSlice = 1000; //for now, this will get set later
  int msqid;
  key_t msgKey = 612;
  if ((msqid = msgget(msgKey, 0666 | IPC_CREAT)) == -1) {
    perror("oss: error creating message queue.");
    exit(1);
  }

  //make a single PCB...
  pcbType pcb;

  //Using the interupt handlers...
  // alarm for max time and ctrl-c
  signal(SIGALRM, interruptHandler);
  signal(SIGINT, interruptHandler);
  alarm(3);

  //set up our basic round robin queue for 1 pcb, and go from there...
  queueType *rrq = createQueue(maxProc);

  int maxTimeBetweenNewProcsNS;
  int maxTimeBetweenNewProcsSecs;

  //main loop with crit sec parts
  pid_t childpid = 0;
  int status = 0;
  int pid = 0;
  int total = 0;
  int proc_count = 0;
  int nsec = 1000000;

  unsigned int sysSecs;
  unsigned int sysNano;

  int cpuWorkTimeConstant = 10000;
  //main looperino right here!
  //while (total < 100 && (*scSM).secs < maxSecs) {
  while (1) {

    //set the current time/share time after every looperino
    sysSecs = (*scSM).secs; //sec
    sysNano = (*scSM).nano; //nsec
    if (sysNano > 1e9) {
      sysSecs += sysNano/1e9;
      sysNano -= 1e9;
    }
    (*scSM).secs = sysSecs; //sec
    (*scSM).nano = sysNano; //nsec
    sysClock.secs = sysSecs;
    sysClock.nano = sysNano;

    //need to determine when/how to make child- what I had before about total proc
    //check time requirement and proc requirement
    if (((sysClock.secs*1e9) + sysClock.nano) < ((randClock.secs*1e9)+randClock.nano) || (total < maxProc)) {

      //pcb for child
      pcbType initPCB = newPCB(sysClock, 1); //starting with 1 for the simPID
      printf("yo dog PCB wif simPID %d and priority %d\n", initPCB.simPID, initPCB.priority);

      //so 0 priority goonna go into rrq, and anything higher than that
      //gonna be in mlfq, based on timeslice. implement l8r, sk8r.
      if (initPCB.priority == 0) {
        printf("Adding %d into RRQ\n", initPCB.simPID);
        enqueue(rrq, initPCB.simPID);
      }
      //kk, so kids's set up, gonna have to fix this fork part to send messages
      //correctly
      if((childpid = fork()) < 0) {
        perror("./oss: ...it was a stillbirth.");
        if (msgctl(msqid, IPC_RMID, NULL) == -1) {
             perror("oss: msgctl failed to kill the queue");
             exit(1);
         }
         shmctl(scSMid, IPC_RMID, NULL);
        exit(1);
      } else if (childpid == 0) {
        (*scSM).nano += nsec;
        //*(scSM+1) += nsec;
        if (nsec > 1e9) {
          (*scSM).secs += nsec/1e9;
          (*scSM).nano -= 1e9;
          // *(scSM+0)+=nsec/1e9;
          // *(scSM+1)-=1e9;
        }
        //printf("oss: Creating new child pid %d at my time %d.%d\n", getpid(), total_sec, nsec_part);
        fprintf(outfile,"oss: Creating new child pid %d at my time %d.%d\n", getpid(), *(scSM+0), *(scSM+1));
        char simpidstring[16], msgidstring[16];
        sprintf(simpidstring, "%d", initPCB.simPID);
        sprintf(msgidstring, "%d", msqid);
        char *args[]={"./user", simpidstring, msgidstring, NULL};
        execvp(args[0], args);
      } else {
        total++;
        proc_count++;
      }
    }
    //ok so what.... check the queue. Since this is the last queue, we
    //pull the sucker off, send the message to the waiting proc,
    //and let 'er rip.
    //gonna have to change a whole bunch of code here for that....
    else if (!isEmpty(rrq)) {
      //get the simpid and priority to tell proc what to do...
      int rrqSimPid = dequeue(rrq);
      //int rrqPriority = initPCB.priority; <- now this is out of scope ;(
      int rrqPriority = 0;


      //need a better way to send messages.
      //well damn procs aint gon run if they aint get message!
      mb.mtype = rrqSimPid;
      //so if we change up priority, timeslice is 2 to priority times wtv our quantum is
      mb.timeSlice = 10000 * pow(2.0, rrqPriority);
      //NOW we send got the info to send the message!
      if (msgsnd(msqid, &mb, sizeof(mb.timeSlice), 0) == -1) {
        perror("oss: Message failed to send.");
        exit(1);
      }
    }
    //THIS MESS FOR TOO MANY PROCS. don't think its needed so much.
    //WAIT YES IT IS NEED A WAY TO CHECK FOR DEAD KIDS
    //don't want to destroy shm too fast, so we wait for child to finish.
    // if(proc_count >= maxProc) {
    //   do {
    //     if (*(shm+2) != 0) {
    //       printf("killed %d\n", *(shm+2));
    //       fprintf(outfile, "oss: Child pid %d terminated at system clock time %d.%d\n", *(shm+2), *(scSM+0), *(scSM+1));
    //       proc_count--;
	  //     }
    //   } while(*(shm+2) == 0);
    //   *(shm+2) = 0;
    // }

    //check exit condish
    if (total == 0 || total > 18) break;

 }


  printf("total: %d, time: %d.%d\n", total, *(scSM+0), *(scSM+1));
  //de-tach and de-stroy shm..
  printf("And we're back! shm contains %ds and %dns.\n", *(scSM+0), *(scSM+1));
  //detach shared mem
  shmdt((void*) scSM);
  //delete shared mem
  shmctl(scSMid, IPC_RMID, NULL);
  //printf("shm has left us for Sto'Vo'Kor\n");
  if (msgctl(msqid, IPC_RMID, NULL) == -1) {
       perror("oss: msgctl failed to kill the queue");
       exit(1);
   }

  printf("fin.\n");
  return 0;
}

static void interruptHandler() {
  key_t scSMkey = 1337;
  shareClock* scSM;
  int scSMid;
  if ((scSMid = shmget(scSMkey, sizeof(shareClock), IPC_CREAT | 0666)) < 0) {
    perror("oss: error created shared memory segment.");
    exit(1);
  }
  if ((scSM = shmat(scSMid, NULL, 0)) == (shareClock*) -1) {
    perror("oss: error attaching shared memory.");
    exit(1);
  }

  key_t pcbSMkey = 80085;
  pcbType* pcbSM;
  int pcbSMid;
  if ((pcbSMid = shmget(pcbSMkey, sizeof(pcbType), IPC_CREAT | 0666)) < 0) {
    perror("oss: error created shared memory segment.");
    exit(1);
  }
  if ((pcbSM = shmat(pcbSMid, NULL, 0)) == (pcbType*) -1) {
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
  fprintf(outfile, "Interrupt in yo face @ %ds, %dns\n", *(scSM+0), *(scSM+1));
  fclose(outfile);
  //cleanup shm...
  shmctl(scSMid, IPC_RMID, NULL);
  shmctl(pcbSMid, IPC_RMID, NULL);
  //cleanup shared MEMORY
  if (msgctl(msqid, IPC_RMID, NULL) == -1) {
       perror("oss: msgctl failed to kill the queue");
       exit(1);
   }
  //eliminate any witnesses...
  kill(0, SIGKILL);
  exit(0);
}

pcbType newPCB(shareClock sysClock, int simPID) {
  pcbType pcb;
  pcb.totalCPUTime.secs = 0;
  pcb.totalCPUTime.nano = 0;
  pcb.totalSystemTime.secs = 0;
  pcb.totalSystemTime.nano = 0;
  pcb.timeUsedLastBurst.secs = 0;
  pcb.timeUsedLastBurst.nano = 0;
  pcb.simPID = simPID;

  //start by simulating real time class, highest priority
  pcb.priority = 0;
  //I'll add the randorinos later, gator.

  return pcb;

}
