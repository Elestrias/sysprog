#pragma once
#include <stddef.h>

enum Sep_{
    COMM=0,
    PIPE=1,
    FILE_PIPE=2,
    END_PIPE=3,
    AND=4,
    OR=5,
    DAEMON=6,
} typedef Sep;

struct Command{
        char * name;
        int argc;
        char **argv;
        Sep type;
};

struct Commands{
    int size;
    int capacity;
    struct Command * data;
};

int parseInput(struct Commands * storage, char* line, size_t len);