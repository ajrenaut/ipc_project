/* Author: Alex Renaut
 * CWID : 11783089
 *
 * Package reads in two input matrices and creates threads that divide the
 * matrix into subtasks to be sent to the message queue for completion. After
 * each thread sends its message, it waits for a message containing completed
 * results which are used to build the resulting matrix.
 */

#define _GNU_SOURCE
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/msg.h>
#include <sys/ipc.h>
#include <unistd.h>
#include <semaphore.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>
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

// Struct used to pass in all the data a thread will need to package up subtasks
typedef struct ThreadParam{
	int **mat1;
	int **mat2;
	int rowNum;
	int colNum;
	int vecLength;
	int threadNum;
	int msgid;
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

// createMatrix reads in matrix data from a file, and creates a 2D array based
// on that data to be returned to the caller. Pointers for the rows and columns
// are used to allow the function to return that information as well.
int **createMatrix(FILE *inFile, int *matRows, int *matCols) {
	FILE *readFile = inFile;
	int numRows, numCols;

	fscanf(readFile, " %d", &numRows);
	fscanf(readFile, " %d", &numCols);

	*matRows = numRows;
	*matCols = numCols;

	int **matrix = (int **)malloc(numRows * sizeof(int*));
	for (int i = 0; i < numRows; i++) {
		matrix[i] = (int*)malloc(numCols * sizeof(int));
	}

	for (int i = 0; i < numRows; i++) {
		for (int j = 0; j < numCols; j++) {
			fscanf(readFile, " %d", &matrix[i][j]);
		}
	}
	return matrix;
}

// Frees a matrix made by createMatrix().
void freeMatrix(int **matrix, int rows) {
	for (int i = 0; i < rows; i++) {
		free(matrix[i]);
	}
	free(matrix);
	return;
}

// Function that is passed to the threads.
void *packageDotProduct(void *arg) {
	ThreadParam *threadInfo = (ThreadParam*)arg;

	Msg *msgToSend = (Msg*)malloc(sizeof(Msg));
	msgToSend->type = 1;
	msgToSend->jobid = threadInfo->threadNum;

	msgToSend->rowvec = threadInfo->rowNum;
	msgToSend->colvec = threadInfo->colNum;
	msgToSend->innerDim = threadInfo->vecLength;

	for (int i = 0; i < threadInfo->vecLength; i++) {
		msgToSend->data[i] = threadInfo->mat1[threadInfo->rowNum][i];
	}
	for (int i = 0; i < threadInfo->vecLength; i++) {
		msgToSend->data[i+threadInfo->vecLength]
			= threadInfo->mat2[i][threadInfo->colNum];
	}
	/* Starting message size should be twice the length of the vector
	* ex: To store two vectors of length 10, the length of the data
	* array will need to be 20 integers, plus 4 integers for the other
	* message fields.
	*/
	int msgSize = ((threadInfo->vecLength)*2*(int)sizeof(int));
	msgSize += (int)((sizeof(msgToSend->rowvec)
		+ sizeof(msgToSend->colvec)
		+ sizeof(msgToSend->innerDim)
		+ sizeof(msgToSend->jobid)));

	int rc = msgsnd(threadInfo->msgid, (void *)msgToSend, msgSize, 0);
	/* Continue to attempt to send the message until it is successful.
	 * A return code of 0 indicates a success. The programs are simple
	 * and controlled enough that any message queue failure will likely
	 * be transitory.
	 */
	while (rc != 0) {
		key_t tmpKey;
		int tmpMsgid;
		tmpKey = ftok("ajrenaut", 42);
		tmpMsgid = msgget(tmpKey, 0666 | IPC_CREAT);
		rc = msgsnd(tmpMsgid, (void *)msgToSend, msgSize, 0);
	}

	printf("Sending job id %d type %ld size %d (rc=%d)\n",
		msgToSend->jobid, msgToSend->type,
		msgSize, rc);

	// Let compute know that a message is waiting, and safely increment
	// the jobs sent counter.
	sem_post(packageSem);
	pthread_mutex_lock(&sendCountLock);
	numJobsSent++;
	pthread_mutex_unlock(&sendCountLock);

	sem_wait(computeSem);
	Msg *receivedMsg = (Msg*)malloc(sizeof(Msg));
	int receivedMsgSize = msgrcv(threadInfo->msgid,
		receivedMsg, sizeof(Msg), 2, 0);
	printf("Receiving job id %d type %ld size %d\n",
		msgToSend->jobid, msgToSend->type, receivedMsgSize);
	pthread_mutex_lock(&receiveCountLock);
	numJobsReceived++;
	pthread_mutex_unlock(&receiveCountLock);
	free(threadInfo);
	free(msgToSend);
	return (void*)receivedMsg;
}

int main (int argc, char **argv) {
	signal(SIGINT, sigintHandler);
	int sleepTime = 0;
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

	// The fifth argument, if it exists, indicates the thread delay.
	if (argc == 5) {
		sleepTime = atoi(argv[4]);
	}

	FILE *matOneFile = fopen(argv[1], "r");
	FILE *matTwoFile = fopen(argv[2], "r");
	FILE *matOutFile = fopen(argv[3], "w");
	int *matOneRows = (int*)malloc(sizeof(int));
	int *matOneCols = (int*)malloc(sizeof(int));
	int *matTwoRows = (int*)malloc(sizeof(int));
	int *matTwoCols = (int*)malloc(sizeof(int));

	int **matrix1 = createMatrix(matOneFile, matOneRows, matOneCols);
	int **matrix2 = createMatrix(matTwoFile, matTwoRows, matTwoCols);

	// The number of threads will be equal to the number of indices in the
	// resultant matrix, as calculated below using the matrix 1 rows and
	// matrix 2 columns.
	int outMatrixRows = *matOneRows;
	int outMatrixCols = *matTwoCols;
	int vecLength = *matOneCols;
	pthread_t threads[outMatrixRows * outMatrixCols];
	pthread_attr_t attr;
	pthread_attr_init(&attr);
	pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_JOINABLE);

	key_t key;
	int msgid;

	key = ftok("ajrenaut", 42);
	msgid = msgget(key, 0666 | IPC_CREAT);

	int threadCount = 0;
	/* A thread parameter struct is passed to each thread with all of the
	* information it will need to package up its subtask into a message.
	*/
	for (int i = 0; i < outMatrixRows; i++) {
		for (int j = 0; j < outMatrixCols; j++) {
			ThreadParam *threadInfo =
				(ThreadParam*)malloc(sizeof(ThreadParam));
			threadInfo->mat1 = matrix1;
			threadInfo->mat2 = matrix2;
			threadInfo->rowNum = i;
			threadInfo->colNum = j;
			threadInfo->threadNum = threadCount;
			threadInfo->vecLength = vecLength;
			threadInfo->msgid = msgid;
			pthread_create(&threads[threadCount], &attr,
					packageDotProduct, (void *)threadInfo);
			threadCount++;
			sleep(sleepTime);
		}
	}

	void *receivedMsg = NULL;
	Msg *currentMsg = NULL;
	// For simplicity, the resulting matrix is stored in a 1D array.
	int *resultArray =(int*)malloc(outMatrixRows*outMatrixCols*sizeof(int));
	for (int i = 0; i < threadCount; i++) {
		pthread_join(threads[i], &receivedMsg);
		currentMsg = (Msg*)receivedMsg;
		resultArray[currentMsg->jobid] = currentMsg->data[0];
		free(currentMsg);
	}

	printf("\n");
	for (int i = 0; i < threadCount; i++) {
		fprintf(matOutFile, "%d ", resultArray[i]);
		printf("%d ", resultArray[i]);
	}
	printf("\n\n");

	freeMatrix(matrix1, *matOneRows);
	freeMatrix(matrix2, *matTwoRows);
	free(matOneRows);
	free(matOneCols);
	free(matTwoRows);
	free(matTwoCols);
	free(resultArray);

	return 0;
}
