#include <stdio.h>
#include <stdlib.h>
#include <sys/ipc.h>
#include <sys/shm.h>
#include <time.h>

struct msgBuffer {
  long mtype;
  int msgData;
};

int main(int argc, char *argv[]){

  // use these instead of that nanosecond guff?
  // clock_t start_t, end_t;
  // start_t = clock();


  //message queue mess down here
  struct msgBuffer mb;
  int msqid;
  key_t msgKey = 612;

  if ((msqid = msgget(msgKey, 0666 | IPC_CREAT)) == -1) {
    perror("user: error creating message queue.");
    exit(1);
  }

  time_t t;
  srand((unsigned) time(&t));
  int nsec = rand() % 1000000;

  int shmid;
  key_t key = 1337;
  int* shm;

  //So this is the critical section I believe.

  while (1) {
    //Entrance criteria: must have a message.
    if (msgrcv(msqid, &mb, sizeof(int), 0, 0)) {
        //doing stuff in the critical section.
        printf("Child %d got message!\n", getpid());
        //shared memory mess here

        //get at that shared mammory...
        if ((shmid = shmget(key, 2*sizeof(int), IPC_CREAT | 0666)) < 0) {
          perror("user: error created shared memory segment.");
          exit(1);
        }
        //attachit...
        if ((shm = shmat(shmid, NULL, 0)) == (int*) -1) {
          perror("user: error attaching shared memory.");
          exit(1);
        }

        //add duration values from arguments...
        *(shm+1) += nsec;
        if (nsec > 1e9) {
          *(shm+0)+=nsec/1e9;
          *(shm+1)-=1e9;
        }
        // printf("and the magic numbers from shm are...\n");
        // printf("%d and %d. We happy?\n", *(shm+0), *(shm+1));

        struct timespec tim, tim2;
        tim.tv_sec = 0;
        tim.tv_nsec = nsec;

        //sleep given time, update shared int, send message to queue, and die.
        if(nanosleep(&tim, &tim2) < 0) {
          perror("user: oh no! the baby woke up! jk sleep broke.");
          exit(1);
        }
        //while ((double)(end_t=clock()-start_t)/CLOCKS_PER_SEC )
        //update shared int...
        *(shm+2) = getpid();
        break;
    }
  }
  if (msgsnd(msqid, &mb, sizeof(int), 0) == -1) {
    perror("user: Message failed to send.");
    exit(1);
  }

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
