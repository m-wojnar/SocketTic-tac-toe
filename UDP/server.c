#define _DEFAULT_SOURCE

#include <arpa/inet.h>
#include <netinet/in.h>
#include <poll.h>
#include <pthread.h>
#include <signal.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/un.h>
#include <unistd.h>

#include "common.h"

struct client_t {
    int sock_fd;
    int opponent_id;
    socklen_t addr_len;
    struct sockaddr *addr;
    bool is_connected;
    char *name;
};

struct client_t clients[MAX_CLIENTS];
int clients_size = 0;

int inet_sock, unix_sock;
char *unix_path;

pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;
char buffer[MAX_MESS_LEN];

void remove_client(int id) {
    // wyslanie do klienta informacji o zakonczeniu polaczenia
    sprintf(buffer, "%s", END_CMD);
    sendto(clients[id].sock_fd, buffer, MAX_MESS_LEN, 0, clients[id].addr, clients[id].addr_len);

    // wyzerowanie struktury klienta
    free(clients[id].name);
    free(clients[id].addr);
    memset(&clients[id], 0, sizeof(struct client_t));

    clients_size--;
}

void exit_handler() {
    // usuniecie polaczenia z klientami
    for (int i = 0; i < MAX_CLIENTS; ++i) {
        if (clients[i].sock_fd != 0)
            remove_client(i);
    }

    // zamkniecie socketow
    close(inet_sock);
    close(unix_sock);
    unlink(unix_path);

    pthread_mutex_destroy(&mutex);
    printf("Server closed.\n");
}

void sigint_handler(int signo) {
    exit(0);
}

void create_inet_socket(int port) {
    // utworzenie socketu INET
    if ((inet_sock = socket(AF_INET, SOCK_DGRAM, 0)) == -1) {
        perror("Cannot create INET socket.\n");
        exit(3);
    }

    struct sockaddr_in inet_addr;
    inet_addr.sin_family = AF_INET;
    inet_addr.sin_port = htons(port);
    inet_addr.sin_addr.s_addr = INADDR_ANY;

    if (bind(inet_sock, (struct sockaddr *) &inet_addr, sizeof(inet_addr)) == -1) {
        perror("Cannot bind INET socket.\n");
        exit(4);
    }

    printf("Listening INET at %s:%d\n", inet_ntoa(inet_addr.sin_addr), port);
}

void create_unix_socket() {
    // utworzenie socketu UNIX
    if ((unix_sock = socket(AF_UNIX, SOCK_DGRAM, 0)) == -1) {
        perror("Cannot create UNIX socket.\n");
        exit(3);
    }

    struct sockaddr_un unix_addr;
    unix_addr.sun_family = AF_UNIX;
    strcpy(unix_addr.sun_path, unix_path);

    if (bind(unix_sock, (struct sockaddr *) &unix_addr, sizeof(unix_addr)) == -1) {
        perror("Cannot bind UNIX socket.\n");
        exit(4);
    }

    printf("Listening UNIX at %s\n", unix_path);
}

void *ping_function(void *arg) {
    while (1) {
        printf("PING\n");
        pthread_mutex_lock(&mutex);
        sprintf(buffer, "%s", PING_CMD);

        for (int i = 0; i < MAX_CLIENTS; ++i) {
            if (clients[i].sock_fd != 0 && !clients[i].is_connected) {
                // usuniecie klientow, ktorzy nie odpowiedzieli na ping
                int opponent_id = clients[i].opponent_id;
                printf("Removed inactive client \"%s\".\n", clients[i].name);
                remove_client(i);

                if (opponent_id != -1) {
                    printf("Removed opponent \"%s\".\n", clients[opponent_id].name);
                    remove_client(opponent_id);
                }
            }
            else if (clients[i].sock_fd != 0) {
                // wyslanie sygnalu ping do zarejestrowanych klientow
                sendto(clients[i].sock_fd, buffer, MAX_MESS_LEN, 0, clients[i].addr, clients[i].addr_len);
                clients[i].is_connected = false;
            }
        }

        pthread_mutex_unlock(&mutex);
        sleep(5);
    }
}

int poll_sockets() {
    // ustawienie struktury dla socketow serwera
    struct pollfd fds[2];
    fds[0].fd = inet_sock;
    fds[1].fd = unix_sock;
    fds[0].events = fds[1].events = POLLIN;

    // oczekiwanie na przychodzaca wiadomosc
    poll(fds, 2, -1);

    // odczytanie numeru socketu, z ktorego nadeszla wiadomosc
    return fds[0].revents & POLLIN ? inet_sock : unix_sock;
}

void connect_clients(int id) {
    // wyszukiwanie drugiego wolnego klienta
    for (int i = 0; i < MAX_CLIENTS; ++i) {
        if (i != id && clients[i].sock_fd != 0 && clients[i].opponent_id == -1) {
            // wyslanie wiadomosci do pierwszego klienta
            int client_key = rand() % 2;
            clients[id].opponent_id = i;
            sprintf(buffer, "%s|%s|%c", GAME_CMD, clients[i].name, client_key ? 'X' : 'O');
            sendto(clients[id].sock_fd, buffer, MAX_MESS_LEN, 0, clients[id].addr, clients[id].addr_len);

            // wyslanie wiadomosci do drugiego klienta
            client_key = (client_key + 1) % 2;
            clients[i].opponent_id = id;
            sprintf(buffer, "%s|%s|%c", GAME_CMD, clients[id].name, client_key ? 'X' : 'O');
            sendto(clients[i].sock_fd, buffer, MAX_MESS_LEN, 0, clients[i].addr, clients[i].addr_len);

            printf("Clients \"%s\" and \"%s\" paired.\n", clients[i].name, clients[id].name);
            return;
        }
    }
}

void accept_new_client(int sock, char *name, struct sockaddr *addr, socklen_t addr_len) {
    // sprawdzenie czy nazwa jest zarezerwowana
    for (int i = 0; i < MAX_CLIENTS; ++i) {
        if (clients[i].sock_fd != 0 && strcmp(clients[i].name, name) == 0) {
            printf("Existing name \"%s\".\n", name);
            sprintf(buffer, "%s", EXISTS_CMD);
            sendto(sock, buffer, MAX_MESS_LEN, 0, addr, addr_len);
            return;
        }
    }

    // wyszukiwanie wolnego miejsca dla klienta
    for (int i = 0; i < MAX_CLIENTS; ++i) {
        if (clients[i].sock_fd == 0) {
            clients[i].sock_fd = sock;
            clients[i].opponent_id = -1;
            clients[i].is_connected = true;
            clients[i].addr = calloc(1, addr_len);
            memcpy(clients[i].addr, addr, addr_len);
            clients[i].addr_len = addr_len;
            clients[i].name = calloc(strlen(name) + 1, sizeof(char));
            strcpy(clients[i].name, name);
            clients_size++;

            printf("Connected with \"%s\".\n", name);

            if (clients_size % 2 == 0)
                sprintf(buffer, "%s", CONNECTED_CMD);
            else
                sprintf(buffer, "%s", WAIT_CMD);

            sendto(sock, buffer, MAX_MESS_LEN, 0, addr, addr_len);
            connect_clients(i);
            return;
        }
    }

    // osiagnieto maksymalna liczbe polaczonych klientow
    sprintf(buffer, "%s", MAX_CMD);
    sendto(sock, buffer, MAX_MESS_LEN, 0, addr, addr_len);
    printf("Max clients connected.\n");
}

// wyszukiwanie id klienta o danym deskryptorze socketa
int find_id(int sock, struct sockaddr *addr) {
    for (int i = 0; i < MAX_CLIENTS; ++i) {
        if (clients[i].sock_fd == sock && memcmp(clients[i].addr, addr, clients[i].addr_len) == 0)
            return i;
    }

    return -1;
}

void handle_message(int sock) {
    socklen_t addr_len = sock == inet_sock ? sizeof(struct sockaddr_in) : sizeof(struct sockaddr_un);
    struct sockaddr addr;

    // odczytanie wiadomosci od klienta
    pthread_mutex_lock(&mutex);
    recvfrom(sock, buffer, MAX_MESS_LEN, 0, &addr, &addr_len);
    char *command = strtok(buffer, "|");

    if (strcmp(command, PING_CMD) == 0) {
        // wiadomosc zwrotna na ping
        int id = find_id(sock, &addr);
        clients[id].is_connected = true;
    }
    else if (strcmp(command, SEND_CMD) == 0) {
        // wykonanie ruchu przez jednego z klientow
        char *value = strtok(NULL, "|");

        int id = find_id(sock, &addr);
        int opponent_id = clients[id].opponent_id;

        if (opponent_id != -1) {
            sprintf(buffer, "%s|%s", SEND_CMD, value);
            sendto(clients[opponent_id].sock_fd, buffer, MAX_MESS_LEN, 0, clients[opponent_id].addr, clients[opponent_id].addr_len);
            printf("\"%s\" send %s to \"%s\".\n", clients[id].name, value, clients[opponent_id].name);
        }
        else {
            printf("\"%s\" send %s.\n", clients[id].name, value);
        }
    }
    else if (strcmp(command, END_CMD) == 0) {
        // odlaczenie klienta
        int id = find_id(sock, &addr);
        if (id == -1) {
            pthread_mutex_unlock(&mutex);
            return;
        }

        int opponent_id = clients[id].opponent_id;

        printf("Removed client \"%s\".\n", clients[id].name);
        remove_client(id);

        if (opponent_id != -1) {
            printf("Removed opponent \"%s\".\n", clients[opponent_id].name);
            remove_client(opponent_id);
        }
    }
    else {
        accept_new_client(sock, command, &addr, addr_len);
    }

    pthread_mutex_unlock(&mutex);
}

int main(int argc, char *argv[]) {
    if (argc != 3) {
        printf("Invalid number of arguments.\n");
        printf("Usage: ./server <port> <path>\n");
        exit(1);
    }

    // ustawienie serwera i otworzenie socketow
    unix_path = argv[2];
    char *endprt;
    int port = strtol(argv[1], &endprt, 10);

    if (endprt == argv[1]) {
        printf("Invalid number of port.\n");
        exit(2);
    }

    srand(time(NULL));
    memset(clients, 0, MAX_CLIENTS * sizeof(struct client_t));

    atexit(exit_handler);
    signal(SIGINT, sigint_handler);

    create_inet_socket(port);
    create_unix_socket();
    printf("Server started.\n");

    // uruchomienie watku do pingowania
    pthread_t tid;
    pthread_create(&tid, NULL, ping_function, NULL);

    // glowna petla programu
    while (1) {
        int sock = poll_sockets();

        if (sock != -1)
            handle_message(sock);
    }

    return 0;
}