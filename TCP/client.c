#define _DEFAULT_SOURCE

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/un.h>
#include <poll.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#include "common.h"

int sock;
char *unix_path = NULL;

char buffer[MAX_MESS_LEN];
char sign;

int board[9] = {0};
int board_taken = 0;

void exit_handler() {
    // wyslanie wiadomosci o zakonczeniu pracy
    sprintf(buffer, "%s", END_CMD);
    send(sock, buffer, MAX_MESS_LEN, MSG_NOSIGNAL);

    // zamkniecie socketu
    shutdown(sock, SHUT_RDWR);
    close(sock);

    printf("\nClient closed.\n");
}

void sigint_handler(int signo) {
    exit(0);
}

void create_inet_socket(char *ip, int port, char *name) {
    // utworzenie socketu INET
    if ((sock = socket(AF_INET, SOCK_STREAM, 0)) == -1) {
        perror("Cannot create INET socket.\n");
        exit(3);
    }

    struct sockaddr_in inet_addr;
    inet_addr.sin_family = AF_INET;
    inet_addr.sin_port = htons(port);
    inet_aton(ip, &inet_addr.sin_addr);

    // polaczenie z serwerem
    if (connect(sock, (struct sockaddr *) &inet_addr, sizeof(inet_addr)) == -1) {
        perror("Cannot connect to INET socket.\n");
        exit(4);
    }

    write(sock, name, MAX_MESS_LEN);
}

void create_unix_socket(char *name) {
    // utworzenie socketu UNIX
    if ((sock = socket(AF_UNIX, SOCK_STREAM, 0)) == -1) {
        perror("Cannot create UNIX socket.\n");
        exit(3);
    }

    struct sockaddr_un unix_addr;
    unix_addr.sun_family = AF_UNIX;
    strcpy(unix_addr.sun_path, unix_path);

    // polaczenie z serwerem
    if (connect(sock, (struct sockaddr *) &unix_addr, sizeof(unix_addr)) == -1) {
        perror("Cannot connect to UNIX socket.\n");
        exit(4);
    }

    write(sock, name, MAX_MESS_LEN);
}

// odczytanie i zinterpretowanie wiadomosci zwrotnej
void connect_result() {
    read(sock, buffer, MAX_MESS_LEN);

    if (strcmp(buffer, EXISTS_CMD) == 0) {
        printf("Name already taken.\n");
        exit(5);
    }
    else if (strcmp(buffer, MAX_CMD) == 0) {
        printf("Reached max limit of connected clients.\n");
        exit(5);
    }
    else if (strcmp(buffer, CONNECTED_CMD) == 0) {
        printf("Connected to server.\n");
    }
    else if (strcmp(buffer, WAIT_CMD) == 0) {
        printf("Connected to server. Waiting for opponent.\n");
    }
    else {
        printf("Unknown response from server.\n");
        exit(6);
    }
}

void setup_connection(char *argv[]) {
    // uruchomienie polaczenia
    if (strcmp(argv[2], "unix") == 0) {
        unix_path = argv[3];
        create_unix_socket(argv[1]);
    }
    else if (strcmp(argv[2], "inet") == 0) {
        char *endprt;
        int port = strtol(argv[4], &endprt, 10);

        if (endprt == argv[1]) {
            printf("Invalid number of port.\n");
            exit(2);
        }

        create_inet_socket(argv[3], port, argv[1]);
    }
    else {
        printf("Incorrect method of connection.\n");
        exit(2);
    }

    // odczytanie wiadomosci zwrotnej
    connect_result();
}

void print_board() {
    printf("\n");

    for (int i = 0; i < 9; ++i) {
        if (board[i] == 0)
            printf(" ");
        else if (board[i] == 1)
            printf("X");
        else if (board[i] == 2)
            printf("O");

        if (i % 3 == 2 && i < 8)
            printf("\n-----\n");
        else if (i < 8)
            printf("|");
    }

    printf("\n\n");
}

// sprawdzenie czy nastapila wygrana lub przegrana
void check_winner(int value) {
    if (value == 0)
        return;

    if ((value == 1 && sign == 'X') || (value == 2 && sign == 'O'))
        printf("You are the winner!!!\n");
    else
        printf("You lost.\n");

    exit(0);
}

// wyszukiwanie na planszy trzech znakow w jednej linii
void check_board() {
    for (int i = 0; i < 3; ++i) {
        if (board[3 * i] == board[3 * i + 1] && board[3 * i] == board[3 * i + 2])
            check_winner(board[3 * i]);

        if (board[i] == board[i + 3] && board[i] == board[i + 6])
            check_winner(board[i]);
    }

    if (board[0] == board[4] && board[0] == board[8])
        check_winner(board[0]);

    if (board[2] == board[4] && board[2] == board[6])
        check_winner(board[2]);

    if (board_taken == 9) {
        printf("Draw.\n");
        exit(0);
    }
}

void handle_message(bool *wait) {
    // odczytanie wiadomosci
    read(sock, buffer, MAX_MESS_LEN);
    char *command = strtok(buffer, "|");

    if (strcmp(command, PING_CMD) == 0) {
        // wyslanie odpowiedzi na ping
        write(sock, buffer, MAX_MESS_LEN);
    }
    else if (strcmp(command, GAME_CMD) == 0) {
        // rozpoczecie gry i wypisanie na ekran informacji
        char *name = strtok(NULL, "|");
        sign = strtok(NULL, "|")[0];
        printf("Game started!\n\nYour opponent is \"%s\".\nYour sign is %c.\n\n", name, sign);
        printf("Enter number between 1 and 9 to make a move.\n");
        printf("Here is the board with according numbers.\n\n");

        for (int i = 0; i < 9; ++i) {
            printf("%d", i + 1);

            if (i % 3 == 2 && i < 8)
                printf("\n-----\n");
            else if (i < 8)
                printf("|");
        }

        if (sign == 'X') {
            printf("\n\nYou start the game!\n\n");
            *wait = false;
        }
        else {
            printf("\n\nYour opponent starts the game!\n\n");
            *wait = true;
        }
    }
    else if (strcmp(command, SEND_CMD) == 0) {
        // odebranie ruchu przeciwnika
        char *value = strtok(NULL, "|");
        board[value[0] - '1'] = sign == 'X' ? 2 : 1;
        *wait = false;

        // wypisanie planszy i sprawdzenie wygranej
        board_taken++;
        print_board();
        check_board();
    }
    else if (strcmp(command, END_CMD) == 0) {
        // odebranie sygnalu zakonczenia dzialania
        exit(0);
    }
}

void handle_input(bool *wait) {
    // pobranie wejscia z klawiatury
    char value[MAX_MESS_LEN];
    scanf("%s", value);

    if (!*wait && strlen(value) == 1 && value[0] >= '1' && value[0] <= '9') {
        // jest kolej klienta oraz wejscie poprawne
        if (board[value[0] - '1'] != 0) {
            printf("Field already taken. Choose another one.\n\n");
        }
        else {
            // wyslanie do serwera informacji o dokonanym ruchu
            sprintf(buffer, "%s|%c", SEND_CMD, value[0]);
            write(sock, buffer, MAX_MESS_LEN);
            board[value[0] - '1'] = sign == 'X' ? 1 : 2;
            *wait = true;

            // wypisanie planszy i sprawdzenie wygranej
            board_taken++;
            print_board();
            check_board();
        }
    }
    else if (!*wait) {
        printf("Incorrect input.\n\n");
    }
    else {
        printf("It is not your turn.\n\n");
    }
}

int main(int argc, char *argv[]) {
    if (argc < 4 || argc > 5) {
        printf("Invalid number of arguments.\n");
        printf("Usage: ./client <name> <method> [<ip> <port> | <path>]\n");
        exit(1);
    }

    // ustawienie klienta oraz nawiazanie polaczenia
    atexit(exit_handler);
    signal(SIGINT, sigint_handler);

    setup_connection(argv);

    struct pollfd fds[2];
    fds[0].fd = sock;
    fds[1].fd = STDIN_FILENO;
    fds[0].events = fds[1].events = POLLIN;
    bool wait = true;

    // glowna petla programu
    while (1) {
        poll(fds, 2, -1);

        if (fds[0].revents & POLLIN)
            handle_message(&wait);
        else if (fds[1].revents & POLLIN)
            handle_input(&wait);
    }

    return 0;
}