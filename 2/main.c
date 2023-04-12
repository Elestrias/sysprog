#include <stdio.h>
#include "parser.h"
#include "stdlib.h"
#include <string.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#include <sys/stat.h>
#include <fcntl.h>
//#include "heap_help/heap_help.h"

void clearMemory(struct Commands * coms){
    for(int i = 0; i < coms->size; i++){
        for(int j = 0; j < coms->data[i].argc; j++){
            free(coms->data[i].argv[j]);
        }
        if(coms->data[i].argc != 0) {
            free(coms->data[i].name);
        }
        free(coms->data[i].argv);
    }
    free(coms->data);
}

int executeCd(int argc, char *argv[]){
    char *path = argv[1];
    if (chdir(path) != 0) {
        printf("cd: %s: No such file or directory\n", path);
        return 1;
    }
    return 0;
}

void executeExit(struct Commands *comms, int needClear, int argc, char *argv[]){
    if (argc == 1) {
        if(needClear) {
            clearMemory(comms);
        }
        exit(0);
    }

    int ret_code = 0;
    sscanf(argv[1], "%d", &ret_code);
    if(needClear) {
        clearMemory(comms);
    }
    exit(ret_code);
}

int executeCommand(struct Commands *comms, int *current, int globalDaemon) {
    struct Command command = comms->data[*current];

    int file = 0;
    int daemon = globalDaemon;
    FILE *fd;

    int status = 0;


    if (*current + 1 != comms->size) {
        Sep sep = comms->data[*current + 1].type;

        if (sep == PIPE) {
            if (*current + 2 < comms->size) {
                int capacity = 1024;
                struct Command **pipedComms = calloc(capacity, sizeof(struct Command *));
                int size = 0;
                while (*current + 2 < comms->size && comms->data[*current + 1].type == PIPE) {
                    if(!(size + 1 < capacity)){
                        capacity *= 2;
                        pipedComms = realloc(pipedComms, capacity*sizeof(struct  Command *));
                    }
                    pipedComms[size++] = comms->data + *current;
                    *current += 2;
                }
                if(!(size + 1 < capacity)){
                    capacity += 1;
                    pipedComms = realloc(pipedComms, capacity*sizeof(struct  Command *));
                }
                pipedComms[size++] = comms->data + *current;


                if (*current + 2 < comms->size &&
                    (comms->data[*current + 1].type == FILE_PIPE || comms->data[*current + 1].type == END_PIPE)) {
                    file = 1;

                    *current += 2;
                }

                if (*current + 1 < comms->size && comms->data[*current + 1].type == DAEMON) {
                    if (*current + 2 < comms->size && (comms->data[*current + 1].type != COMM ||
                        comms->data[*current - 1].type != COMM)) {
                        printf("Invalid syntax in command - revert");
                        return 1;
                    }
                    daemon = 1;
                }

                int pf[2];
                int old_pf[2];
                pid_t pids[size];

                pipe(pf);

                for (int i = 0; i < size; i++) {
                    old_pf[0] = pf[0];
                    old_pf[1] = pf[1];
                    if (pipe(pf) > 0) {
                        perror("Pipes");
                        return 1;
                    }
                    pids[i] = fork();
                    if (pids[i] == -1) {
                        perror("pid open error");
                    } else if (pids[i] == 0) {
                        if (i == 0) {
                            close(pf[0]);
                            dup2(pf[1], STDOUT_FILENO);

                            close(old_pf[0]);
                            close(old_pf[1]);
                        } else if (i == size - 1) {
                            close(old_pf[1]);
                            close(pf[0]);
                            close(pf[1]);
                            dup2(old_pf[0], STDIN_FILENO);
                            if (file) {
                                int flags = O_WRONLY | O_CREAT;
                                if (comms->data[*current - 1].type == END_PIPE)
                                    flags |= O_APPEND;
                                else
                                    flags |= O_TRUNC;
                                int mode = S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP | S_IROTH;
                                int outfd = open(comms->data[*current].name, flags, mode);
                                dup2(outfd, STDOUT_FILENO);
                            }
                        } else {
                            close(old_pf[1]);
                            close(pf[0]);

                            dup2(old_pf[0], STDIN_FILENO);
                            dup2(pf[1], STDOUT_FILENO);
                        }

                        if(strcmp(pipedComms[i]->argv[0], "exit") == 0){
                            executeExit(comms, 0, pipedComms[i]->argc, pipedComms[i]->argv);
                        }

                        if(strcmp(pipedComms[i]->argv[0], "cd") == 0){
                            exit(executeCd(pipedComms[i]->argc, pipedComms[i]->argv));
                        }

                        pipedComms[i]->argv[pipedComms[i]->argc] = NULL;
                        if (execvp(pipedComms[i]->name, pipedComms[i]->argv) == -1) {
                            printf("%s: command not found\n", pipedComms[i]->name);
                            goto dead;
                        }
                    }

                    close(old_pf[0]);
                    close(old_pf[1]);
                }

                if (!daemon) {
                    for (int i = 0; i < size; i++) {
                        waitpid(pids[i], &status, 0);
                    }
                    close(pf[0]);
                    close(pf[1]);
                }
                dead:
                    close(pf[0]);
                    close(pf[1]);
                    close(old_pf[0]);
                    close(old_pf[1]);
                    free(pipedComms);
                return status;
            }
        }

        if (sep == DAEMON) {
            daemon = 1;
        }

        if (sep == FILE_PIPE || sep == END_PIPE) {
            if (*current + 2 != comms->size) {
                if ((fd = fopen(comms->data[*current + 2].name, sep == FILE_PIPE ? "w" : "a")) == NULL) {
                    perror("File open failed with error: ");
                    *current += 2;
                    return 1;
                }
                *current += 2;
                file = 1;
            }
        }

    }

    if(strcmp(command.argv[0], "exit") == 0){
        executeExit(comms, 1, command.argc, command.argv);
    }

    if(strcmp(command.argv[0], "cd") == 0){
        return  executeCd(command.argc, command.argv);
    }

    pid_t pid;

    pid = fork();
    if (pid == 0) {
        if (!daemon) {
            if (file) {
                dup2(fileno(fd), STDOUT_FILENO);
            }
        }
        command.argv[command.argc] = NULL;

        if (execvp(command.name, command.argv) == -1) {
            printf("%s: command not found\n", command.name);
            goto dead2;
        }


    } else if (pid == -1) {
        perror("Fork failed with error: ");
    } else {
        if (!daemon) {
            waitpid(pid, &status, 0);
        }
    }
    dead2:
    if (file) {
        fclose(fd);
        fd = NULL;
    }
    return status;
}

int main() {
    while (1) {
        struct Commands coms;
        coms.data = calloc(1024, sizeof(struct Command));
        coms.capacity = 1024;
        coms.size = -1;
        char *line = NULL;
        size_t line_size = 0;
        if(feof(stdin)){
            free(coms.data);
            exit(0);
        }
        if(getline(&line, &line_size, stdin) == -1){
            clearMemory(&coms);
            exit(0);
        }

        int parse = parseInput(&coms, line, line_size);
        free(line);
        if(parse != 0){
            clearMemory(&coms);
            exit(parse);
        }
        coms.size++;
        int globalDaemon = 0;
        for (int i = 0; i < coms.size; ++i) {
            if (coms.data[i].type == COMM) {
                if (i + 1 < coms.size && (coms.data[i + 1].type == AND || coms.data[i + 1].type == OR)) {
                    if (coms.data[coms.size - 1].type == DAEMON) {
                        globalDaemon = 1;
                    }
                }

                int res = executeCommand(&coms, &i, globalDaemon);

                if (i + 1 < coms.size && res == 0 && coms.data[i + 1].type == OR) {
                    while (i + 1 < coms.size && coms.data[i + 1].type != AND) {
                        ++i;
                    }
                    ++i;
                } else if (i + 1 < coms.size && res != 0 && coms.data[i + 1].type == AND) {
                    while (i + 1 < coms.size && coms.data[i + 1].type != OR) {
                        ++i;
                    }
                    ++i;
                }
            }

        }
        clearMemory(&coms);
        if (feof(stdin)) {
            exit(0);
        }
    }
}