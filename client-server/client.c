/* includes */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#include <fcntl.h>
#include <semaphore.h>

#define MAXDIRPATH 1024
#define MAXKEYWORD 256

/**
 * @brief contains all data relating to shared memory queue
 *
 */
struct Queue {
    char *buffer;                   //data stored in queue
    unsigned short *overlap;        //points to number of characters left of overlap (if any)
    int back;                       //back of queue (point of next entry)
    int size;                       //size of queue (not entire shared memory space)
    sem_t *empty;
    sem_t *full;
    sem_t *mutex;
};

/**
 * @brief creates shared memory region for queue
 *
 * @param size - size of queue (in bytes)
 */ 
void *open_shared_memory(int size)
{
    int fd;
    void *ptr;

    fd = shm_open("queue", O_RDWR, 0666);
    if (fd == -1) {
        fprintf(stderr, "client: shared memory failed\n");
        exit(1);
    }

    ptr = mmap(0, size + 3, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (ptr == MAP_FAILED) {
        fprintf(stderr, "client: memory map failed\n");
        exit(1);
    }

    return ptr;
}

/**
 * @brief creates a queue on shared memory
 *
 * @param size - size of queue (in # requests)
 */
struct Queue open_queue(int size)
{
    int total_size, linesize;
    struct Queue q;

    linesize = MAXDIRPATH + 1 + MAXKEYWORD + 1;
    total_size = size * linesize;
    q.buffer = (char *) open_shared_memory(total_size);
    q.overlap = (unsigned short *) (q.buffer + total_size + 1);
    q.back = 0;
    q.size = total_size;
    q.empty = sem_open("/empty", 0);
    q.full = sem_open("/full", 0);
    q.mutex = sem_open("/mutex", 0);
    return q;
}

/**
 * @brief closes semaphores opened by queue
 *
 * @param q - queue to have semaphores closed
 */
void close_queue(struct Queue *q)
{
    sem_close(q->empty);
    sem_close(q->full);
    sem_close(q->mutex);
}

/**
 * @brief enqueues request string to shared memory queue
 *
 * @param request - string to be placed on queue
 * @param q - reference to queue
 */
void enqueue(char *request, struct Queue *q)
{
    unsigned short offset, reqlen;

    reqlen = strlen(request) + 1;                                               //add 1 because strlen() excludes null character

    sem_wait(q->empty);
    sem_wait(q->mutex);

    if (q->back + reqlen <= q->size)
        sprintf(&q->buffer[q->back], "%s", request);
    else {
        offset = q->size - q->back;
        snprintf(&q->buffer[q->back], offset + 1, "%s", request);
        sprintf(q->buffer, "%s", &request[offset]);
        *q->overlap = offset;
    }

    sem_post(q->mutex);
    sem_post(q->full);

    q->back = (q->back + reqlen) % q->size;
}

/**
 * @brief reads file provided by command line and sends contents to server
 *
 * @param inputfile - absolute path of the input file to be read
 * @param request_queue - reference to request_queue
 */
void read_inputfile(char *inputfile, struct Queue *request_queue)
{
    FILE *fs;
    const int bufflen = MAXDIRPATH + MAXKEYWORD + 2;
    char request[bufflen];
    char *context, *exclude;

    fs = fopen(inputfile, "r");
    context = NULL, exclude = "\n";

    while (fgets(request, bufflen, fs) != NULL) 
        enqueue(strtok_r(request, exclude, &context), request_queue);
}

int main(int argc, char **argv)
{
    if (argc != 3) {
        fprintf(stderr, "client usage: ./client <req-queue-size> <inputfile>\n");
        return 1;
    }
    
    char *inputfile = argv[2];
    int req_queue_size = atoi(argv[1]);
    struct Queue request_queue = open_queue(req_queue_size);
    read_inputfile(inputfile, &request_queue);
    close_queue(&request_queue);
    return 0;
}
