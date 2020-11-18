#include <stdio.h>
#include <stdlib.h>
#include <sys/ipc.h>
#include <sys/shm.h>
#include <time.h>

//to refactor later, put struct defs in a header
struct msgBuffer {
  long mtype;
  int timeSlice;
};

typedef struct shareClock{
  int secs;
  int nano;
}shareClock;

int main(int argc, char *argv[]){

  //ok, lets redo this mess-
  //we're gonna get the time slice from the message,
  //we're gonna update the clock in shared mem
  //then oss will take it from there after child finishes.

  // time_t t;
  // srand((unsigned) time(&t));
  // int nsec = rand() % 1000000;

  int shmid;
  key_t key = 1337;
  int* shm;

  shareClock userClock;
  unsigned int userNano;
  unsigned int userSecs;

  //get info from exec [1] <- simPid, [2] <- msqID, controlling user info from scheduling
  int simPID = atoi(argv[1]);
  //msqID is determined by scheduler
  //cuz with mutha fucking messages I can determine which process to send via id!!!
  int msqid =  atoi(argv[2]);

  //get at that shared mammory...
  if ((shmid = shmget(key, sizeof(shareClock), IPC_CREAT | 0666)) < 0) {
    perror("user: error created shared memory segment.");
    exit(1);
  }
  //attachit...
  if ((shm = shmat(shmid, NULL, 0)) == (int*) -1) {
    perror("user: error attaching shared memory.");
    exit(1);
  }
  //set user's time based on vals in shared mem, doing this to do time rounding mess
  userSecs = *(shm+0);
  userNano = (*shm+1);
  //update based on rounding
  if (userNano > 1e9) {
    userSecs += userNano/1e9;
    userNano -= 1e9;
  }
  //update and post back for oss clock, then we use user clock to show user time
  *(shm+0) = userSecs;
  *(shm+1) = userNano;

  //user clock update
  userClock.secs = userSecs;
  userClock.nano = userNano;

  //so everything is set to grab the message
  //message queue mess down here
  struct msgBuffer mb;
  key_t msgKey = 612;
  if ((msqid = msgget(msgKey, 0666 | IPC_CREAT)) == -1) {
    perror("user: error creating message queue.");
    exit(1);
  }
  if (msgrcv(msqid, &mb, sizeof(mb.timeSlice), (simPID+1), 0) <= -1) {
    perror("user: Message failed to send.");
    exit(1);
  }

  printf("Yo gramps! Kid %d here @ %ds %dns\n", getpid(), userClock.secs, userClock.nano);
  printf("I got a msg in my pants for ya: %d type %d timeSlice\n", mb.mtype, mb.timeSlice);
  //So this is the critical section I believe.

  //old mess with weird crit sec
  //message handling going to be done by oss scheduling
  //all we should do here is recieve to correct message.
  // while (1) {
  //   //Entrance criteria: must have a message.
  //   if (msgrcv(msqid, &mb, sizeof(int), 0, 0)) {
  //       //doing stuff in the critical section.
  //       printf("Child %d got message!\n", getpid());
  //       //shared memory mess here
  //
  //
  //
  //       //add duration values from arguments...
  //       *(shm+1) += nsec;
  //       if (nsec > 1e9) {
  //         *(shm+0)+=nsec/1e9;
  //         *(shm+1)-=1e9;
  //       }
  //       // printf("and the magic numbers from shm are...\n");
  //       // printf("%d and %d. We happy?\n", *(shm+0), *(shm+1));
  //
  //       struct timespec tim, tim2;
  //       tim.tv_sec = 0;
  //       tim.tv_nsec = nsec;
  //
  //       //sleep given time, update shared int, send message to queue, and die.
  //       if(nanosleep(&tim, &tim2) < 0) {
  //         perror("user: oh no! the baby woke up! jk sleep broke.");
  //         exit(1);
  //       }
  //       //while ((double)(end_t=clock()-start_t)/CLOCKS_PER_SEC )
  //       //update shared int...
  //       *(shm+2) = getpid();
  //       break;
  //   }
  // }
  // if (msgsnd(msqid, &mb, sizeof(int), 0) == -1) {
  //   perror("user: Message failed to send.");
  //   exit(1);
  // }

  //so what we're gonna do here in user-
  //we wait to receive a message that we can open the file.
  //when we get the ok, we write to log, close file,
  //and send a message back to oss that we good, then terminate.

  //detach shared mem
  shmdt((void*) shm);
  //delete shared mem
  //shmctl(shmid, IPC_RMID, NULL);

  //whats the diff between return and exit?
  exit(0);
}
