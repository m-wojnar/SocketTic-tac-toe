#define _DEFAULT_SOURCE

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <unistd.h>
#include <pthread.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/un.h>
#include <poll.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#include "common.h"

struct client_t {
    int sock_fd;
    int opponent_id;
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
    send(clients[id].sock_fd, buffer, MAX_MESS_LEN, MSG_NOSIGNAL);

    // zamkniecie socketu
    shutdown(clients[id].sock_fd, SHUT_RDWR);
    close(clients[id].sock_fd);

    // wyzerowanie struktury klienta
    clients[id].sock_fd = 0;
    clients[id].opponent_id = 0;
    free(clients[id].name);

    clients_size--;
}

void exit_handler() {
    // zamkniecie socketow
    shutdown(inet_sock, SHUT_RDWR);
    close(inet_sock);

    shutdown(unix_sock, SHUT_RDWR);
    close(unix_sock);
    unlink(unix_path);

    // usuniecie polaczenia z klientami
    for (int i = 0; i < MAX_CLIENTS; ++i) {
        if (clients[i].sock_fd != 0)
            remove_client(i);
    }

    pthread_mutex_destroy(&mutex);
    printf("Server closed.\n");
}

void sigint_handler(int signo) {
    exit(0);
}

void create_inet_socket(int port) {
    // utworzenie socketu INET
    if ((inet_sock = socket(AF_INET, SOCK_STREAM, 0)) == -1) {
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

    // nasluchiwanie na porcie
    if (listen(inet_sock, MAX_CLIENTS) == -1) {
        perror("Listening on INET socket failed.\n");
        exit(5);
    }

    printf("Listening INET at %s:%d\n", inet_ntoa(inet_addr.sin_addr), port);
}

void create_unix_socket() {
    // utworzenie socketu UNIX
    if ((unix_sock = socket(AF_UNIX, SOCK_STREAM, 0)) == -1) {
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

    // nasluchiwanie
    if (listen(unix_sock, MAX_CLIENTS) == -1) {
        perror("Listening on UNIX socket failed.\n");
        exit(5);
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
                send(clients[i].sock_fd, buffer, MAX_MESS_LEN, MSG_NOSIGNAL);
                clients[i].is_connected = false;
            }
        }

        pthread_mutex_unlock(&mutex);
        sleep(5);
    }
}

int poll_sockets() {
    pthread_mutex_lock(&mutex);
    int size = clients_size;

    // ustawienie struktury dla socketow serwera oraz zarejestrowanych klientow
    struct pollfd fds[size + 2];
    fds[0].fd = inet_sock;
    fds[1].fd = unix_sock;
    fds[0].events = fds[1].events = POLLIN;

    int j = 2;
    for (int i = 0; i < MAX_CLIENTS; ++i) {
        if (clients[i].sock_fd != 0) {
            fds[j].fd = clients[i].sock_fd;
            fds[j].events = POLLIN;
            j++;
        }
    }
    pthread_mutex_unlock(&mutex);

    // oczekiwanie na przychodzaca wiadomosc
    poll(fds, size + 2, -1);

    // odczytanie numeru socketu, z ktorego nadeszla wiadomosc
    for (int i = 0; i < size + 2; ++i) {
        if (fds[i].revents & POLLIN)
            return fds[i].fd;
    }

    return -1;
}

void connect_clients(int id) {
    // wyszukiwanie drugiego wolnego klienta
    for (int i = 0; i < MAX_CLIENTS; ++i) {
        if (i != id && clients[i].sock_fd != 0 && clients[i].opponent_id == -1) {
            // wyslanie wiadomosci do pierwszego klienta
            int client_key = rand() % 2;
            clients[id].opponent_id = i;
            sprintf(buffer, "%s|%s|%c", GAME_CMD, clients[i].name, client_key ? 'X' : 'O');
            send(clients[id].sock_fd, buffer, MAX_MESS_LEN, MSG_NOSIGNAL);

            // wyslanie wiadomosci do drugiego klienta
            client_key = (client_key + 1) % 2;
            clients[i].opponent_id = id;
            sprintf(buffer, "%s|%s|%c", GAME_CMD, clients[id].name, client_key ? 'X' : 'O');
            send(clients[i].sock_fd, buffer, MAX_MESS_LEN, MSG_NOSIGNAL);

            printf("Clients \"%s\" and \"%s\" paired.\n", clients[i].name, clients[id].name);
            return;
        }
    }
}

void accept_new_client(int sock) {
    // zaakceptowanie polaczenia i odczytanie nazwy wybranej przez klienta
    sock = accept(sock, NULL, NULL);
    read(sock, buffer, MAX_MESS_LEN);

    pthread_mutex_lock(&mutex);
    // sprawdzenie czy nazwa jest zarezerwowana
    for (int i = 0; i < MAX_CLIENTS; ++i) {
        if (clients[i].sock_fd != 0 && strcmp(clients[i].name, buffer) == 0) {
            printf("Existing name \"%s\".\n", buffer);
            sprintf(buffer, "%s", EXISTS_CMD);
            write(sock, buffer, MAX_MESS_LEN);

            shutdown(sock, SHUT_RDWR);
            close(sock);
            pthread_mutex_unlock(&mutex);
            return;
        }
    }

    // wyszukiwanie wolnego miejsca dla klienta
    for (int i = 0; i < MAX_CLIENTS; ++i) {
        if (clients[i].sock_fd == 0) {
            clients[i].sock_fd = sock;
            clients[i].opponent_id = -1;
            clients[i].is_connected = true;
            clients[i].name = calloc(strlen(buffer) + 1, sizeof(char));
            strcpy(clients[i].name, buffer);
            clients_size++;

            printf("Connected with \"%s\".\n", buffer);

            if (clients_size % 2 == 0)
                sprintf(buffer, "%s", CONNECTED_CMD);
            else
                sprintf(buffer, "%s", WAIT_CMD);

            write(clients[i].sock_fd, buffer, MAX_MESS_LEN);
            connect_clients(i);

            pthread_mutex_unlock(&mutex);
            return;
        }
    }
    pthread_mutex_unlock(&mutex);

    // osiagnieto maksymalna liczbe polaczonych klientow
    sprintf(buffer, "%s", MAX_CMD);
    write(sock, buffer, MAX_MESS_LEN);
    shutdown(sock, SHUT_RDWR);
    close(sock);

    printf("Max clients connected.\n");
}

// wyszukiwanie id klienta o danym deskryptorze socketa
int find_by_fd(int sock) {
    for (int i = 0; i < MAX_CLIENTS; ++i) {
        if (clients[i].sock_fd == sock)
            return i;
    }

    return -1;
}

void handle_message(int sock) {
    // odczytanie wiadomosci od klienta
    read(sock, buffer, MAX_MESS_LEN);
    char *command = strtok(buffer, "|");

    pthread_mutex_lock(&mutex);
    int id = find_by_fd(sock);
    int opponent_id = clients[id].opponent_id;

    if (strcmp(command, PING_CMD) == 0) {
        // wiadomosc zwrotna na ping
        clients[id].is_connected = true;
    }
    else if (strcmp(command, SEND_CMD) == 0) {
        // wykonanie ruchu przez jednego z klientow
        char *value = strtok(NULL, "|");

        if (opponent_id != -1) {
            sprintf(buffer, "%s|%s", SEND_CMD, value);
            write(clients[opponent_id].sock_fd, buffer, MAX_MESS_LEN);
            printf("\"%s\" send %s to \"%s\".\n", clients[id].name, value, clients[opponent_id].name);
        }
        else {
            printf("\"%s\" send %s.\n", clients[id].name, value);
        }
    }
    else if (strcmp(command, END_CMD) == 0) {
        // odlaczenie klienta
        printf("Removed client \"%s\".\n", clients[id].name);
        remove_client(id);

        if (opponent_id != -1) {
            printf("Removed opponent \"%s\".\n", clients[opponent_id].name);
            remove_client(opponent_id);
        }
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

        if (sock == inet_sock || sock == unix_sock)
            accept_new_client(sock);
        else if (sock != -1)
            handle_message(sock);
    }

    return 0;
}
