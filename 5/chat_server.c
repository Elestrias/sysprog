#include "chat.h"
#include "chat_server.h"
#include <netinet/in.h>
#include <sys/socket.h>
#include "stdio.h"
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "fcntl.h"
#include "sys/epoll.h"


struct Queue{
    struct chat_message* message;
    struct Queue* next;
};
struct Buffer{
    char * buffer;
    int bufferSize;
    int cursor;
};

void initBuffer(struct Buffer * buffer, int size){
    buffer->buffer  = calloc(size, sizeof(char));
    buffer->bufferSize = size;
    buffer->cursor = 0;
}

struct chat_peer {
	int socket;
    struct epoll_event clientEvent;
    int outBufInited;
    struct Buffer outBuf;
    int inBufInited;
    struct  Buffer inBuf;
};

void initClientServer(struct chat_peer* client){
    client->socket  = -1;
    client->inBufInited = 0;
    client->outBufInited = 0;
}

void deinitClientServer(struct  chat_peer* client){
    if(client->socket != -1){
        close(client->socket);
    }

    if(client->inBufInited){
        client->inBufInited = 0;
        client->inBuf.bufferSize = 0;
        free(client->inBuf.buffer);
    }

    if(client->inBufInited){
        client->outBufInited = 0;
        client->outBuf.bufferSize = 0;
        free(client->outBuf.buffer);
    }
}

struct chat_server {

    int socket;
    struct chat_peer* peers;

    struct Queue* head;
    struct Queue* tail;

    int numberOfPeers;
    int peersCapacity;

    int epollStore;
    struct epoll_event serverEvent;

};

struct chat_server *
chat_server_new(void)
{
	struct chat_server *server = calloc(1, sizeof(*server));
	server->socket = -1;
    server->epollStore  = -1;
    server->peersCapacity = 1024;
    server->numberOfPeers = 0;
    server->peers = calloc(1024, sizeof(struct chat_peer));

    server->head = NULL;
    server->tail = NULL;

    for(int i = 0; i < server->peersCapacity; i++){
        initClientServer(server->peers + i);
    }

	return server;
}

void
chat_server_delete(struct chat_server *server)
{
	if (server->socket >= 0)
		close(server->socket);

	// TODO: clear all
    if(server->socket != -1) {
        close(server->socket);
    }
    if(server->epollStore != -1){
        close(server->epollStore);
    }

    for(int i = 0; i < server->numberOfPeers; ++i){
        if(server->peers[i].socket != -1) {
            close(server->peers[i].socket);
            if(server->peers[i].inBufInited){
                free(server->peers[i].inBuf.buffer);
            }

            if(server->peers[i].outBufInited){
                free(server->peers[i].outBuf.buffer);
            }

        }
    }

    while(server->head != NULL){
        struct Queue * node = server->head;
        server->head = node->next;
        chat_message_delete(node->message);
        free(node);
    }

    free(server->peers);
	free(server);
}

int
chat_server_listen(struct chat_server *server, uint16_t port)
{

    if(server->socket != -1){
        return  CHAT_ERR_ALREADY_STARTED;
    }

	struct sockaddr_in addr;
	memset(&addr, 0, sizeof(addr));
	addr.sin_port = htons(port);
	addr.sin_addr.s_addr = htonl(INADDR_ANY);

    server->socket = socket(AF_INET, SOCK_STREAM, 0);
    u_int reuse  = 1;
    int len = sizeof(reuse);

    if(setsockopt(server->socket, SOL_SOCKET, SO_REUSEADDR, &reuse, len) < 0){
        close(server->socket);
        return CHAT_ERR_SYS;
    }

    int flagMask = fcntl(server->socket, F_GETFL, 0);
    flagMask |= O_NONBLOCK;
    if(fcntl(server->socket, F_SETFL, flagMask) < 0){
        close(server->socket);
        server->socket = -1;
        return CHAT_ERR_SYS;
    }

    if(bind(server->socket, (struct sockaddr *)&addr,sizeof(addr)) < 0){
        close(server->socket);
        server->socket = -1;
        return CHAT_ERR_PORT_BUSY;
    }

    if(server->epollStore == -1){
        server->epollStore = epoll_create(1);
    }

    if(listen(server->socket, SOMAXCONN) < 0){
        close(server->socket);
        server->socket = -1;
        close(server->epollStore);
        server->epollStore = -1;
        return  CHAT_ERR_SYS;
    }

    server->serverEvent.events = EPOLLIN;
    server->serverEvent.data.ptr = (void *)server;

    if(epoll_ctl(server->epollStore, EPOLL_CTL_ADD, server->socket, &server->serverEvent) < 0){
        close(server->socket);
        server->socket = -1;
        close(server->epollStore);
        server->epollStore = -1;
        return CHAT_ERR_SYS;
    }

    return 0;
}

struct chat_message *
chat_server_pop_next(struct chat_server *server)
{
    if(server->head == NULL) {
        return NULL;
    }

    struct Queue * cur = server->head;
    server->head = cur->next;

    struct chat_message * message = cur->message;
    free(cur);
    return message;
}

int
chat_server_update(struct chat_server *server, double timeout)
{
    if(server->socket == -1){
        return CHAT_ERR_NOT_STARTED;
    }

    int size = server->numberOfPeers*2 + 1;
    struct epoll_event* events  = calloc(size, sizeof(struct epoll_event));
    int res2 = epoll_wait(server->epollStore, events, size, (int)(timeout * 1000));

    if(res2 == 0){
        free(events);
        return CHAT_ERR_TIMEOUT;
    }

    if(res2 < 0){
        free(events);
        return CHAT_ERR_SYS;
    }

    for(int i = 0; i < res2; ++i){
        if(events[i].data.ptr == server){
            struct sockaddr_in addr;
            socklen_t addr_len = sizeof(addr);

            int client = accept(server->socket, (struct sockaddr *)&addr, &addr_len);

            int flagMask = fcntl(client, F_GETFL, 0);
            flagMask |= O_NONBLOCK;

            if(fcntl(client, F_SETFL, flagMask) < 0){
                free(events);
                return CHAT_ERR_SYS;
            }

            if(client < 0){
                free(events);
                return CHAT_ERR_SYS;
            }

            struct chat_peer * peer = NULL;

            for(int j = 0; j < server->peersCapacity; j++){
                if(server->peers[j].socket == -1){
                    server->peers[j].socket = client;
                    peer = server->peers + j;
                    break;
                }
            }

            if(peer == NULL){
                int current = server->peersCapacity;
                server->peersCapacity *= 2;
                server->peers = realloc(server->peers, sizeof(struct chat_peer) * server->peersCapacity);
                for(int j = current; j < server->peersCapacity; j++){
                    server->peers[server->peersCapacity].socket = -1;
                }

                server->peers[current].socket = client;
                peer = server->peers + current;
            }

            ++server->numberOfPeers;
            peer->clientEvent.events = EPOLLIN;
            peer->clientEvent.data.ptr = (void*)peer;

            if(epoll_ctl(server->epollStore, EPOLL_CTL_ADD, peer->socket, &peer->clientEvent)){
                free(events);
                return CHAT_ERR_SYS;
            }

        }else{
            if(events[i].events & EPOLLIN){
                struct chat_peer* peer = (struct chat_peer *)events[i].data.ptr;
                int recieved = 0;
                char buffer[1024];

                if((recieved = recv(peer->socket, buffer , 1024,  0)) == 0){
                    epoll_ctl(server->epollStore, EPOLL_CTL_DEL,peer->socket,  &peer->clientEvent);
                    deinitClientServer(peer);
                }else{
                    for(int h = 0; h < recieved; h++){

                        if(!peer->inBufInited){
                            initBuffer(&peer->inBuf, 1024);
                            peer->inBufInited = 1;
                        }

                        if(buffer[h] == '\n') {
                            struct Queue *newMsg = calloc(1, sizeof(struct Queue));
                            newMsg->next = NULL;

                            if(peer->inBuf.bufferSize - peer->inBuf.cursor  < 2){
                                peer->inBuf.buffer = realloc(peer->inBuf.buffer, peer->inBuf.bufferSize + 2);
                            }
                            peer->inBuf.buffer[peer->inBuf.cursor] = '\0';

                            newMsg->message = calloc(1, sizeof(struct chat_message));
                            newMsg->message->data = strdup(peer->inBuf.buffer);

                            // HERE QUEUE PUSH
                            if(server->head == NULL){
                                server->head = newMsg;
                                server->tail = newMsg;
                            }else{
                                server->tail->next = newMsg;
                                server->tail = newMsg;
                            }
                            peer->inBuf.buffer[peer->inBuf.cursor++] = '\n';
                            peer->inBuf.buffer[peer->inBuf.cursor] = '\0';

                            for(int z = 0; z < server->peersCapacity; z++){
                                if(server->peers[z].socket != -1 && server->peers[z].socket != peer->socket){

                                    struct chat_peer * destPeer  = server->peers + z;

                                    if(!destPeer->outBufInited){
                                        destPeer->outBufInited = 1;
                                        destPeer->outBuf.buffer = strdup(peer->inBuf.buffer);
                                        destPeer->outBuf.bufferSize = strlen(peer->inBuf.buffer);
                                        destPeer->clientEvent.events |= EPOLLOUT;
                                        int res = epoll_ctl(server->epollStore, EPOLL_CTL_MOD, destPeer->socket, &destPeer->clientEvent);

                                        if(res < 0){
                                            free(events);
                                            return CHAT_ERR_SYS;
                                        }

                                    }else{
                                        destPeer->outBuf.buffer = realloc(destPeer->outBuf.buffer, (destPeer->outBuf.bufferSize + peer->inBuf.bufferSize)*sizeof(char));
                                        strcat(destPeer->outBuf.buffer, peer->inBuf.buffer);
                                        destPeer->outBuf.bufferSize += strlen(peer->inBuf.buffer);
                                    }
                                }
                            }

                            free(peer->inBuf.buffer);
                            peer->inBufInited = 0;
                            peer->inBuf.bufferSize = 0;
                            peer->inBuf.cursor = 0;
                            continue;
                        }

                        if(buffer[h] == '\0'){
                            break;
                        }

                        peer->inBuf.buffer[peer->inBuf.cursor++] = buffer[h];

                        if(peer->inBuf.cursor >= peer->inBuf.bufferSize){
                            peer->inBuf.bufferSize *= 2;
                            peer->inBuf.buffer = realloc(peer->inBuf.buffer, peer->inBuf.bufferSize);
                        }
                    }
                }
                memset(buffer, '\0', 1024);
            }
            if(events->events & EPOLLOUT){
                int sent;
                struct chat_peer * peer = (struct chat_peer *)events[i].data.ptr;
                if((sent = send(peer->socket, peer->outBuf.buffer, peer->outBuf.bufferSize, 0)) < 0){
                    free(events);
                    return CHAT_ERR_SYS;
                }else{
                    if(sent == 0){
                        continue;
                    }else if(sent >= peer->outBuf.bufferSize) {
                        free(peer->outBuf.buffer);
                        peer->outBuf.bufferSize = 0;
                        peer->outBuf.cursor = 0;
                        peer->outBufInited = 0;
                        peer->clientEvent.events ^= EPOLLOUT;
                        int res = epoll_ctl(server->epollStore, EPOLL_CTL_MOD, peer->socket, &peer->clientEvent);

                        if(res < 0){
                            free(events);
                            return CHAT_ERR_SYS;
                        }
                    }else{
                        char * temp = strdup(peer->outBuf.buffer + sent);
                        free(peer->outBuf.buffer);
                        peer->outBuf.bufferSize -= sent;
                        peer->outBuf.buffer = temp;
                    }
                }
            }

        }
    }

    free(events);
    return 0;
}

int
chat_server_get_descriptor(const struct chat_server *server)
{
#if NEED_SERVER_FEED
	/* IMPLEMENT THIS FUNCTION if want +5 points. */

	/*
	 * Server has multiple sockets - own and from connected clients. Hence
	 * you can't return a socket here. But if you are using epoll/kqueue,
	 * then you can return their descriptor. These descriptors can be polled
	 * just like sockets and will return an event when any of their owned
	 * descriptors has any events.
	 *
	 * For example, assume you created an epoll descriptor and added to
	 * there a listen-socket and a few client-sockets. Now if you will call
	 * poll() on the epoll's descriptor, then on return from poll() you can
	 * be sure epoll_wait() can return something useful for some of those
	 * sockets.
	 */
#endif
	(void)server;
	return -1;
}

int
chat_server_get_socket(const struct chat_server *server)
{
	return server->socket;
}

int
chat_server_get_events(const struct chat_server *server)
{
    if(server->socket == -1) {
        return 0;
    }

    int mask = CHAT_EVENT_INPUT;

    for(int i = 0; i < server->peersCapacity; ++i){
        if(server->peers[i].socket != -1 && (server->peers[i].clientEvent.events & EPOLLOUT)){
            mask |= CHAT_EVENT_OUTPUT;
            break;
        }
    }
	return mask;
}

int
chat_server_feed(struct chat_server *server, const char *msg, uint32_t msg_size)
{
#if NEED_SERVER_FEED
	/* IMPLEMENT THIS FUNCTION if want +5 points. */
#endif
	(void)server;
	(void)msg;
	(void)msg_size;
	return CHAT_ERR_NOT_IMPLEMENTED;
}
