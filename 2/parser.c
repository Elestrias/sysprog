#include "parser.h"
#include "string.h"
#include "stdlib.h"
#include <stdio.h>

void addToStorage(struct Command * com, char * word, int* starter){
    if(*starter){
        com->name = strdup(word);
        *starter = 0;
        com->argv = calloc(1024, sizeof(char*));
        com->capacity = 1024;
    }
    com->argv[com->argc] = strdup(word);
    ++com->argc;
    if(com->argc >= com->capacity){
        com->capacity *= 2;
        com->argv = realloc(com->argv, com->capacity*sizeof(char*));
    }
}

void reallocStorage(struct Commands * comms){
    comms->capacity *= 2;
    comms->data = realloc(comms->data, comms->capacity *sizeof(struct Command));
}

void reallocWord(char **currentWord, size_t * size){
    *size *= 2;
    (*currentWord) = realloc((*currentWord), *size * sizeof(char));
}

void checkStorageWord(char ** currentWord, size_t * size, int cursor){
    if(cursor >= *size){
        reallocWord(currentWord, size);
    }
}

int parseInput(struct Commands * storage, char* line, size_t len){
    int first_clear_line = 0;
    char symbol;
    size_t currentSize = 1024;
    char * currentWord = calloc(1024, sizeof(char));
    memset(currentWord, 0, 1024);
    int cursor = 0;
    int starter = 1;
    int braced = 0;
    int bracedDouble = 0;

    for(int i = 0; i < len; i++){
        symbol = line[i];
        if(symbol =='\000'){
            break;
        }
        if(!braced && symbol == '#'){
            if(bracedDouble && i != 0 && line[i-1] == '\\'){}else{
                break;
            }
        }
        switch (symbol) {
            case '|':
                if(!braced && !bracedDouble) {
                    if (i + 1 != len && line[i + 1] == '|') {
                        if(cursor){
                            if(starter){
                                ++storage->size;
                            }
                            addToStorage(storage->data + storage->size, currentWord, & starter);
                        }

                        storage->data[++storage->size].type = OR;

                        memset(currentWord, 0, currentSize);
                        currentSize = 512;
                        reallocWord(&currentWord, &currentSize);
                        cursor = 0;

                        starter = 1;
                        i++;
                    }else if(i + 1 == len){
                        // INVALID, but I will ignore
                    }else{
                        if(cursor){
                            if(starter){
                                ++storage->size;
                                if(!(storage->size < storage->capacity)){
                                    reallocStorage(storage);
                                }
                            }
                            addToStorage(storage->data + storage->size, currentWord, & starter);
                        }
                        if(!(storage->size+1 < storage->capacity)){
                            reallocStorage(storage);
                        }
                        storage->data[++storage->size].type = PIPE;

                        memset(currentWord, 0, currentSize);
                        currentSize = 512;
                        reallocWord(&currentWord, &currentSize);

                        cursor = 0;

                        starter = 1;
                    }
                    break;
                }
            case '>':
                if(!braced && !bracedDouble){
                    if (i + 1 != len && line[i + 1] == '>') {
                        if(cursor){
                            if(starter){
                                ++storage->size;
                            }
                            if(!(storage->size < storage->capacity)){
                                reallocStorage(storage);
                            }
                            addToStorage(storage->data + storage->size, currentWord, & starter);
                        }
                        if(!(storage->size+1 < storage->capacity)){
                            reallocStorage(storage);
                        }
                        storage->data[++storage->size].type = END_PIPE;
                        memset(currentWord, 0, currentSize);
                        currentSize = 512;
                        reallocWord(&currentWord, &currentSize);
                        cursor = 0;
                        starter = 1;
                        i++;
                    }else if(i + 1 == len){
                        // INVALID, but I will ignore
                    }else{
                        if(cursor){
                            if(starter){
                                ++storage->size;
                                if(!(storage->size < storage->capacity)){
                                    reallocStorage(storage);
                                }
                            }
                            addToStorage(storage->data + storage->size, currentWord, & starter);
                        }
                        if(!(storage->size + 1 < storage->capacity)){
                            reallocStorage(storage);
                        }
                        storage->data[++storage->size].type = FILE_PIPE;
                        memset(currentWord, 0, currentSize);
                        currentSize = 512;
                        reallocWord(&currentWord, &currentSize);
                        cursor = 0;
                        starter = 1;
                    }
                    break;
                }
            case '&':
                if(!braced && !bracedDouble){
                    if (i + 1 != len && line[i + 1] == '&') {
                        if(cursor){
                            if(starter){
                                ++storage->size;
                                if(!(storage->size < storage->capacity)){
                                    reallocStorage(storage);
                                }
                            }
                            addToStorage(storage->data + storage->size, currentWord, & starter);
                        }
                        if(!(storage->size +1 < storage->capacity)){
                            reallocStorage(storage);
                        }
                        storage->data[++storage->size].type = AND;
                        memset(currentWord, 0, currentSize);
                        currentSize = 512;
                        reallocWord(&currentWord, &currentSize);

                        cursor = 0;
                        starter = 1;
                        i++;
                    }else if(i + 1 == len){
                        // INVALID, but I will ignore
                    }else{
                        if(cursor){
                            if(starter){
                                ++storage->size;
                                if(!(storage->size < storage->capacity)){
                                    reallocStorage(storage);
                                }
                            }
                            addToStorage(storage->data + storage->size, currentWord, & starter);
                        }
                        if(!(storage->size + 1 < storage->capacity)){
                            reallocStorage(storage);
                        }
                        storage->data[++storage->size].type = DAEMON;
                        memset(currentWord, 0, currentSize);
                        currentSize = 512;
                        reallocWord(&currentWord, &currentSize);
                        cursor = 0;
                        starter = 1;
                    }
                    break;
                }
            case ' ':
                if(!braced && !bracedDouble){
                    if(cursor) {
                        if (starter) {
                            ++storage->size;
                            if(!(storage->size < storage->capacity)){
                                reallocStorage(storage);
                            }
                        }
                        addToStorage(storage->data + storage->size, currentWord, &starter);
                        memset(currentWord, 0, currentSize);
                        currentSize = 512;
                        reallocWord(&currentWord, &currentSize);
                        cursor = 0;
                    }
                    break;
                }
                goto general;
            case '\\':
                if(i + 1 < len && (line[i + 1] == ' ' || line[i + 1] == '\\')){
                    checkStorageWord(&currentWord, &currentSize, cursor + 1);
                    currentWord[cursor++] = line[++i];
                    break;
                }else if(i + 1 < len && ((braced && line[i+1] == '\'') || (bracedDouble && line[i+1] == '\"'))){
                    checkStorageWord(&currentWord, &currentSize, cursor + 2);
                    currentWord[cursor++] = line[i++];
                    currentWord[cursor++] = line[i];
                    break;
                }else if(i+1 < len && line[i+1] == 'n'){
                    checkStorageWord(&currentWord, &currentSize, cursor + 1);
                    currentWord[cursor++] = line[i];
                    break;
                }else if(i+1 != len && line[i+1] == '\n'){
                    i = -1;
                    if (first_clear_line) {
                        free(line);
                    }
                    first_clear_line = 1;
                    line = NULL;
                    len = 0;
                    if(getline(&line, &len, stdin) == -1){
                        free(line);
                        free(currentWord);
                        return 1;
                    }
                    break;
                }
            case '\"':
                if(!braced && bracedDouble){
                    bracedDouble = 0;
                    break;
                }else if(!braced && !bracedDouble){
                    bracedDouble = 1;
                    break;
                }
            default:
                if(symbol == '\'' && !braced && !bracedDouble){
                    braced = 1;
                    break;
                }else if(symbol == '\'' && braced && !bracedDouble){
                    braced = 0;
                    break;
                }
                general:
                checkStorageWord(&currentWord, &currentSize, cursor + 1);
                currentWord[cursor++] = symbol;
                break;
            case '\n':
                if(braced || bracedDouble){
                    i = -1;
                    if (first_clear_line) {
                        free(line);
                    }
                    first_clear_line = 1;
                    line = NULL;
                    len = 0;
                    if(getline(&line, &len, stdin) == -1){
                        free(line);
                        free(currentWord);
                        return 1;
                    };
                }
                if(braced || bracedDouble){
                    goto general;
                }
                break;
        }
    }
    if(cursor){
        if(starter){
            ++storage->size;
        }
        if(!(storage->size < storage->capacity)){
            reallocStorage(storage);
        }
        addToStorage(storage->data + storage->size, currentWord, &starter);
    }
    free(currentWord);
    if (first_clear_line) {
        free(line);
    }
    return 0;
}
