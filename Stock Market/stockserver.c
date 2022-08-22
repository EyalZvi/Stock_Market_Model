// Stock Server Source Code:

// Includes
// -------------------
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <sys/time.h>
#include <sys/select.h>
#include <pthread.h>

// Defines:
// -------------------
#define MAX_CLIENTS 50
#define STOCK_COUNT 10
#define STOCK_SIZE 38
#define STOCK_UPDATE_SIZE 49
#define BufferSize 100
#define MAX_COMPANY_NAME 20
#define MAX_INVEST_SIZE 11
#define LOGIN_FIELD_SIZE 8
#define MAX_PPU 10000

// USER STRUCTS
typedef struct MyStock
{
    // Struct Of A Specific Stock (shared with the users)
    char *StockName;                // Stock Name
    int units;                      // Owned units
    float buying_ppu;               // Buying price per unit
    float current_ppu;              // Current price per unit
} MyStock;

typedef struct User
{
    // Struct Of A Specific client
    char username[9];               // Username
    char password[9];               // Password
    int isLoggedIn;                 // Preventing more than one connection to the same username
    MyStock portfolio[10];          // Array of user's stocks
    float balance_sheet;            // current balance of the client
} User;

// BUY/SELL STRUCTS
typedef struct Offer
{
    // Struct Of A Representation Of Buy/Sell Offer
    int Units;                      // Offer's Units
    float ppu;                      // Offer's Price Per Unit
} Offer;

typedef struct Stock
{
    // Struct which contain all stock information.
    // Most of this will be sent via multicast updates
    char StockName[MAX_COMPANY_NAME];                   // Stock Name
    Offer Ask[MAX_CLIENTS + 1];                         // Asking prices for each client (last index is the market)
    Offer Bid[MAX_CLIENTS];                             // Biding prices for each client
    float current_ppu;                                  // Current Price Per Unit
} Stock;

// Global:
// --------
struct sockaddr_in servAddr;
struct sockaddr_in clientAddr;
struct sockaddr_in mcastAddr;


int error_detect = 0;                                   // Used to indicate an error during the connection
User Accounts[MAX_CLIENTS];                             // Client's details array
Stock Stocks[STOCK_COUNT];                              // Stocks array
pthread_t ClientThreads[MAX_CLIENTS];                   // Client's [TCP handling] threads
pthread_t MulticastThread;                              // Multicast Updates thread

int socket_counter = 0;                                 // Current client socket counter
int reg_clients = 0;                                    // Current registered clients counter
int mcast_indicator = 0;                                // Multicast flag to prevent timeout of clients
char stock_names[STOCK_COUNT][MAX_COMPANY_NAME] = {
    "Apple Inc.         \0", "Ford Motor Company \0",
    "AT&T Inc.          \0", "Nvidia Corp        \0",
    "Tesla Inc.         \0", "Pfizer Inc.        \0",
    "Bank Of America    \0", "Intel Corp         \0",
    "Cisco Systems      \0", "Amazon Inc.        \0"};

// Function Declaration:
// ---------------------
void *connection_handler(void *socket);
void *updates_handler(void *socket);
int update_user_database(int socket, int index);
void buy(Offer buy_offer, int stock_num, int buyer_index);
void sell(Offer sell_offer, int stock_num, int seller_index);
int valid(Offer offer, int stock_index, int account_index, char type);
int my_send(int client_socket, char *buffer, int size);
int my_recv(int client_socket, char *buffer, int size);
int select_timer(int client_socket);

int main(int argc, char *argv[])
{
    // Stock Server Main Program
    // Argument vector: 1-Port
    if (argc < 2)
    {
        perror("Missing arguments. [Syntax: ./StockServer <PORT>");
        exit(EXIT_FAILURE);
    }

    // Variables Declaration & Initialization

    int welcome_socket;                         // Welcome socket variable
    int mcast_socket;                           // Multicast socket variable
    int client_socket = 0;                      // TCP socket variable (for each client)
    int addr_len = sizeof(clientAddr);
    ////////////////////////////////////////////////
    srand(time(NULL));
    for (int i = 0; i < STOCK_COUNT; i++)
    {
        strncpy(Stocks[i].StockName, stock_names[i], MAX_COMPANY_NAME);
        for (int j = 0; j < MAX_CLIENTS + 1; j++)
        {
            if (j != MAX_CLIENTS)
            {
                Stocks[i].Ask[j].ppu = 0;
                Stocks[i].Ask[j].Units = 0;
                Stocks[i].Bid[j].ppu = 0;
                Stocks[i].Bid[j].Units = 0;
            }
            else
            {
                Stocks[i].Ask[j].ppu = (float)(rand() % (3000));
                Stocks[i].Ask[j].Units = (int)(rand() % (100) + 20);
            }
        }
        Stocks[i].current_ppu = Stocks[i].Ask[MAX_CLIENTS].ppu;
    }

    ///////////////////////////////////////////////

    printf("----------------------------\n");
    printf("Stock Market Server Logfile :\n");
    printf("----------------------------\n");

    // Create A Welcome Socket
    if ((welcome_socket = socket(AF_INET, SOCK_STREAM, 0)) < 0)
    {
        perror("Creation of welcome socket has failed, stock server shutdown!");
        exit(EXIT_FAILURE);
    }
    printf("Creating 'welcome' socket.\n");

    // Create Multicast Socket
    if ((mcast_socket = socket(AF_INET, SOCK_DGRAM, 0)) < 0)
    {
        perror("Creation of multicast socket has failed, stock server shutdown!");
        exit(EXIT_FAILURE);
    }
    printf("Creating multicast socket.\n");

    // Configure IPv4 Address Properties
    bzero(&servAddr, sizeof(servAddr));
    servAddr.sin_family = AF_INET;
    servAddr.sin_addr.s_addr = htonl(INADDR_ANY);
    servAddr.sin_port = htons(atoi(argv[1]));

    bzero(&mcastAddr, sizeof(mcastAddr));
    mcastAddr.sin_family = AF_INET;
    mcastAddr.sin_addr.s_addr = inet_addr("239.0.0.1");
    mcastAddr.sin_port = htons(6000);

    // Creating Multicast Thread
    if ((pthread_create(&MulticastThread, NULL, updates_handler, &mcast_socket)) != 0)
    {
        perror("Multicast thread creation has failed, stock server shutdown!");
        close(welcome_socket);
        exit(EXIT_FAILURE);
    }

    // Binding The Welcome Socket
    if (bind(welcome_socket, (struct sockaddr *)&servAddr, sizeof(servAddr)) < 0)
    {
        perror("Binding welcome socket failed, stock server shutdown!");
        close(welcome_socket);
        exit(EXIT_FAILURE);
    }
    printf("Binding the 'welcome' socket.\n");

    // Listen To The Welcome Socket
    if (listen(welcome_socket, MAX_CLIENTS) < 0)
    {
        perror("Listen to welcome socket failed, stock server shutdown!");
        close(welcome_socket);
        exit(EXIT_FAILURE);
    }
    printf("Listening for incoming connections....\n");

    // Accept new client & open client handling thread.
    while (!error_detect)
    {
        while ((client_socket = accept(welcome_socket, (struct sockaddr *)&clientAddr, (socklen_t *)&addr_len)))
        {
            if (client_socket < 0)
            {
                perror("Accepting new connection has failed.\n");
                close(welcome_socket);
                exit(EXIT_FAILURE);
            }
            printf("New connection accepted.\n");
            if ((pthread_create(&ClientThreads[socket_counter++], NULL, connection_handler, &client_socket)) != 0)
            {
                perror("Client handler thread creation has failed, stock server shutdown!");
                close(client_socket);
                close(welcome_socket);
                exit(EXIT_FAILURE);
            }
        }
    }
    // Forcing main thread to wait for all clients & multicast threads to finish
    pthread_join(MulticastThread, NULL); // MULTICAST SOCKET
    for (int i = 0; i < socket_counter + 1; i++)
    {
        pthread_join(ClientThreads[socket_counter], NULL); // TCP SOCKETS
    }
    close(welcome_socket);
    exit(EXIT_SUCCESS);
}

void *connection_handler(void *socket)
{
    // TCP Client Handler Function
    // This will handle connection for each client

    int client_socket = *(int *)socket;       // The client socket of the specific client
    printf("Connection handler Socket #%d\n", client_socket);   // print to logfile

    // Account details variables
    char username[LOGIN_FIELD_SIZE + 1];
    char password[LOGIN_FIELD_SIZE + 1];
    username[LOGIN_FIELD_SIZE] = '\0';
    password[LOGIN_FIELD_SIZE] = '\0';
    float invest;
    char investChar[MAX_INVEST_SIZE];
    int account_index;

    // Flags, messages buffers & iterator variables.
    int i, approved = 0, cont_flag = 1;
    char buffer[BufferSize];
    char acknowledge[11];
    char msg_type;

    // Waiting X Interval of time for client to send login/register request
    // if not receiving then close the socket & exit.

    while (cont_flag)
    {
        // This section will be executed while not logged in
        memset(buffer,0,sizeof(BufferSize));
        memset(acknowledge, 0, sizeof(acknowledge));
        if (select_timer(client_socket) == 0)
        {
            close(client_socket);
            printf("Client from socket %d is idle for too long, kicking.\n", client_socket);
            pthread_exit(NULL);
        }
        if (my_recv(client_socket, buffer, BufferSize) <= 0)
        {
            close(client_socket);
            pthread_exit(NULL);
        }
        msg_type = buffer[0];
        strncpy(username, buffer + 1, 8);
        strncpy(password, buffer + 9, 8);
        if (msg_type == 'R')
        {
            if (reg_clients == MAX_CLIENTS)
            {
                if (my_send(client_socket, "F", sizeof("F")) <= 0)
                {
                    close(client_socket);
                    pthread_exit(NULL);
                }
                continue;
            }
            approved = 1;
            strncpy(investChar, buffer + 17, MAX_INVEST_SIZE);
            invest = (float)atof(investChar);
            if (reg_clients != 0)
            {
                // Checking if new registeration is allowed & username is free to use.
                for (i = 0; i < reg_clients; i++)
                {
                    if (!strcmp(Accounts[i].username, username))
                    {
                        approved = 0;
                        break;
                    }
                }
            }
            if (approved)
            {
                // Registeration success -> Saving new client details
                printf("%s has registered to the server\n", username);  // print to logfile
                strncpy(Accounts[reg_clients].username, username, LOGIN_FIELD_SIZE);
                strncpy(Accounts[reg_clients].password, password, LOGIN_FIELD_SIZE);
                Accounts[reg_clients].balance_sheet = invest;
                for (i = 0; i < STOCK_COUNT; i++)
                {
                    // Updating new client portfolio
                    Accounts[reg_clients].portfolio[i].StockName = stock_names[i];
                    Accounts[reg_clients].portfolio[i].buying_ppu = 0;
                    Accounts[reg_clients].portfolio[i].current_ppu = 0;
                    Accounts[reg_clients].portfolio[i].units = 0;
                }
                reg_clients++;
            }
        }
        else
        {
            if (msg_type == 'L')
            {
                approved = 0;
                // Checking if login is allowed
                if (reg_clients != 0)
                {
                    // checking if the entered username and password are valid & match.
                    for (i = 0; i < reg_clients; i++)
                    {
                        if (((!strcmp(Accounts[i].username, username) && !strcmp(Accounts[i].password, password))) && !Accounts[i].isLoggedIn)
                        {
                            approved = 1;
                            Accounts[i].isLoggedIn = 1;
                            account_index = i;
                            cont_flag = 0;
                            break;
                        }
                    }
                }
            }
            else
            {
                // This message should not be sent -> closing client socket & thread handler.
                printf("An undifined message received, closing client socket\n");
                Accounts[account_index].isLoggedIn = 0;
                close(client_socket);
                pthread_exit(NULL);
            }
        }

        if (approved)
        {
            if (msg_type == 'R')
                acknowledge[0] = 'A';
            else
                strcpy(acknowledge, "A239.0.0.1");
            // Sending Registeration Ack with Multicast IP address
            if (my_send(client_socket, acknowledge, sizeof(acknowledge)) <= 0)
            {
                Accounts[account_index].isLoggedIn = 0;
                pthread_exit(NULL);
            }
        }
        else
        {
            // Sending Registeration/Login Negative ACK
            acknowledge[0] = 'X';
            if (my_send(client_socket, acknowledge, sizeof(acknowledge)) <= 0)
            {
                Accounts[account_index].isLoggedIn = 0;
                close(client_socket);
                pthread_exit(NULL);
            }
        }
    }

    // Connection established
    // The user logged in successfully

    // Updating client's portfolio
    if (update_user_database(client_socket, account_index) == -1)
    {
        Accounts[account_index].isLoggedIn = 0;
        close(client_socket);
        pthread_exit(NULL);
    }
    // Offer's variables
    Offer offer;
    char request[14];
    char c0[2], c1[5], c2[9];
    int stock_index;
    int recv_in_while = 1;
    while(1){
        // Checks client's request
        if(select_timer(client_socket) == 0)
            break;
        if (recv_in_while = my_recv(client_socket, request, sizeof(request)) <= 0)
            break;
            if(request[0] == '\0')
            {
                continue;
            }
        if (request[0] == 'B' || request[0] == 'S')
        {
            // Get Buy/Sell request variables
            memcpy(c0, request + 1, 1);
            stock_index = atoi(c0);
            memcpy(c1, request + 2, 4);
            offer.Units = atoi(c1);
            memcpy(c2, request + 6, 7);
            offer.ppu = (float)atof(c2);
        }
        else
        {
            if(request[0] != 'G' && request[0] !='M')
            {
                // Handling wrong type requests
                printf("An undifined message received, closing client socket\n");
                Accounts[account_index].isLoggedIn = 0;
                close(client_socket);
                pthread_exit(NULL);
            }
        }
        switch (request[0])
        {
            // Act according to client's request
        case ('G'):
        {
            // Get details reuqest
            if (update_user_database(client_socket, account_index) <= -1)
            {
                Accounts[account_index].isLoggedIn = 0;
                pthread_exit(NULL);
            }
            break;
        }
        case ('B'):
        {
            // Buy request
            if (!valid(offer, stock_index, account_index, 'B'))
            {
                if (my_send(client_socket, "I", sizeof("I")) <= 0)
                {
                    Accounts[account_index].isLoggedIn = 0;
                    pthread_exit(NULL);
                }
                break;
            }
            buy(offer, stock_index, account_index);
            if (my_send(client_socket, "V", sizeof("V")) <= 0)
            {
                Accounts[account_index].isLoggedIn = 0;
                pthread_exit(NULL);
            }
            break;
        }
        case ('S'):
        {
            // Sell request
            if (!valid(offer, stock_index, account_index, 'S'))
            {
                if (my_send(client_socket, "I", sizeof("I")) <= 0)
                {
                    Accounts[account_index].isLoggedIn = 0;
                    pthread_exit(NULL);
                }
                break;
            }
            sell(offer, stock_index, account_index);
            if (my_send(client_socket, "V", sizeof("V")) <= 0)
            {
                Accounts[account_index].isLoggedIn = 0;
                pthread_exit(NULL);
            }
            break;
        }
        case ('M'):
        {
            // Multicast State - In this section timeout is disabled
            memset(request, 0, sizeof(request));
            mcast_indicator = 1;
            my_recv(client_socket, request, sizeof('m'));
            // Waiting for client to end veiwing mcast updates
            if (request[0] == 'm')
            {
                // Re-enable timeout
                mcast_indicator = 0;
                break;
            }
            else
            {
                Accounts[account_index].isLoggedIn = 0;
                pthread_exit(NULL);
            }
        }
        }
    }
    // Exit - re-enable login
    Accounts[account_index].isLoggedIn = 0;
    close(client_socket);
    if (recv_in_while == 1)
        printf("Socket %d closed successfully\n", client_socket);
    else
        printf("Client from socket %d is idle for too long, kicking.\n", client_socket);

    pthread_exit(NULL);
}

int update_user_database(int socket, int index)
{
    // Send details to client

    // Client's details variables
    char UpdateBuffer[STOCK_COUNT * STOCK_SIZE + MAX_INVEST_SIZE];
    char names[STOCK_COUNT][MAX_COMPANY_NAME];
    int units[STOCK_COUNT];
    float bppu[STOCK_COUNT];
    float cppu[STOCK_COUNT];
    float curr_balance;
    char reply[2];
    reply[0] = 'N';
    memset(UpdateBuffer, 0, STOCK_COUNT * STOCK_SIZE + MAX_INVEST_SIZE);

    for (int m = 0; m < STOCK_COUNT; m++)
    {
        // Construct message
        strncpy(names[m], Accounts[index].portfolio[m].StockName, MAX_COMPANY_NAME);
        units[m] = Accounts[index].portfolio[m].units;
        bppu[m] = Accounts[index].portfolio[m].buying_ppu;
        cppu[m] = Accounts[index].portfolio[m].current_ppu;
        curr_balance = Accounts[index].balance_sheet;
    }
    for (int m = 0; m < STOCK_COUNT; m++)
    {
        snprintf(UpdateBuffer + (STOCK_SIZE * m), STOCK_SIZE, "%s%04d%07.2f%07.2f", names[m], units[m], bppu[m], cppu[m]);
    }
    for (int u = 0; u < STOCK_COUNT * STOCK_SIZE + MAX_INVEST_SIZE; u++)
    {
        if (UpdateBuffer[u] == '\0')
            UpdateBuffer[u] = ' ';
    }
    sprintf(UpdateBuffer + STOCK_SIZE * STOCK_COUNT, "%10.2f", curr_balance);
    while (reply[0] == 'N')
    {
        // This is verifying that client recieved the message correctly
        if (my_send(socket, UpdateBuffer, STOCK_SIZE * STOCK_COUNT + MAX_INVEST_SIZE) <= 0)
        {
            return -1;
        }
        if (my_recv(socket, reply, 1) <= 0)
        {
            return -1;
        }
        if (reply[0] != 'N' && reply[0] != 'Y')
        {
            printf("An undifined message received, closing client socket\n");
            pthread_exit(NULL);
            return -1;
        }
    }
    return 1;
}

int find_min_ask_index(Offer Ask[MAX_CLIENTS + 1], int buyer_index)
{
    // This function returns the best current ask offer's index
    int ret;
    float min = MAX_PPU;
    for (int i = 0; i < MAX_CLIENTS + 1; i++)
    {
        if (i == buyer_index)
            continue;
        if (Ask[i].ppu > 0 && Ask[i].ppu < min)
        {
            min = Ask[i].ppu;
            ret = i;
        }
    }
    if (min == MAX_PPU)
        return -1;
    else
        return ret;
}

int find_max_bid_index(Offer Bid[MAX_CLIENTS], int seller_index)
{
    // This function returns the best current bid offer's index
    int ret;
    float max = 0;
    for (int i = 0; i < MAX_CLIENTS; i++)
    {
        if (i == seller_index)
            continue;
        if (Bid[i].ppu > 0 && Bid[i].ppu > max)
        {
            max = Bid[i].ppu;
            ret = i;
        }
    }
    if (max == 0)
        return -1;
    else
        return ret;
}

void buy(Offer buy_offer, int stock_num, int buyer_index)
{
    // This function is handling buying operation
    int min_index;
    int units_bought = 0;
    float total_money_buy = 0;
    int action = 0;
    while (1)
    {
        min_index = find_min_ask_index(Stocks[stock_num].Ask, buyer_index);
        if (min_index != -1 && Stocks[stock_num].Ask[min_index].ppu <= buy_offer.ppu)
        { //Min is a lower ppu, buy from it
            action = 1;
            if (Stocks[stock_num].Ask[min_index].Units >= buy_offer.Units)
            { //Min has enough units to complete the buy
                Stocks[stock_num].Ask[min_index].Units -= buy_offer.Units;
                units_bought += buy_offer.Units;
                total_money_buy += (float)buy_offer.Units * Stocks[stock_num].Ask[min_index].ppu;

                Accounts[min_index].portfolio[stock_num].units -= buy_offer.Units;
                Accounts[min_index].balance_sheet += (float)buy_offer.Units * Stocks[stock_num].Ask[min_index].ppu;
                buy_offer.Units = 0;
                buy_offer.ppu = 0;
                Stocks[stock_num].current_ppu = Stocks[stock_num].Ask[min_index].ppu;
                if (Stocks[stock_num].Ask[min_index].Units == 0)
                {
                    // Delete the offer, (bought all no need to save the buyer offer its all bought)
                    Stocks[stock_num].Ask[min_index].ppu = 0;
                    if (Accounts[min_index].portfolio[stock_num].units == 0)
                        Accounts[min_index].portfolio[stock_num].buying_ppu = 0;
                }
                break;
            }
            else
            {
                // Min does not have enough units to complete the buy, bought all Min
                // (Stocks[stock_num].Ask[min_index].units) units of his stock (all) were bought

                buy_offer.Units -= Stocks[stock_num].Ask[min_index].Units;
                units_bought += Stocks[stock_num].Ask[min_index].Units;
                total_money_buy += (float)Stocks[stock_num].Ask[min_index].Units * Stocks[stock_num].Ask[min_index].ppu;

                Accounts[min_index].portfolio[stock_num].units -= Stocks[stock_num].Ask[min_index].Units;
                Accounts[min_index].balance_sheet += (float)Stocks[stock_num].Ask[min_index].Units * Stocks[stock_num].Ask[min_index].ppu;
                Stocks[stock_num].Ask[min_index].Units = 0; // Delete the offer
                Stocks[stock_num].Ask[min_index].ppu = 0;
                Stocks[stock_num].current_ppu = Stocks[stock_num].Ask[min_index].ppu;
            }
        }
        else
        // Add to the Bid array if not empty bid
        {
            if (buy_offer.Units == 0)
                break;
            Stocks[stock_num].Bid[buyer_index] = buy_offer;
            break;
        }
    }
    if (action == 1)
    {
        Accounts[buyer_index].portfolio[stock_num].units += units_bought;
        if (Accounts[buyer_index].portfolio[stock_num].buying_ppu == 0)
            Accounts[buyer_index].portfolio[stock_num].buying_ppu = total_money_buy / (float)units_bought;
        Accounts[buyer_index].portfolio[stock_num].current_ppu = total_money_buy / (float)units_bought;
        Accounts[buyer_index].balance_sheet -= total_money_buy;
        for (int acc = 0; acc < MAX_CLIENTS; acc++)
        {
            Accounts[acc].portfolio[stock_num].current_ppu = total_money_buy / (float)units_bought;
        }
    }
}

void sell(Offer sell_offer, int stock_num, int seller_index)
{
    // This function is handling selling operation
    int max_index;
    int units_sold = 0;
    float total_money_sell = 0;
    int action = 0;
    while (1)
    {
        max_index = find_max_bid_index(Stocks[stock_num].Bid, seller_index);
        if (max_index != -1 && Stocks[stock_num].Bid[max_index].ppu >= sell_offer.ppu)
        { // Max is a higher ppu, sell to him
            action = 1;
            if (Stocks[stock_num].Bid[max_index].Units >= sell_offer.Units)
            {   // Max has enough units to complete the sell
                Stocks[stock_num].Bid[max_index].Units -= sell_offer.Units;
                units_sold += sell_offer.Units;
                total_money_sell += (float)sell_offer.Units * Stocks[stock_num].Bid[max_index].ppu;

                if (Accounts[seller_index].portfolio[stock_num].units == 0)
                    Accounts[seller_index].portfolio[stock_num].buying_ppu = 0;
                Accounts[max_index].portfolio[stock_num].units += sell_offer.Units;
                Accounts[max_index].balance_sheet += (float)sell_offer.Units * Stocks[stock_num].Bid[max_index].ppu;
                sell_offer.Units = 0;
                sell_offer.ppu = 0;
                Stocks[stock_num].current_ppu = Stocks[stock_num].Bid[max_index].ppu;

                if (Stocks[stock_num].Bid[max_index].Units == 0)
                {
                    Stocks[stock_num].Bid[max_index].Units = 0; // Delete the offer, (sold all no need to save the seller offer its all sold)
                    Stocks[stock_num].Bid[max_index].ppu = 0;
                }
                break;
            }
            else
            {
                // Max does not have enough units to complete the sell, all Max sold
                // (Stocks[stock_num].Bid[max_index].units) units of his stock (all) were sold
                sell_offer.Units -= Stocks[stock_num].Bid[max_index].Units;

                units_sold += Stocks[stock_num].Bid[max_index].Units;
                total_money_sell += (float)Stocks[stock_num].Bid[max_index].Units * Stocks[stock_num].Bid[max_index].ppu;
                Stocks[stock_num].current_ppu = Stocks[stock_num].Bid[max_index].ppu;

                Accounts[max_index].portfolio[stock_num].units -= Stocks[stock_num].Bid[max_index].Units;
                Accounts[max_index].balance_sheet += (float)Stocks[stock_num].Bid[max_index].Units * Stocks[stock_num].Bid[max_index].ppu;
                Stocks[stock_num].Bid[max_index].Units = 0; // Delete the offer
                Stocks[stock_num].Bid[max_index].ppu = 0;
            }
        }
        else
        // Add to the Ask array if not empty ask
        {
            if (sell_offer.Units == 0)
                break;
            Stocks[stock_num].Ask[seller_index] = sell_offer;
            break;
        }
    }
    if (action == 1)
    {
        Accounts[seller_index].portfolio[stock_num].units -= units_sold;
        if (Accounts[seller_index].portfolio[stock_num].units == 0)
        {
            Accounts[seller_index].portfolio[stock_num].buying_ppu = 0;
            Accounts[seller_index].portfolio[stock_num].current_ppu = 0;
        }
        Accounts[seller_index].portfolio[stock_num].current_ppu = total_money_sell / (float)units_sold;
        Accounts[seller_index].balance_sheet += total_money_sell;

        for (int acc = 0; acc < MAX_CLIENTS; acc++)
        {
            Accounts[acc].portfolio[stock_num].current_ppu = total_money_sell / (float)units_sold;
        }
    }
}

int valid(Offer offer, int stock_index, int account_index, char type)
{
    // Verifying offer request is valid for the specific client
    if (type == 'B' && (float)offer.Units * offer.ppu > Accounts[account_index].balance_sheet)
        return 0;
    if (type == 'S' && offer.Units > Accounts[account_index].portfolio[stock_index].units)
        return 0;
    return 1;
}

int my_send(int client_socket, char *buffer, int size)
{
    // Send function with error handling
    int send_val;
    send_val = (int)send(client_socket, buffer, size, 0);
    if (send_val <= 0)
    {
        close(client_socket);
    }
    return (int)send_val;
}

int my_recv(int client_socket, char *buffer, int size)
{
    // Receive function with error handling
    int recv_val;
    memset(buffer, 0, size);
    recv_val = (int)recv(client_socket, buffer, size, 0);
    if (recv_val <= 0)
    {
        close(client_socket);
    }
    return (int)recv_val;
}

void *updates_handler(void *socket)
{
    // Multicast Updates Function

    // Multicast variables
    int mcast_socket = *(int *)socket;
    int updateInterval = 10;
    char updateMessage[STOCK_COUNT * STOCK_UPDATE_SIZE + 5]; // +5 for seq_num
    unsigned char ttl = 64;
    int min_ask, max_bid;
    int seq_num = 0;

    // Configure The TTL of the IP Header to 64
    if ((setsockopt(mcast_socket, IPPROTO_IP, IP_TTL, (void *)&ttl, sizeof(ttl))) < 0)
    {
        perror("Multicast socket configure has failed. Server shutdown!");
        error_detect = 1;
        close(mcast_socket);
        pthread_exit(NULL);
    }
    // Configure The TTL of the UDP Header to 64
    if ((setsockopt(mcast_socket, IPPROTO_IP, IP_MULTICAST_TTL, (void *)&ttl, sizeof(ttl))) < 0)
    {
        perror("Multicast socket configure has failed. Server shutdown!");
        error_detect = 1;
        close(mcast_socket);
        pthread_exit(NULL);
    }
    int swap_flag = 0;
    while (!error_detect)
    {
        // Constructing Multicast Update Message
        memset(updateMessage, 0, sizeof(updateMessage));
        for (int i = 0; i < STOCK_COUNT; i++)
        {
            snprintf(updateMessage + (i * STOCK_UPDATE_SIZE), MAX_COMPANY_NAME, "%s", Stocks[i].StockName);
            min_ask = find_min_ask_index(Stocks[i].Ask, -1);
            max_bid = find_max_bid_index(Stocks[i].Bid, -1);
            if (min_ask != -1)
            {
                sprintf(updateMessage + (i * STOCK_UPDATE_SIZE) + MAX_COMPANY_NAME, "%04d%07.2f",
                        Stocks[i].Ask[min_ask].Units, Stocks[i].Ask[min_ask].ppu);
            }
            else
                sprintf(updateMessage + (i * STOCK_UPDATE_SIZE) + MAX_COMPANY_NAME, "%s", "-----------");
            if (max_bid != -1)
            {
                sprintf(updateMessage + (i * STOCK_UPDATE_SIZE) + MAX_COMPANY_NAME + 11, "%04d%07.2f",
                        Stocks[i].Bid[max_bid].Units, Stocks[i].Bid[max_bid].ppu);
            }
            else
                sprintf(updateMessage + (i * STOCK_UPDATE_SIZE) + MAX_COMPANY_NAME + 11, "%s", "-----------");
            sprintf(updateMessage + (i * STOCK_UPDATE_SIZE) + MAX_COMPANY_NAME + 22, "%07.2f", Stocks[i].current_ppu);
        }
        // THIS SHOULD SEND MCAST UPDATE WITH WRONG SEQUENCE NUMBER :)
        // The third mcast update will always be dropped
        if(seq_num == 3){
            seq_num = 2;
            swap_flag = 1;
        }
        sprintf(updateMessage + (STOCK_COUNT * STOCK_UPDATE_SIZE), "%04d", seq_num);
        sendto(mcast_socket, updateMessage, sizeof(updateMessage), 0, (struct sockaddr *)&mcastAddr, sizeof(mcastAddr));
        if(swap_flag == 1 && seq_num == 2)
            {
                seq_num = 3;
                swap_flag = 0;
            }
        seq_num = (seq_num + 1)%10000;
        // Going to sleep in order to let things to change up a bit...
        sleep(updateInterval);
    }
    close(mcast_socket);
    pthread_exit(NULL);
}

int select_timer(int client_socket)
{
    // This function will handle timeouts of clients (45 seconds)
    fd_set readfds;
    struct timeval tv;
    tv.tv_sec = 45;
    tv.tv_usec = 0;
    int activity;
    FD_ZERO(&readfds);
    FD_SET(client_socket, &readfds);
    activity = select(client_socket + 1, &readfds, NULL, NULL, &tv);
    return activity;
}
