#include "chat.h"
#include "chat_client.h"

#include <stdlib.h>
#include <unistd.h>
#include "sys/socket.h"
#include "string.h"
#include "netdb.h"
#include "stdio.h"
#include "sys/poll.h"
#include <ctype.h>


struct Buffer{
    char * buffer;
    int bufferSize;
    int cursor;
};


struct Queue{
    struct chat_message * message;
    struct Queue * next;
};

struct chat_client {
	/** Socket connected to the server. */
	int socket;
	/** Array of received messages. */
	/* ... */
	/** Output buffer. */
    struct Queue* head;
    struct Queue* tail;
    struct Buffer inBuffer;
    int InitInBuf;
    int InitOutBuf;
    struct Buffer outBuffer;

    struct pollfd clientPoll;

	/* PUT HERE OTHER MEMBERS */
};

void initClient(struct chat_client* client){
    client->socket = -1;
    client->clientPoll.fd = -1;
    client->InitInBuf = 0;
    client->InitOutBuf = 0;
    client->head = NULL;
    client->tail = NULL;
}


struct chat_client *
chat_client_new(const char *name)
{
	(void)name;
	struct chat_client *client = calloc(1, sizeof(*client));
    initClient(client);

	return client;
}

void clear_client_data(struct  chat_client* client){
    if (client->socket >= 0) {
        close(client->socket);
        client->socket = -1;
    }
    client->clientPoll.fd = -1;

    if(client->InitOutBuf){
        free(client->outBuffer.buffer);
        client->outBuffer.bufferSize = 0;
        client->outBuffer.cursor = 0;
    }

    if(client->InitInBuf){
        free(client->inBuffer.buffer);
        client->inBuffer.bufferSize = 0;
        client->inBuffer.cursor = 0;
    }

    while(client->head != NULL){
        struct Queue * msg = client->head;
        client->head = msg->next;
        chat_message_delete(msg->message);
        free(msg);
    }

}
void
chat_client_delete(struct chat_client *client)
{
    clear_client_data(client);
	free(client);
}

int
chat_client_connect(struct chat_client *client, const char *addr)
{
	/*
	 * 1) Use getaddrinfo() to resolve addr to struct sockaddr_in.
	 * 2) Create a client socket (function socket()).
	 * 3) Connect it by the found address (function connect()).
	 */
	/* IMPLEMENT THIS FUNCTION */

    if (client->socket != -1){
        return CHAT_ERR_ALREADY_STARTED;
    }

    struct addrinfo hints;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_flags = 0;
    hints.ai_protocol = 0;

    struct addrinfo *result;

    char *temp_addr = strdup(addr);
    char *tokens = strtok(temp_addr, ":");
    char *host = tokens;
    tokens = strtok(NULL, ":");
    char *port = tokens;

    if(getaddrinfo(host, port, &hints, &result) < 0){
        free(temp_addr);
        return CHAT_ERR_NO_ADDR;
    }

    int sockDesc;
    if ((sockDesc = socket(result->ai_family, result->ai_socktype, result->ai_protocol)) == -1) {
        freeaddrinfo(result);
        free(temp_addr);
        return CHAT_ERR_SYS;
    }

    if (connect(sockDesc, result->ai_addr, result->ai_addrlen) == -1) {
        freeaddrinfo(result);
        close(client->socket);
        client->socket = -1;
        free(temp_addr);
        return CHAT_ERR_SYS;
    }

    client->socket = sockDesc;
    client->clientPoll.fd = client->socket;
    client->clientPoll.events = POLLIN;

    freeaddrinfo(result);
    free(temp_addr);
    return 0;
}

struct chat_message *
chat_client_pop_next(struct chat_client *client)
{
	if(client->head != NULL){
        struct Queue * node = client->head;
        client->head = node->next;
        struct chat_message * msg = node->message;
        free(node);
        return msg;
    }
	return NULL;
}

int
chat_client_update(struct chat_client *client, double timeout)
{
    if(client->socket == -1){
        return CHAT_ERR_NOT_STARTED;
    }
    struct pollfd copyFd = client->clientPoll;
    copyFd.revents = 0;
    int result = poll(&copyFd, 1, (int)(timeout * 1000));

    if(result <= 0){
        return CHAT_ERR_TIMEOUT;
    }

    if(copyFd.revents & POLLIN){
        char buffer[1024];
        int recieved;
        if ((recieved = recv(client->socket, buffer, sizeof (buffer), MSG_DONTWAIT)) < 0){
            return CHAT_ERR_SYS;
        }else{

            if(recieved == 0){
                clear_client_data(client);
                return CHAT_ERR_NOT_STARTED;
            }

            for(int i = 0; i < recieved; ++i){
                if(!client->InitOutBuf){
                    client->outBuffer.buffer = calloc(1024, sizeof(char));
                    client->outBuffer.bufferSize = 1024;
                    client->InitOutBuf = 1;
                    client->outBuffer.cursor = 0;
                }

                if(buffer[i] == '\n'){
                    struct chat_message * msg = calloc(1, sizeof(struct chat_message));
                    if(client->outBuffer.cursor >= client->outBuffer.bufferSize){
                        client->outBuffer.buffer = realloc(client->outBuffer.buffer, sizeof(char) * (client->outBuffer.bufferSize + 2));
                    }
                    client->outBuffer.buffer[client->outBuffer.cursor] = '\0';
                    msg->data = strdup(client->outBuffer.buffer);

                    struct Queue * newNode = calloc(1, sizeof (struct Queue));
                    newNode->next = NULL;

                    newNode->message  = msg;

                    if(client->head == NULL){
                        client->head = newNode;
                        client->tail = newNode;
                    }else{
                        client->tail->next = newNode;
                        client->tail = newNode;
                    }

                    free(client->outBuffer.buffer);
                    client->outBuffer.cursor = 0;
                    client->outBuffer.bufferSize = 0;
                    client->InitOutBuf = 0;
                    continue;
                }

                client->outBuffer.buffer[client->outBuffer.cursor++] = buffer[i];

                if(client->outBuffer.cursor >= client->outBuffer.bufferSize){
                    client->outBuffer.bufferSize *= 2;
                    client->outBuffer.buffer = realloc(client->outBuffer.buffer, sizeof(char) * client->outBuffer.bufferSize);
                }
            }
        }
    }

    if(copyFd.revents & POLLOUT){
        int sent;
        if(!client->InitInBuf){
            client->clientPoll.events ^= POLLOUT;
            return 0;
        }

        if((sent = send(client->socket, client->inBuffer.buffer, client->inBuffer.bufferSize, 0))  < 0){
            return CHAT_ERR_SYS;
        }else{
            if(sent < client->inBuffer.bufferSize) {
                char *newBuf = strdup(client->inBuffer.buffer + sent);
                free(client->inBuffer.buffer);

                client->inBuffer.buffer = newBuf;
                client->inBuffer.bufferSize -= sent;
            }else{
                free(client->inBuffer.buffer);
                client->inBuffer.cursor = 0;
                client->inBuffer.bufferSize = 0;
                client->InitInBuf = 0;
                client->clientPoll.events ^= POLLOUT;
            }
        }
    }

	return 0;
}

int
chat_client_get_descriptor(const struct chat_client *client)
{
	return client->socket;
}

int
chat_client_get_events(const struct chat_client *client)
{

    if(client->socket == -1){
        return 0;
    }

    if(client->InitInBuf){
        return CHAT_EVENT_INPUT | CHAT_EVENT_OUTPUT;
    }
	return CHAT_EVENT_INPUT;
}

int
chat_client_feed(struct chat_client *client, const char *msg, uint32_t msg_size)
{
    if(client->socket == -1){
        return CHAT_ERR_NOT_STARTED;
    }

    uint32_t msgCapacity = 1024;
    uint32_t currentSize = 0;

    char* currentMessage = calloc(msgCapacity, sizeof(char));

    for(uint32_t i = 0; i < msg_size; ++i){
        currentMessage[currentSize++] = msg[i];

        if(currentSize >= msgCapacity){
            msgCapacity *= 2;
            currentMessage = realloc(currentMessage, msgCapacity*sizeof(char));
        }

        if(msg[i] == '\n') {
            currentMessage[currentSize] = '\0';

            uint32_t start = 0;
            for(; isspace(currentMessage[start]) && start < currentSize; ++start);
            int end = (int)currentSize - 1;
            for(; (currentMessage[end] == '\0' || isspace(currentMessage[end])) && end >= 0; --end);
            if (end < 0) {
                return 0;
            }

            uint32_t new_msg_size = end - start + 2;
            char *new_msg = calloc(new_msg_size, sizeof(char));
            strncpy(new_msg, currentMessage + start, end - start + 1);
            new_msg[new_msg_size - 1] = '\n';

            free(currentMessage);
            currentSize = 0;
            msgCapacity = 1024;
            currentMessage = calloc(msgCapacity, sizeof(char));

            if(!client->InitInBuf){
                client->inBuffer.buffer = calloc(new_msg_size + 1, sizeof(char));
                strncpy(client->inBuffer.buffer, new_msg, new_msg_size);

                client->inBuffer.bufferSize = new_msg_size;
                client->clientPoll.events |= POLLOUT;
                client->InitInBuf = 1;
            }else{
                client->inBuffer.buffer = realloc(client->inBuffer.buffer, (new_msg_size + client->inBuffer.bufferSize)*sizeof(char));
                client->inBuffer.bufferSize += new_msg_size;
                strncat(client->inBuffer.buffer, new_msg, new_msg_size);
                client->inBuffer.bufferSize += new_msg_size;
            }

            free(new_msg);
        }
    }

    if(currentSize != 0){
        currentMessage[currentSize] = '\0';

        uint32_t start = 0;
        for(; isspace(currentMessage[start]) && start < currentSize; ++start);
        int end = (int)currentSize - 1;
        for(; (currentMessage[end] == '\0' || isspace(currentMessage[end])) && end >= 0; --end);
        if (end < 0) {
            return 0;
        }

        uint32_t new_msg_size = end - start + 2;
        char *new_msg = calloc(new_msg_size, sizeof(char));
        strncpy(new_msg, currentMessage + start, end - start + 1);
        free(currentMessage);

        if(!client->InitInBuf){
            client->inBuffer.buffer = calloc(new_msg_size + 1, sizeof(char));
            strncpy(client->inBuffer.buffer, new_msg, new_msg_size);

            client->inBuffer.bufferSize = new_msg_size;
            client->clientPoll.events |= POLLOUT;
            client->InitInBuf = 1;
        }else{
            client->inBuffer.buffer = realloc(client->inBuffer.buffer, (new_msg_size + client->inBuffer.bufferSize)*sizeof(char));
            client->inBuffer.bufferSize += new_msg_size;
            strncat(client->inBuffer.buffer, new_msg, new_msg_size);
            client->inBuffer.bufferSize += new_msg_size;
        }
        free(new_msg);
    }

    return 0;
}
