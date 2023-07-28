#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <stdlib.h>
#include <dirent.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <ctype.h>

/* linked list w/ subroutines */

struct node {
    char *path;
    int level;
    struct node *next;
    struct node *prev;
};

struct list {
    struct node *head;
    struct node *tail;
};

//creation subroutines

struct node *create_node(char *path, int level)
{
    struct node *node = malloc(sizeof(struct node));
    if (node == NULL) {
        fprintf(stderr, "%s: couldn't create memory for list; %s\n", "dirlist", strerror(errno));
        exit(-1);
    }
    node->path = strdup(path);
    node->level = level;
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

void populate_list(char *path, struct list *list)
{
    static int current_level = 1;
    if (current_level == 1) {
        insert_sorted(create_node(path, current_level), list);
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
        insert_sorted(create_node(tmp, current_level), list);
        if (S_ISDIR(buf.st_mode)) {
            current_level++;
            populate_list(tmp, list);
            current_level--;
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

void print_list_to_file(struct list *list, char *filename)
{
    int order;
    struct node *curr = list->head;
    FILE *fs = fopen(filename, "w");
    while (curr != NULL) {
        if (curr->prev != NULL && curr->level == curr->prev->level)
            order++;
        else
            order = 1;
        fprintf(fs, "%d:%d:%s\n", curr->level, order, curr->path);
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
    if (argc != 3) {
        printf("usage: dirlist directory_path file_name\n");
        return -1;
    }

    char *dirpath = argv[1], *outfile = argv[2];
    struct list *dirlist = create_list();
    populate_list(dirpath, dirlist);
    insertion_sort_by_level_increasing(dirlist);
    print_list_to_file(dirlist, outfile);    
    destroy_list(dirlist);
    return 0;
}
