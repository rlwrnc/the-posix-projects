/*
 * CSE 420 Project 2
 * Author: Raymond Lawrence
 * 
 */


#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <stdlib.h>
#include <dirent.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <ctype.h>
#include <pthread.h>

/* linked list w/ subroutines */

struct node {
    char *path;
    int level;
    int keyword_frequency;
    pthread_t tid;
    struct node *next;
    struct node *prev;
};

struct list {
    struct node *head;
    struct node *tail;
};

//creation subroutines

struct node *create_node(char *path, int level, int keyword_frequency)
{
    struct node *node = malloc(sizeof(struct node));
    if (node == NULL) {
        fprintf(stderr, "%s: couldn't create memory for list; %s\n", "dirlist", strerror(errno));
        exit(-1);
    }
    node->path = strdup(path);
    node->level = level;
    node->keyword_frequency = keyword_frequency;
    node->tid = 0;
    node->next = NULL;
    node->prev = NULL;
    return node;
}

struct list *create_list()
{
    struct list *list = malloc(sizeof(struct list));
    if (list == NULL) {
        fprintf(stderr, "%s: couldn't create memory for list; %s\n", "dirlist", strerror(errno));
        exit(-1);
    }
    list->head = NULL;
    list->tail = NULL;
    return list;
}

//frequency helper functions

void seq_search_file(struct node *node, char *keyword)
{
    FILE *fs;
    char buff[1025];
    char *token, *context, *exclude;
    int frequency;

    fs = fopen(node->path, "r");                                    //open file with read permissions
    context = NULL, exclude = " \t\n";                              //initialize strings for strtok_r
    frequency = 0;                                                  //initialize frequency
    
    while (fgets(buff, 1025, fs) != NULL)                           //place the next line into buff until we reach the end of the file
        //place next token into token until end of line
        for (token = strtok_r(buff, exclude, &context); token; token = strtok_r(NULL, exclude, &context))
            if (strcmp(token, keyword) == 0)                        //if the current token is the keyword, add to frequency
                frequency++;
    node->keyword_frequency = frequency;
    fclose(fs);
}

struct psf_args {                                                   //structure to pass to pthread_create()
    struct node *node;
    char *keyword;
};

void *psf_runner(void *param);                                      //function prototype for my sanity

void par_search_file(struct node *node, char *keyword)              //wrapper for thread function
{
    pthread_attr_t attributes;
    struct psf_args *args = malloc(sizeof(struct psf_args));        //allocate new argument for each thread
    args->node = node;
    args->keyword = keyword;
    pthread_attr_init(&attributes);
    pthread_attr_setdetachstate(&attributes, PTHREAD_CREATE_JOINABLE);
    if (pthread_create(&node->tid, &attributes, &psf_runner, args)) {
            fprintf(stderr, "pardirlist: could not create thread; %s\n", strerror(errno));
            exit(-1);
    }
}

void *psf_runner(void *param)                                       //same as seq_search, but with multithreading
{
    //cast void pointer to psf_args pointer so the compiler knows what we're talking about
    struct psf_args *args = (struct psf_args *) param;
    FILE *fs;
    char buff[1025];
    char *token, *context, *exclude;
    int frequency;

    fs = fopen(args->node->path, "r");                              //must reference the args struct for the node's path
    context = NULL, exclude  = " \t\n";
    frequency = 0;

    while (fgets(buff, 1025, fs) != NULL)
        for (token = strtok_r(buff, exclude, &context); token; token = strtok_r(NULL, exclude, &context))
            if (strcmp(token, args->keyword) == 0)                  //must reference the args struct for the keyword
                frequency++;
    args->node->keyword_frequency = frequency;
    fclose(fs);
    free(args);                                                     //free memory we allocated to prevent leakage
    pthread_exit(0);                                                //exit with null value as we passed our node by reference
}

//inserts

void insert_sorted(struct node *node, struct list *list)
{
    if (list->head == NULL && list->tail == NULL) {
        list->head = node;
        list->tail = node;
    } else if (strcmp(node->path, list->head->path) <= 0) {
        list->head->prev = node;
        node->next = list->head;
        list->head = node;
    } else if (strcmp(node->path, list->tail->path) > 0) {
        list->tail->next = node;
        node->prev = list->tail;
        list->tail = node;
    } else {
        struct node *ptr = list->head;
        while (ptr->next != NULL && strcmp(node->path, ptr->path) > 0)
            ptr = ptr->next;
        node->next = ptr;
        node->prev = ptr->prev;
        ptr->prev->next = node;
        ptr->prev = node;
    }
}

void populate_list(char *path, struct list *list, char *keyword, int ispar)
{
    static int current_level = 1;
    if (current_level == 1) {
        insert_sorted(create_node(path, current_level, 0), list);
        current_level++;
    }
    DIR *ds = opendir(path);
    char tmp[255];
    struct dirent *d;
    struct stat buf;
    while ((d = readdir(ds)) != NULL) {
        if (d->d_name[0] == '.')    //if hidden file, continue
            continue;
        
        /* create a temporary string containing {PATH}/{DIRECTORY NAME} */
        strcpy(tmp, path);
        strcat(tmp, "/");
        strcat(tmp, d->d_name);
        
        stat(tmp, &buf);            //populate buf with file information
        struct node *new = create_node(tmp, current_level, 0);
        insert_sorted(new, list);
        if (S_ISDIR(buf.st_mode)) {
            current_level++;
            populate_list(tmp, list, keyword, ispar);
            current_level--;
        } else {
            if (ispar == 1)
                par_search_file(new, keyword);
            else
                seq_search_file(new, keyword);
        }
    }
    closedir(ds);
}

//deletions

void destroy_list(struct list *list)
{
    struct node *curr = list->head, *tmp;
    while (curr != NULL) {
        free(curr->path);
        tmp = curr;
        curr = curr->next;
        free(tmp);
    }
    free(list);
}

//prints

void print_list_to_file(struct list *list, char *filename, int ispar)
{
    int order;
    struct node *curr = list->head;
    FILE *fs = fopen(filename, "w");
    while (curr != NULL) {
        if (ispar == 1 && curr->tid != 0) {
            if (pthread_join(curr->tid, NULL)) {
                fprintf(stderr, "pardirlist: could not join thread; %s\n", strerror(errno));
                exit(-1);
            }
        }
        if (curr->prev != NULL && curr->level == curr->prev->level)
            order++;
        else
            order = 1;
        fprintf(fs, "%d:%d:%d:%s\n", curr->level, order, curr->keyword_frequency, curr->path);
        curr = curr->next;
    }
    fclose(fs);
}

void insertion_sort_by_level_increasing(struct list *list)
{
    struct node *fi = list->head->next, *bi, *tmp;
    int key;
    while (fi != NULL) {
        key = fi->level;
        bi = fi->prev;
        tmp = fi;
        fi = fi->next;
        while (bi != list->head && bi->level > key)
            bi = bi->prev;

        //remove tmp and place it after bi 
        if (tmp->next != NULL)
            tmp->next->prev = tmp->prev;
        tmp->prev->next = tmp->next;
        tmp->next = bi->next;
        tmp->prev = bi;
        if (bi->next != NULL)
            bi->next->prev = tmp;
        bi->next = tmp;
    }
}

/* main */

int main(int argc, char **argv)
{
    if (argc != 5) {
        fprintf(stderr, "dirlist: usage: dirlist <directory_path> <keyword> <output_file> <ispar>\n");
        return 1;
    }

    char *dirpath = argv[1], *keyword = argv[2], *outfile = argv[3];
    int ispar = atoi(argv[4]);

    if (ispar != 0 && ispar != 1) {
        fprintf(stderr, "dirlist: <ispar> must be 0 or 1");
        return 1;
    }

    struct list *dirlist = create_list();
    populate_list(dirpath, dirlist, keyword, ispar);
    insertion_sort_by_level_increasing(dirlist);
    print_list_to_file(dirlist, outfile, ispar);    
    destroy_list(dirlist);
    return 0;
}
