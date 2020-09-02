/* Author: Alex Renaut
 * CWID : 11783089
 *
 * Compute generates a pool of threads specified via command line argument
 * which receive jobs from the message queue. Upon completing each calculation,
 * the results are sent back to the queue. This program runs indefinitely and
 * must be terminated with Ctrl-\
 */


#define _GNU_SOURCE
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/msg.h>
#include <sys/ipc.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <stdbool.h>
#include <string.h>
#include <semaphore.h>
#include <signal.h>
#include <errno.h>
#include <string.h>

/* Two semaphores are used, one to track messages sent by package.c to be
 * received by compute.c, and a second to track messages sent by compute.c
 * to be received by package.c.
 */
#define PACKAGE_SEM "/packageSem"
#define COMPUTE_SEM "/computeSem"
sem_t *packageSem = NULL;
sem_t *computeSem = NULL;

/* Global variables are used for the jobs sent and received counters, as the
 * signal handlers cannot receive parameters.
 */
pthread_mutex_t sendCountLock = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t receiveCountLock = PTHREAD_MUTEX_INITIALIZER;
int numJobsSent = 0;
int numJobsReceived = 0;

typedef struct QueueMessage{
        long type;
        int jobid;
        int rowvec;
        int colvec;
        int innerDim;
        int data[100];
} Msg;

typedef struct ThreadParam{
	int msgid;
        bool sendCalculations;
} ThreadParam;

/* Code from:
 * https://www.geeksforgeeks.org/
 * write-a-c-program-that-doesnt-terminate-when-ctrlc-is-pressed/
 */
void sigintHandler(int sig_num) {
	signal(SIGINT, sigintHandler);
	printf("Jobs sent %d | Jobs received %d\n",
                numJobsSent, numJobsReceived);
}

void *compute(void *arg) {
        ThreadParam *threadInfo = (ThreadParam*)arg;
        signal(SIGINT, sigintHandler);
        int msgid = threadInfo->msgid;
        bool sendCalculations = threadInfo->sendCalculations;
        free(threadInfo);
        while (1) {
                sem_wait(packageSem);
                int sum = 0;
                Msg *receivedMsg = (Msg*)malloc(sizeof(Msg));
                int receivedMsgSize = msgrcv(msgid, receivedMsg,
                        sizeof(Msg), 1, 0);
                printf("Receiving job id %d type %ld size %d\n",
                        receivedMsg->jobid, receivedMsg->type, receivedMsgSize);
                pthread_mutex_lock(&receiveCountLock);
                numJobsReceived++;
                pthread_mutex_unlock(&receiveCountLock);
                for (int i = 0; i < receivedMsg->innerDim; i++) {
                        sum += receivedMsg->data[i]
                                * receivedMsg->data[i+receivedMsg->innerDim];
                }

                printf("Sum for cell %d,%d is %d\n",
                        receivedMsg->rowvec, receivedMsg->colvec, sum);
                // The messages sent by compute always consist of 5 integers.
                int msgSize = sizeof(int) * 5;
                Msg *msgToReceive = (Msg*)malloc(sizeof(Msg));
                msgToReceive->type = 2;
                msgToReceive->jobid = receivedMsg->jobid;
                msgToReceive->rowvec = receivedMsg->rowvec;
                msgToReceive->colvec = receivedMsg->colvec;
                msgToReceive->innerDim = receivedMsg->innerDim;
                msgToReceive->data[0] = sum;

                // If the -n flag is specified then compute will not send
                // any completed jobs back to package.
                if (sendCalculations) {
                        int rc = msgsnd(msgid, (void*)msgToReceive, msgSize, 0);
                        // Attempt to send the message until it is successful.
                        // A return code of 0 indicates success.
                        while (rc != 0) {
                                key_t tmpKey;
                                int tmpMsgid;
                                tmpKey = ftok("ajrenaut", 42);
                                tmpMsgid = msgget(tmpKey, 0666 | IPC_CREAT);
                                rc = msgsnd(tmpMsgid, (void*)msgToReceive,
                                            msgSize, 0);
                        }
                        printf("Sending job id %d type %ld size %d (rc=%d)\n",
                        msgToReceive->jobid, msgToReceive->type, msgSize, rc);
                        sem_post(computeSem);
                        pthread_mutex_lock(&sendCountLock);
                        numJobsSent++;
                        pthread_mutex_unlock(&sendCountLock);
                        free(receivedMsg);
                }
        }
}

int main(int argc, char **argv) {
        int numThreads = atoi(argv[1]);
        bool sendCalculations = true;
        packageSem = sem_open(PACKAGE_SEM, O_CREAT, 0644, 0);
        if (packageSem == SEM_FAILED) {
                perror("Semaphore failed to open");
                return -1;
        }
	computeSem = sem_open(COMPUTE_SEM, O_CREAT, 0644, 0);
        if (computeSem == SEM_FAILED) {
                perror("Semaphore failed to open");
                return -1;
        }

        if (argc==3) {
                if (strcmp(argv[2], "-n") == 0) sendCalculations = false;
        }

        pthread_t threads[numThreads];
        pthread_attr_t attr;
        pthread_attr_init(&attr);
        pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_JOINABLE);

        key_t key;
        int msgid;
        key = ftok("ajrenaut", 42);
        msgid = msgget(key, 0666 | IPC_CREAT);
        for (int i = 0; i < numThreads; i++) {
                ThreadParam *threadInfo =
                        (ThreadParam*)malloc(sizeof(ThreadParam));
                threadInfo->msgid = msgid;
                threadInfo->sendCalculations = sendCalculations;
                pthread_create(&threads[i], &attr, compute, (void *)threadInfo);
        }

        // Although the threads will never terminate on their own, calling a
        // join on them is one way to prevent the main thread from terminating
        // early.
        for (int i = 0; i < numThreads; i++) {
                pthread_join(threads[i], NULL);
        }
}
