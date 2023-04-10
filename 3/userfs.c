#include "userfs.h"
#include <stddef.h>
#include "stdlib.h"
#include "string.h"

enum {
	BLOCK_SIZE = 512,
	MAX_FILE_SIZE = 1024 * 1024 * 100,
};

/** Global error code. Set from any function on any error. */
static enum ufs_error_code ufs_error_code = UFS_ERR_NO_ERR;

struct block {
	/** Block memory. */
	char *memory;
	/** How many bytes are occupied. */
	int occupied;
	/** Next block in the file. */
	struct block *next;
	/** Previous block in the file. */
//	struct block *prev;

	/* PUT HERE OTHER MEMBERS */
};

struct file {
	/** Double-linked list of file blocks. */
	struct block *block_list;
	/**
	 * Last block in the list above for fast access to the end
	 * of file.
	 */
//	struct block *last_block;
	/** How many file descriptors are opened on the file. */
	int refs;
	/** File name. */
	char *name;
	/** Files are stored in a double-linked list. */
	struct file *next;
	struct file *prev;
    int dead;
    int occupied;
    /* PUT HERE OTHER MEMBERS */
};

/** List of all files. */
static struct file *file_list = NULL;

struct filedesc {
	struct file *file;
    int Cursor;
    int flagsActivated[3];
	/* PUT HERE OTHER MEMBERS */
};

/**
 * An array of file descriptors. When a file descriptor is
 * created, its pointer drops here. When a file descriptor is
 * closed, its place in this array is set to NULL and can be
 * taken by next ufs_open() call.
 */
static struct filedesc **file_descriptors = NULL;
static int file_descriptor_capacity = 0;

enum ufs_error_code
ufs_errno()
{
	return ufs_error_code;
}

int
ufs_open(const char *filename, int flags)
{
    int flagSet[3];
    memset(flagSet, 0, 3 * sizeof(int));
    if(flags != 0) {
        if (flags & UFS_CREATE) {
            flagSet[0] = 1;
            flagSet[1] = 1;
            flagSet[2] = 1;
        }
        if(flags & UFS_READ_ONLY){
            flagSet[1] = 1;
        }
        if(flags & UFS_WRITE_ONLY){
            flagSet[2] = 1;
        }
        if(flags & UFS_READ_WRITE){
            flagSet[1] = 1;
            flagSet[2] = 1;
        }
    }else{
        flagSet[1] = 1;
        flagSet[2] = 1;
    }
    int existed = 0;

    struct file * currentFileChecked = file_list;
    struct file * foundFile = NULL;
    while(currentFileChecked){
        if(strcmp(currentFileChecked->name, filename) == 0){
            foundFile = currentFileChecked;
            existed = 1;
            break;
        }
        currentFileChecked = currentFileChecked->next;
    }

    if(!existed){
        if(flagSet[0]){
            struct file * new_file = malloc(sizeof(struct file));
            new_file->name = strdup(filename);
            new_file->refs = 0;
            new_file->block_list = calloc(1, sizeof(struct block));
            new_file->block_list->memory = calloc(BLOCK_SIZE, sizeof(char*));
            new_file->block_list->occupied = 0;
//            new_file->last_block = NULL;
            new_file->next = NULL;
            new_file->prev = NULL;
            new_file->occupied = 0;
            new_file->dead = 0;

            if (file_list == NULL) {
                file_list = new_file;
            } else {
                new_file->next = file_list;
                file_list->prev = new_file;
                file_list = new_file;
            }

            foundFile = new_file;
        }else{
            ufs_error_code = UFS_ERR_NO_FILE;
            return -1;
        }
    }

	if(file_descriptors == NULL){
        file_descriptors = calloc(10, sizeof(struct  filedesc *));
        file_descriptor_capacity = 10;
        for(int i = 0; i < 10; ++i){
            file_descriptors[i] = NULL;
        }
	}

    int found = 0;
    int shift = 0;
    for (int i = 0; i < file_descriptor_capacity; ++i) {
        if (file_descriptors[i] == NULL) {
            found = 1;
            file_descriptors[i] = calloc(1, sizeof(struct filedesc));
            shift = i;
            break;
        }
    }

    if (!found) {
        file_descriptors = realloc(file_descriptors, sizeof(struct filedesc*) * file_descriptor_capacity * 2);
        for(int i = file_descriptor_capacity; i < file_descriptor_capacity * 2; ++i) {
            file_descriptors[i] = NULL;
        }

        file_descriptors[file_descriptor_capacity] = calloc(1, sizeof(struct filedesc));
        shift = file_descriptor_capacity;
        file_descriptor_capacity *= 2;
    }

    file_descriptors[shift]->file = foundFile;
    ++foundFile->refs;
    file_descriptors[shift]->flagsActivated[0] = flagSet[0];
    file_descriptors[shift]->flagsActivated[1] = flagSet[1];
    file_descriptors[shift]->flagsActivated[2] = flagSet[2];
    file_descriptors[shift]->Cursor = 0;
    ufs_error_code = UFS_ERR_NO_ERR;
    return shift;
}

ssize_t
ufs_write(int fd, const char *buf, size_t size)
{
    if (fd < 0 || fd > file_descriptor_capacity || file_descriptors[fd] == NULL) {
        ufs_error_code = UFS_ERR_NO_FILE;
        return -1;
    }

    struct filedesc *filedesc = file_descriptors[fd];
    if(!filedesc->flagsActivated[2]) {
        ufs_error_code = UFS_ERR_NO_PERMISSION;
        return -1;
    }

    ssize_t returned = 0;
    struct block *block = filedesc->file->block_list;

    for(int i = 0; i < filedesc->Cursor / BLOCK_SIZE - (filedesc->Cursor % BLOCK_SIZE == 0 ? 1 : 0); ++i) {
        block = block->next;
    }

    for(int i = 0; i < size; ++i) {
        if (filedesc->file->occupied >= MAX_FILE_SIZE) {
            ufs_error_code = UFS_ERR_NO_MEM;
            return -1;
        }

        if (filedesc->Cursor > 0 && filedesc->Cursor % BLOCK_SIZE == 0) {
            if (block->next == NULL) {
                block->next = calloc(1, sizeof(struct block));
                block->next->next = NULL;
                block->next->memory = calloc(BLOCK_SIZE, sizeof(char));
                block->next->occupied = 0;
            }
            block = block->next;
        }

        block->memory[filedesc->Cursor % BLOCK_SIZE] = buf[i];

        returned++;
        filedesc->Cursor++;

        if (filedesc->file->occupied < filedesc->Cursor) {
            filedesc->file->occupied++;
            block->occupied++;
//            filedesc->file->last_block = block;
        }

    }

    return returned;
}

ssize_t
ufs_read(int fd, char *buf, size_t size)
{
    if (fd < 0 || fd > file_descriptor_capacity || file_descriptors[fd] == NULL) {
        ufs_error_code = UFS_ERR_NO_FILE;
        return -1;
    }

    struct filedesc *filedesc = file_descriptors[fd];
    if(!filedesc->flagsActivated[1]) {
        ufs_error_code = UFS_ERR_NO_PERMISSION;
        return -1;
    }

    ssize_t returned = 0;
    struct block *block = filedesc->file->block_list;
    for(int i = 0; i < filedesc->Cursor / BLOCK_SIZE - (filedesc->Cursor % BLOCK_SIZE == 0 ? 1 : 0); ++i) {
        block = block->next;
    }

    for(int i = 0; i < size; ++i) {
        if (filedesc->file->occupied == filedesc->Cursor) {
            return returned;
        }

        if (filedesc->Cursor > 0 && filedesc->Cursor % BLOCK_SIZE == 0) {
            block = block->next;
        }

        buf[i] = block->memory[filedesc->Cursor % BLOCK_SIZE];
        returned++;
        filedesc->Cursor++;
    }

    return returned;
}

void delete_from_file_list(struct file* file) {

    if (file->next == NULL && file->prev == NULL) {
        file_list = NULL;
    }

    if (file->next && file->prev) {
        file->next->prev = file->prev;
        file->prev->next = file->next;
    } else if (file->next) {
        file_list = file->next;
        file->next->prev = NULL;
    } else if (file->prev) {
        file->prev->next = NULL;
    }
}

int
ufs_close(int fd)
{
	if(fd <= file_descriptor_capacity && fd >= 0 && file_descriptors[fd] != NULL){
	    --file_descriptors[fd]->file->refs;

	    if (file_descriptors[fd]->file->refs == 0 && file_descriptors[fd]->file->dead) {
	        free_file(file_descriptors[fd]->file);
	    }

	    free(file_descriptors[fd]);
        file_descriptors[fd] = NULL;
        return 0;
	}
	ufs_error_code = UFS_ERR_NO_FILE;
	return -1;
}

int
ufs_delete(const char *filename) {
    struct file *currentFile = file_list;
    while (currentFile != NULL) {
        if (strcmp(currentFile->name, filename) == 0) {
            currentFile->dead = 1;

            delete_from_file_list(currentFile);
            if (currentFile->refs == 0) {
                free_file(currentFile);
//                free(currentFile);
            }
            return 0;
        }

        currentFile = currentFile->next;
    }

	ufs_error_code = UFS_ERR_NO_FILE;
	return -1;
}

void close_program() {
    for(int i = 0; i < file_descriptor_capacity; ++i) {
        if (file_descriptors[i]) {
            free(file_descriptors[i]);
        }
    }
    free(file_descriptors);

    struct file *file = file_list;
    while(file) {
        struct file* temp = file;
        file = temp->next;

        free_file(temp);
    }
}

void free_file(struct file* file) {
    struct block* block = file->block_list;
    while(block) {
        struct block* to_free = block;
        block = block->next;

        free(to_free->memory);
        free(to_free);
    }
    free((char*)file->name);
    free(file);
}

int
ufs_resize(int fd, size_t new_size) {

    return 0;
}