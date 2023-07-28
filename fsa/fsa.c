#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdbool.h>
#include <ext2fs/ext2fs.h>

#define NUMARGS 2
#define BOOT_OFFSET 1024

/**
 * @brief Linked list nodes containing inode number id and the full path of the file the inode represents.
 * 
 */
struct list_node {
    unsigned int inode_number;
    char *path;
    struct list_node *next;
};

struct inode_list {
    struct list_node *head;
    struct list_node *tail;
};

/**
 * @brief Stores information about the file system.
 * 
 */
struct file_system_info {
    unsigned int fd;
    struct ext2_super_block *sb;
    struct ext2_group_desc *gd;
    unsigned int block_size;
    unsigned int inode_table_block;
    unsigned int inode_size;
};

struct ext2_inode * read_inode(struct file_system_info *fsi, unsigned int inode_number);
void read_directory(struct file_system_info *fsi, struct ext2_inode *inode, struct inode_list *list, char *path);

/**
 * @brief Create a node object
 * 
 * @param inode_number - number id of an inode
 * @param path - full path of the file the inode represents
 * @return struct list_node* 
 */
struct list_node *create_node(unsigned int inode_number, char *path)
{
    struct list_node *node = malloc(sizeof(struct list_node));
    if (node == NULL) {
        perror("memory allocation failed");
        exit(1);
    }
    node->inode_number = inode_number;
    node->path = strdup(path);
    node->next = NULL;
    return node;
}

/**
 * @brief Creates the linked list object
 * 
 * @return struct inode_list* 
 */
struct inode_list *init_list()
{
    struct inode_list *list = malloc(sizeof(struct inode_list));
    if (list == NULL) {
        perror("memory allocation failed");
        exit(1);
    }
    list->head = NULL;
    list->tail = NULL;
    return list;
}

/**
 * @brief Destroys the linked list.
 * 
 * @param list 
 */
void destroy_list(struct inode_list *list)
{
    struct list_node *ptr = list->head;
    struct list_node *tmp;
    while (ptr != NULL) {
        free(ptr->path);
        tmp = ptr;
        ptr = ptr->next;
        free(tmp);
    }
    free(list);
}

/**
 * @brief Inserts a node at the tail of the list.
 * 
 * @param node 
 * @param list 
 */
void insert_tail(struct list_node *node, struct inode_list *list)
{
    if (list->head == NULL && list->tail == NULL) {
        list->head = node;
        list->tail = node;
    } else {
        list->tail->next = node;
        list->tail = node;
    }
}

/**
 * @brief Prints full path of every file in the list.
 * 
 * @param list 
 */
void print_list(struct inode_list *list) {
  struct list_node *ptr = list->head;  
  while (ptr != NULL) {
    printf("%s\n", ptr->path);
    ptr = ptr->next;
  }
}

/**
 * @brief Linear search for an absolute path.
 * 
 * @param path - path to search for
 * @param list - the linked list being searched
 * @return unsigned int 
 */
unsigned int search_by_path(char *path, struct inode_list *list) {
  struct list_node *ptr = list->head;
  while(ptr != NULL) {
    if(strcmp(path, ptr->path) == 0) {
      return ptr->inode_number;
    }
    else {
      ptr = ptr->next;
    }
  }
  return 0;
}

/**
 * @brief Create a fsi object
 * 
 * @param fd - file descriptor to read from
 * @param sb - superblock
 * @param gd - group descriptor
 * @param block_size - block size in bytes
 * @param inode_table_block - block number of first node in inode table.
 * @param inode_size - size of inodes in file system
 * @return struct file_system_info* 
 */
struct file_system_info *create_fsi(
    int fd, 
    struct ext2_super_block *sb, 
    struct ext2_group_desc *gd, 
    unsigned int block_size, 
    unsigned int inode_table_block, 
    unsigned int inode_size) 
{
    struct file_system_info *fsi = malloc(sizeof(struct file_system_info));
    if(fsi == NULL) {
        perror("File system info memory allocation failed");
        exit(1);
    }
    fsi->fd = fd;
    fsi->sb = sb;
    fsi->gd = gd;
    fsi->block_size = block_size;
    fsi->inode_table_block = inode_table_block;
    fsi->inode_size = inode_size;

    return fsi;
}

/**
 * @brief Reads the inode corresponding to inputted inode_number
 * 
 * @param fsi - file system information
 * @param inode_number - number of inode to be read.
 * @return struct ext2_inode* 
 */
struct ext2_inode *read_inode(struct file_system_info *fsi, unsigned int inode_number)
{
    int rv;
    struct ext2_inode *inode = NULL;
    inode = malloc(sizeof(struct ext2_inode));
    
    //seek to location of inode in memory
    unsigned int inode_location = (fsi->inode_table_block * fsi->block_size) + (inode_number-1) * fsi->inode_size;
    if(lseek(fsi->fd, inode_location, SEEK_SET) != inode_location) {
        perror("file seek failed");
        exit(1);
    }

    //read the inode from current location.
    rv = read(fsi->fd, inode, sizeof(struct ext2_inode));
    if(rv == -1) {
        perror("file read failed");
        exit(1);
    }

    return inode;
}

/**
 * @brief Recursively reads all directory entries from a given base path.
 * 
 * @param fsi - file system information
 * @param inode - inode from which we read data blocks from.
 * @param list - linked list for populating nodes.
 * @param path - the base path to search from.
 */
void read_directory(struct file_system_info *fsi, struct ext2_inode *inode, struct inode_list *list, char *path)
{
    int rv;
    for (int i = 0; i < 12; i++) {
        if(inode->i_block[i] != 0) { 

            //set base & limit for the data block to be read.
            int directory_entry_index = inode->i_block[i] * fsi->block_size;
            int block_upper_limit = directory_entry_index + fsi->block_size;

            while(directory_entry_index < block_upper_limit) {  
                struct ext2_dir_entry_2 *dirent = malloc(sizeof(struct ext2_dir_entry_2));

                //seek to the current directory entry index
                if(lseek(fsi->fd, directory_entry_index, SEEK_SET) != directory_entry_index) {
                    perror("file seek failed");
                    exit(1);
                }

                //read the directory entry from the data block
                rv = read(fsi->fd, dirent, sizeof(struct ext2_dir_entry));
                if(rv == -1) {
                    perror("file read failed");
                    exit(1);
                }

                directory_entry_index = directory_entry_index + dirent->rec_len;   //offset to go to the next directory entry.

                //filter out "." and ".." entries and invalid inodes.
                if(dirent->name[0] != '.' && dirent->inode != 0) {
                    //copy only dirent->name_len bytes from the dirent->name, this will ensure no garbage symbols are included.
                    char file_name[dirent->name_len];
                    memcpy(file_name, dirent->name, dirent->name_len);
                    file_name[dirent->name_len] = '\0';

                    //sets up the formatting for the full file path.
                    char file_path[strlen(path) + strlen(file_name)];
                    strcpy(file_path, path);
                    strcat(file_path, "/");
                    strncat(file_path, file_name, dirent->name_len);
                    strcat(file_path, "\0");

                    struct list_node *new_node = create_node(dirent->inode, file_path);
                    insert_tail(new_node, list);

                    //recursively check subdirectories.
                    if(dirent->file_type == EXT2_FT_DIR) {
                        struct ext2_inode *tmp = read_inode(fsi, dirent->inode);
                        read_directory(fsi, tmp, list, file_path); 
                        free(tmp);
                    }
                }
             
                free(dirent);
            }
        }
    }
}

/**
 * @brief Traverses the directory structure and populates the linked list
 * 
 * @param fsi - file system information
 * @return struct inode_list* 
 */
struct inode_list *populate_list(struct file_system_info *fsi) {
    struct inode_list *list = init_list();
    
    //set up root directory & start traversing
    struct ext2_inode *root_inode = read_inode(fsi, EXT2_ROOT_INO);
    struct list_node *root_list_node = create_node(EXT2_ROOT_INO, "/");
    read_directory(fsi, root_inode, list, "");
    insert_tail(root_list_node, list);
    free(root_inode);

    return list;
}

/**
 * @brief Prints the content of a file's data block
 * 
 * @param fd - file descriptor
 * @param data_block_number - data block number to be read
 * @param block_size 
 * @param linebuffer - buffer to read file contents to
 */
void print_file_contents(int fd, unsigned int data_block_number, unsigned int block_size, char *linebuffer)
{
    int rv;
    int data_block = data_block_number * block_size;

    //seek to the requested data block
    if(lseek(fd, data_block, SEEK_SET) != data_block) {
        perror("file seek failed");
        exit(1);
    }

    //read data block into the buffer
    rv = read(fd, linebuffer, block_size);
    if(rv == -1) {
        perror("file read failed");
        exit(1);
    }
    printf("%s", linebuffer);
}

/**
 * @brief Traverses the disk and prints paths of every file
 * 
 * @param fsi - file system information
 */
void print_disk(struct file_system_info *fsi)
{
    struct inode_list *list = populate_list(fsi);
    print_list(list);
    destroy_list(list);
}

/**
 * @brief Traveres the disk and prints the content of a requested file.
 * 
 * @param fsi - file system information
 * @param absolute_path - the path of the file we want to print
 */
void print_file(struct file_system_info *fsi, char *absolute_path)
{
    int rv;
    struct inode_list *list = populate_list(fsi);
    char *line_buffer = malloc(fsi->block_size);
    unsigned int inode_found = search_by_path(absolute_path, list);

    if(inode_found != 0) {
        struct ext2_inode *read_node = read_inode(fsi, inode_found);

        //handle all direct data blocks
        for(int i = 0; i < 12; i++) {
            if(read_node->i_block[i] != 0) {
                print_file_contents(fsi->fd, read_node->i_block[i], fsi->block_size, line_buffer);
            }
        }
        //handle single indirect blocks if they exist.
        if (read_node->i_block[12] != 0) {
            //set initial index & limit for the block
            int single_indirect_index = read_node->i_block[12] * fsi->block_size;
            int block_upper_limit = single_indirect_index + fsi->block_size;

            while(single_indirect_index < block_upper_limit) {
                unsigned int data_block_number;

                //seek to the index in the inner level table.
                if(lseek(fsi->fd, single_indirect_index, SEEK_SET) != single_indirect_index) {
                    perror("file seek failed");
                    exit(1);
                }
                
                //read the data block number from inner level table.
                rv = read(fsi->fd, &data_block_number, sizeof(unsigned int));
                if(rv == -1) {
                    perror("file read failed");
                    exit(1);
                }

                if(data_block_number != 0) {
                    print_file_contents(fsi->fd, data_block_number, fsi->block_size, line_buffer);
                }
                single_indirect_index = single_indirect_index + sizeof(int);
            }
        }
        free(read_node);
    }
    free(line_buffer);
    destroy_list(list);
}

/**
 * @brief Gets basic information from the file system and prepares for traversal
 * 
 * @param disk_name - name of the disk to read from
 * @param arg_flags - flag indicating whether we print disk or a file
 * @param absolute_path - path of the file we want to print, NULL if solely traversing
 */
void disk_analyze(char *disk_name, bool *arg_flags, char *absolute_path)
{
    int rv;
    struct ext2_super_block *sb = malloc(sizeof(struct ext2_super_block));
    struct ext2_group_desc *gd = malloc(sizeof(struct ext2_group_desc));

    //open disk in read only mode
    int fd_disk = open(disk_name, O_RDONLY);
    if (fd_disk == -1) {
        perror("disk_image_file open failed");
        exit(1);
    }

    //skip boot section
    if (lseek(fd_disk, BOOT_OFFSET, SEEK_SET) != BOOT_OFFSET) {
        perror("file seek failed");
        exit(1);
    }

    //read superblock information
    rv = read(fd_disk, sb, sizeof(struct ext2_super_block));
    if (rv == -1) {
        perror("file read failed");
        exit(1);
    }

    //set seek to the beginning of group descriptor block. block_size = 4096, group descriptors in block 1.
    unsigned int block_size = 1024 << sb->s_log_block_size;
    if(lseek(fd_disk, block_size, SEEK_SET) != block_size) {
        perror("file seek failed");
        exit(1);
    }

    //read group descriptor block
    rv = read(fd_disk, gd, sizeof(struct ext2_group_desc));
    if (rv == -1) {
        perror("Group descriptors read failed");
        exit(1);
    }

    struct file_system_info *fsi = create_fsi(fd_disk, sb, gd, block_size, gd->bg_inode_table, sb->s_inode_size);

    for (int i = 0; i < NUMARGS; i++) {
        if (arg_flags[i]) {
            switch(i) {
                case 0: print_disk(fsi);
                        break;
                case 1: print_file(fsi, absolute_path);
                        break;
            }
        }
    }

    free(sb);
    free(gd);
    free(fsi);
    close(fd_disk);
}

int main(int argc, char *argv[])
{
    char *disk_name, *absolute_path = NULL;
    bool arg_flags[NUMARGS];
    for (int i = 0; i < NUMARGS; i++)
        arg_flags[i] = false;

    if (argc < 2) {
        fprintf(stderr, "usage: ./fsa <diskname> [args]\n");
        return 1;
    }

    disk_name = argv[1];

    for(int i = 2; i < argc; i++) {
        if (strcmp(argv[i], "-traverse") == 0)
            arg_flags[0] = true;
        else if (strcmp(argv[i], "-file") == 0 && arg_flags[1] == false) {
            arg_flags[1] = true;
            absolute_path = argv[++i];
        } else {
            fprintf(stderr, "fsa: invalid argument\n");
            return 1;
        }
    }
    
    disk_analyze(disk_name, arg_flags, absolute_path);
    return 0;
}
