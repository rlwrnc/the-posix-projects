/* includes */
#include <dirent.h>
#include <errno.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <semaphore.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#include <fcntl.h>
#include <stdint.h>

#define MAXDIRPATH 1024
#define MAXKEYWORD 256
#define MAXLINESIZE 1024
#define MAXOUTSIZE 2048

sem_t buffer_empty;
sem_t buffer_full;
sem_t buffer_mutex;

int buffer_use = 0;
int buffer_filler = 0;
/**
 * @brief contains all data relating to shared memory queue
 *
 */
struct Queue {
    char *buffer;                   //data stored in queue
    unsigned short *overlap;        //points to number of characters left of overlap (if any)
    int front;                      //front of queue (data next served)
    int size;                       //size of queue (not entire shared memory space)
    sem_t *empty;
    sem_t *full;
    sem_t *mutex;
};

/**
 * @brief object defined for storing a filename, linenumber, and line text in a single variable.
 * 
 */
struct Item {
    char *filename;
    int linenumber;
    char line[MAXLINESIZE];
};

/**
 * @brief passed when a thread is created, has all the necessary information for worker & printer threads.
 * 
 */
struct ThreadArgs {
    char *filepath;
    char *filename;
    char *keyword;
    struct Item *buffer;
    int threadbuffersize;
    struct List *list;
    int filestream;
};

/**
 * @brief node containing the thread id and attribute of a thread.
 * 
 */
struct Node {
    struct Node *next;
    pthread_attr_t attr;
    pthread_t tid;
};

/**
 * @brief linked-list to keep track of number of threads opened and store thread information.
 * 
 */
struct List {
    struct Node *head;
    struct Node *tail;
    int threadcount;
    int overallthreadcount;
};

/**
 * @brief Creates the node
 * 
 * @return struct Node* 
 */
struct Node *create_node(void) {
    struct Node *node = malloc(sizeof(struct Node));
    if (node == NULL) {
        fprintf (stderr, "%s: Couldn't create memory for the node; %s\n", "linkedlist", strerror(errno));
        exit(-1);
    }
    node->tid = 0;
    node->next = NULL;
    return node;
}

/**
 * @brief Creates the linked-list
 * 
 * @return struct List* 
 */
struct List *create_list(void) {
    struct List *list = malloc(sizeof(struct List));
    if (list == NULL) {
       fprintf (stderr, "%s: Couldn't create memory for the list; %s\n", "linkedlist", strerror (errno));
       exit(-1);
    }
    list->head = NULL;
    list->tail = NULL;
    list->threadcount = 0;
    list->overallthreadcount = 0;
    return list;
}

/**
 * @brief adds new node to the end of a list.
 * 
 * @param node 
 * @param list 
 */
void insert_tail(struct Node *node, struct List *list) {
    if(list->head == NULL && list->tail == NULL) {
        list->head = node;
        list->tail = node;
        list->threadcount++;
        list->overallthreadcount++;
    } else {
        list->tail->next = node;
        list->tail = node;
        list->threadcount++;
        list->overallthreadcount++;
    }
}

/**
 * @brief destroys list and frees all memory allocation.
 * 
 * @param list 
 */
void destroy_list(struct List *list) {
  struct Node *ptr = list->head;
  struct Node *tmp;  
  while (ptr != NULL) {
    tmp = ptr;
    ptr = ptr->next;
    free(tmp);
  }
  free(list);
}

/**
 * @brief Create a thread args object to pass when a thread is created
 * 
 * @param filename 
 * @param keyword 
 * @param buffer 
 * @param threadbuffersize
 * @param list 
 * @return struct ThreadArgs* 
 */
struct ThreadArgs *create_thread_args(char *filepath, char *filename, int filestream, char *keyword, struct Item *buffer, int threadbuffersize, struct List *list)
{
    struct ThreadArgs *threadargs = malloc(sizeof(struct ThreadArgs));
    if (threadargs == NULL) {
        fprintf (stderr, "%s: Couldn't create memory for the thread arguments; %s\n", "linkedlist", strerror(errno));
        exit(-1);
    }
    threadargs->filepath = strdup(filepath);
    threadargs->filename = filename;
    threadargs->filestream = filestream;
    threadargs->keyword = strdup(keyword);
    threadargs->buffer = buffer;
    threadargs->threadbuffersize = threadbuffersize;
    threadargs->list = list;
    return threadargs;
}

/**
 * @brief adds an Item to the shared buffer
 * 
 * @param item - the item to be added to the buffer
 * @param threadbuffersize - information about the buffer size
 * @param buffer 
 */
void buffer_fill(struct Item item, int threadbuffersize, struct Item *buffer)
{
    buffer[buffer_filler] = item;
    buffer_filler++;
    if (buffer_filler == threadbuffersize) {
        buffer_filler = 0;
    }
}

/**
 * @brief helper function that returns an Item retrieved from the shared buffer
 * 
 * @param bufferInfo contains the buffer indices use and fill
 * @param buffer the shared buffer between threads
 * @return struct Item*
 */
struct Item buffer_get(int threadbuffersize, struct Item *buffer)
{
    struct Item tmp = buffer[buffer_use];
    buffer_use++;
    if(buffer_use == threadbuffersize) {
        buffer_use = 0;
    }
    return tmp;
}

/**
 * @brief Used by worker threads to find keywords in a file and add lines to the shared thread buffer.
 * 
 * @param ThreadArgs - contains requested keyword, filename, semaphores, buffer indices, and the buffer itself
 * @return void* - doesn't return anything, exits after adding dummy value to the shared buffer.
 */
void *retrieve_keyword(void* ThreadArgs)
{
   struct ThreadArgs *threadargs = (struct ThreadArgs *)ThreadArgs;

   FILE *fileptr = fopen(threadargs->filepath, "r");
   FILE *lineptr = fopen(threadargs->filepath, "r");

   char filebuffer[MAXLINESIZE], delim[] = " \t\n";
   char *token, *saveptr = filebuffer;
   int lastsavedline = 0, currentline = 0;

   while(fgets(filebuffer, 1024, fileptr) != NULL) {
        currentline++;
        token = strtok_r(filebuffer, delim, &saveptr);
        while(token != NULL) {
            if(strcmp(token, threadargs->keyword) == 0) {
                struct Item item;
                for(int i = lastsavedline; i < currentline; i++) {
                    fgets(item.line, 1024, lineptr);
                }
                lastsavedline = currentline;
                item.filename = threadargs->filename;
                item.linenumber = currentline;

                sem_wait(&buffer_empty);
                sem_wait(&buffer_mutex);
                buffer_fill(item, threadargs->threadbuffersize, threadargs->buffer);
                sem_post(&buffer_mutex);
                sem_post(&buffer_full);
                break;
            }
            else {
                token = strtok_r(NULL, delim, &saveptr);
            }
        }
   }

    struct Item dummyItem;
    dummyItem.linenumber = -1;
    sem_wait(&buffer_empty);
    sem_wait(&buffer_mutex);
    buffer_fill(dummyItem, threadargs->threadbuffersize, threadargs->buffer);
    sem_post(&buffer_mutex);
    sem_post(&buffer_full);

    fclose(fileptr);
    fclose(lineptr);

    free(((struct ThreadArgs *)ThreadArgs)->filepath);
    free(((struct ThreadArgs *)ThreadArgs)->keyword);
    free((struct ThreadArgs *)ThreadArgs);
    pthread_exit(0);
}

/**
 * @brief Used by printer thread, pulls an item out of the buffer and outputs it to file, 
 *        this will also wait for all worker threads to close
 * 
 * @param ThreadArgs - contains all necessary information for the printer thread. 
 * @return void* 
 */
void *print_buffer(void* ThreadArgs)
{
    struct ThreadArgs *threadargs = (struct ThreadArgs *)ThreadArgs;

    struct flock fl = {F_WRLCK, SEEK_END, 0, 0, 0};
    struct Item ptr;
    ptr.linenumber = 0;
    while(threadargs->list->threadcount != 1) {
        sem_wait(&buffer_full);
        sem_wait(&buffer_mutex);
        ptr = buffer_get(threadargs->threadbuffersize, threadargs->buffer);
        sem_post(&buffer_mutex);
        sem_post(&buffer_empty);

        if(ptr.linenumber != -1 && ptr.filename != NULL) {
            char *linebuff = malloc(MAXOUTSIZE);
            char linenumber[5];
            strcpy(linebuff, "");
            strcat(linebuff, ptr.filename);
            strcat(linebuff, ":");
            sprintf(linenumber, "%d", ptr.linenumber);
            strcat(linebuff, linenumber);
            strcat(linebuff, ":");
            strcat(linebuff, ptr.line);

            fl.l_type = F_SETLKW;
            fl.l_pid = getpid();
            fcntl(threadargs->filestream, F_SETLKW, &fl);
            write(threadargs->filestream, linebuff, strlen(linebuff));
            fl.l_type = F_UNLCK;
            fcntl(threadargs->filestream, F_SETLK, &fl);
            free(linebuff);
        }
        else {
            threadargs->list->threadcount--;
        }
    }
    threadargs->list->threadcount--;
    free(((struct ThreadArgs *)ThreadArgs)->filepath);
    free(((struct ThreadArgs *)ThreadArgs)->keyword);
    free((struct ThreadArgs *)ThreadArgs);
    pthread_exit(0);
}

/**
 * @brief searches through the base directory for a process, creating threads each time a file is found.
 * 
 * @param directory - directory path passed when process is handled
 * @param dir - directory stream to read from
 * @param dirent - struct used for directory operations
 * @param keyword - the requested keyword from the process
 * @param buffer - the shared buffer between threads
 * @param threadbuffersize - the size of the thread buffer
 * @param list - the linked-list of thread id's
 */
void search_directory(char* directory, DIR *dir, struct dirent *dirent, char* keyword, struct Item *buffer, int threadbuffersize, struct List *list, int filestream)
{
    while((dirent = readdir(dir)) != NULL) {
        struct stat statbuf;

        char filepath[strlen(directory) + strlen(dirent->d_name)];
        strcpy(filepath, directory);
        strcat(filepath, "/");
        strcat(filepath, dirent->d_name);
        stat(filepath, &statbuf);

        if(dirent->d_name[0] != '.') {
            if(S_ISREG(statbuf.st_mode)) {
                struct Node *newNode = create_node();
                struct ThreadArgs *threadargs = create_thread_args(filepath, dirent->d_name, filestream, keyword, buffer, threadbuffersize, list);
                pthread_attr_init(&newNode->attr);
                pthread_attr_setdetachstate(&newNode->attr, PTHREAD_CREATE_JOINABLE);
                pthread_create(&newNode->tid, &newNode->attr, retrieve_keyword, (void *) threadargs);
                insert_tail(newNode, list);
            }
        }
    }
    struct Node *printerNode = create_node();
    struct ThreadArgs *printerthreadargs = create_thread_args("dummy", "dummy", filestream, keyword, buffer, threadbuffersize, list);
    pthread_attr_init(&printerNode->attr);
    pthread_attr_setdetachstate(&printerNode->attr, PTHREAD_CREATE_JOINABLE);
    pthread_create(&printerNode->tid, &printerNode->attr, print_buffer, (void *) printerthreadargs);
    insert_tail(printerNode, list);
    closedir(dir);
}

/**
 * @brief function used right after process creation to begin handling the client request
 * 
 * @param request - request from client; contains directory_path and keyword delimited by space
 * @param buffer_size - buffer size defined by the client.
 */
void handle_client_request(char *request, int buffer_size, int filestream) {
   char *directory_path, *keyword, *context = NULL, *exclude = " ";

   directory_path = strtok_r(request, exclude, &context);
   keyword = strtok_r(NULL, exclude, &context);

   struct List *list = create_list();
   struct Item *buffer;
   buffer = malloc(sizeof(struct Item) * buffer_size);
   int threadbuffersize = buffer_size;
   sem_init(&buffer_empty, 0, buffer_size);
   sem_init(&buffer_full, 0, 0);
   sem_init(&buffer_mutex, 0, 1);

   DIR *dir = NULL;
   struct dirent *dirent = NULL;
   dir = opendir(directory_path);
   search_directory(directory_path, dir, dirent, keyword, buffer, threadbuffersize, list, filestream);

   while(list->threadcount != 0) {}
   struct Node *threadnode = list->head;
   for(int i = 0; i < list->overallthreadcount; i++) {
       pthread_join(threadnode->tid, NULL);
       threadnode = threadnode->next;
   }
   destroy_list(list);
   free(buffer);
}

/**
 * @brief creates shared memory region for queue
 *
 * @param size - size of queue (in bytes)
 */ 
void *create_shared_memory(int size)
{
    int fd;
    void *ptr;

    fd = shm_open("queue", O_CREAT | O_RDWR, 0666);
    ftruncate(fd, size + 1 + 2);                                                    //+1 for null character, +2 for overlap flag

    ptr = mmap(0, size + 3, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (ptr == MAP_FAILED) {
        fprintf(stderr, "server: memory map failed\n");
        exit(1);
    }

    return ptr;
}

/**
 * @brief creates a queue on shared memory
 *
 * @param size - size of queue (in # requests)
 */
struct Queue create_queue(int size)
{
    int total_size, line_size;
    struct Queue q;
    
    line_size = MAXDIRPATH + 1 + MAXKEYWORD + 1;                                    //+1 for space, +1 for null character
    total_size = size * line_size;
    q.buffer = (char *) create_shared_memory(total_size);
    q.overlap = (unsigned short *) (q.buffer + total_size + 1);                     //+1 to account for null character
    *q.overlap = 0;
    q.front = 0;
    q.size = total_size;
    q.empty = sem_open("/empty", O_CREAT, 0666, size);
    q.full = sem_open("/full", O_CREAT, 0666, 0);
    q.mutex = sem_open("/mutex", O_CREAT, 0666, 1);
    return q;
}

/**
 * @brief unlinks all shared memory used for queue
 *
 * @param queue to be unlinked
 */
void unlink_queue(struct Queue *q)
{
    shm_unlink("queue");
    sem_close(q->empty);
    sem_unlink("/empty");
    sem_close(q->full);
    sem_unlink("/full");
    sem_close(q->mutex);
    sem_unlink("/mutex");
}

/**
 * @brief dequeues request string on shared memory queue
 *
 * @param request_buffer - holds dequeued request string
 * @param q - reference to queue
 */
void dequeue(char *request_buffer, struct Queue *q)
{
    unsigned short reqlen;

    sem_wait(q->full);
    sem_wait(q->mutex);

    if (*q->overlap != 0 || q->front != q->size - *q->overlap) 
        strcpy(request_buffer, &q->buffer[q->front]);
    else {
        strncpy(request_buffer, &q->buffer[q->front], *q->overlap);
        strcpy(&request_buffer[*q->overlap], q->buffer);
        *q->overlap = 0;
    }
    
    sem_post(q->mutex);
    sem_post(q->empty);

    reqlen = strlen(request_buffer) + 1;
    q->front = (q->front + reqlen) % q->size;
}

/**
 * @brief forks search process for each incoming queue request
 *
 * @param req_queue_size - size of request queue
 * @param buffersize - size of shared thread buffer
 */
void watch_queue(int req_queue_size, int buffersize, int filestream)
{
    int process_count;
    char request_buffer[MAXDIRPATH + MAXKEYWORD + 2];
    struct Queue q;
    pid_t pid;
    
    request_buffer[0] = '\0';
    process_count = 0;
    q = create_queue(req_queue_size);
    pid = 1;
    
    while (pid != 0 && strcmp(request_buffer, "exit") != 0) {
        dequeue(request_buffer, &q);
        process_count++;
        pid = fork();
    }

    if (pid < 0) {
        fprintf(stderr, "server: process fork failed\n");
        exit(1);
    } else if (pid == 0 ) {
        if((strcmp(request_buffer, "") != 0)) {
            handle_client_request(request_buffer, buffersize, filestream);
        }
    } else if (pid != 0) {
        for (int i = 0; i < process_count; i++)
            wait(NULL);
        unlink_queue(&q);
    }
}

int main(int argc, char **argv)
{
    int filestream = open("output.txt", O_CREAT | O_APPEND | O_RDWR, 0666);
    if (argc != 3) {
        fprintf(stderr, "server usage: ./server <req-queue-size> <buffersize>\n");
        return 1;
    }

    int req_queue_size = atoi(argv[1]);
    int buffersize = atoi(argv[2]);
    watch_queue(req_queue_size, buffersize, filestream);
    close(filestream);
    return 0;
}
