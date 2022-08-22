//  Client Source Code:

// Including Libraries
// -------------------
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <time.h>
#include <sys/time.h>
#include <sys/select.h>
#include <fcntl.h>
#include <unistd.h>

// Defines
// ---------------
#define BufferSize 100
#define DataBufferSize 450
#define STOCKS_COUNT 10
#define MAX_COMPANY_NAME 20
#define STOCK_SIZE 38
#define STOCK_UPDATE_SIZE 49
#define MAX_INVEST_SIZE 11
#define MAX_UNITS 9999
#define MAX_PPU 10000

typedef struct MyStock
{
    // Struct Of A Specific Stock (shared with the users)
    char StockName[MAX_COMPANY_NAME];   // Stock Name
    int units;                          // Owned units
    float buying_ppu;                   // Buying price per unit
    float current_ppu;                  // Current price per unit
} MyStock;

// Client Details Variables
char username[9];
char password[9];
MyStock Stocks[STOCKS_COUNT];
float Balance = 0;
int StockAmount = 0;

// Connection variables
int mcast_socket;
int client_socket;
int mcast_on = 0;                       // Flag to indicate multicast socket is opened

void print_details();
void account_update(int socket);
int my_send(int client_socket, char *buffer, int size);
int my_recv(int client_socket, char *buffer, int size);

int main(int argc, char *argv[])
{
    // Stock Client Main Program
    // Argument vector: 1-IP, 2-Port
        if (argc < 3) {
            perror("Missing arguments. [Syntax: ./StockClient <SERVER_IP> <PORT>");
            exit(EXIT_FAILURE);
        }

    // Variables Declaration & Initialization
    char *ip = argv[1];
    char buffer[BufferSize];
    int port = atoi(argv[2]);

    struct sockaddr_in serv_addr;
    struct sockaddr_in mcast_addr;
    struct ip_mreq mreq;

    // Create A New Client Socket
    client_socket = socket(AF_INET, SOCK_STREAM, 0);
    if (client_socket == -1)
    {
        perror("TCP Socket creation failed");
        exit(EXIT_FAILURE);
    }

    // Configure IPv4 Address Properties
    bzero(&serv_addr, sizeof(serv_addr));
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_addr.s_addr = inet_addr(ip);
    serv_addr.sin_port = htons(port);

    // Connect To The Stock Server
    if (connect(client_socket, (struct sockaddr *)&serv_addr, sizeof(serv_addr)) != 0)
    {
        perror("Connection failed!");
        exit(1);
    }

    // Send Welcome Message To The Chat Server (Assuming Input is correct)
    char msg_type[2];
    float invest;
    char mcast_ip[10];
    char char_login = 'X';
    char char_register = 'X';
    // Client Menu
    printf("\nWelcome to our stock market!\n");
    printf("-----------------------------\n");
    while (1)
    {
        printf("[+] Are you new here? Press 'R' to register and start investing today!\n");
        printf("[+] Already have an account? Press 'L' to login\n");
        printf("[-] If do you wish to leave, Press 'Q'\n");
        scanf("%s", msg_type);
        memset(buffer,0,BufferSize);
        if (msg_type[0] == 'L')
        {
            printf("\nLogin Interface:\n[+] Username: ");
            scanf("%8s", username);
            printf("[+] Password: ");
            scanf("%8s", password);
            sprintf(buffer, "%c%8s%8s", msg_type[0], username, password);
            my_send(client_socket, buffer, BufferSize);
            my_recv(client_socket, buffer, BufferSize);
            char_login = buffer[0];
            if (char_login == 'A')
            {
                sprintf(mcast_ip, "%s", buffer + 1);
                break;
            }
            else
            {
                if (char_login != 'X')
                {
                    printf("An undifined message was received, disconnecting...\n");
                    close(client_socket);
                    exit(EXIT_FAILURE);
                }
                else
                    printf("\nBad login attempt, please try again\n");
            }
        }
        if (msg_type[0] == 'R')
        {
            printf("\n[+] Enter a username up to 8 characters: ");
            scanf("%8s", username);
            printf("[+] Enter a password up to 8 characters: ");
            scanf("%8s", password);
            printf("[+] How much money are you willing to invest? (U.S. Dollars): ");
            scanf("%f", &invest);
            sprintf(buffer, "%c%8s%8s%f", msg_type[0], username, password, invest);
            my_send(client_socket, buffer, BufferSize);
            my_recv(client_socket, buffer, BufferSize);
            char_register = buffer[0];
            if (char_register == 'A')
                printf("\nRegistration Successful!\nYou may now log in your account\n\n");
            else
            {
                if (char_register == 'X')
                    printf("\nUsername is already in use, please try another\n");
                else
                {
                    if (char_register == 'F')
                    {
                        printf("\nServer is full, sorry!\n");
                        msg_type[0] = 'Q';
                    }
                    else
                    {
                        printf("An undifined message was received, disconnecting...\n");
                        close(client_socket);
                        exit(EXIT_FAILURE);
                    }
                }
            }
        }
        if (msg_type[0] == 'Q')
        {
            printf("Wish to see you again, Goodbye!\n");
            close(client_socket);
            exit(EXIT_SUCCESS);
        }
    }
    // Login success!!

    //////////////////MCAST SOCKET//////////////////
    mcast_socket = socket(AF_INET, SOCK_DGRAM, 0);
    if (mcast_socket == -1)
    {
        perror("Multicast Socket creation failed");
        exit(EXIT_FAILURE);
    }

    // Configure Multicast IPv4 Address
    bzero(&mcast_addr, sizeof(mcast_addr));
    mcast_addr.sin_family = AF_INET;
    mcast_addr.sin_addr.s_addr = htonl(INADDR_ANY);
    mcast_addr.sin_port = htons(6000);
    mreq.imr_multiaddr.s_addr = inet_addr(mcast_ip);
    mreq.imr_interface.s_addr = htonl(INADDR_ANY);

    // Binding the multicast socket
    if (bind(mcast_socket, (struct sockaddr *)&mcast_addr, sizeof(mcast_addr)) != 0)
    {
        perror("Multicast binding has failed");
        exit(EXIT_FAILURE);
    }
    // Configuring multicast socket
    if (setsockopt(mcast_socket, IPPROTO_IP, IP_ADD_MEMBERSHIP, &mreq, sizeof(mreq)) < 0)
    {
        perror("Multicast socket configure has failed. Server shutdown!");
        close(mcast_socket);
        close(client_socket);
        exit(EXIT_FAILURE);
    }
    int curr_seq_num=-1;
    char recv_seqchar[4];

    mcast_on = 1;
    /////////////////////////////////////////////////
    account_update(client_socket);  // Get account details update (via TCP)
    char choice[2] = "0";
    char offer_reply[50];
    printf("\nLogin Successful\n");
    printf("\nWelcome Back, ");
    for (int i = 0; i < 8; i++)
    {
        if (username[i] != ' ')
            printf("%c", username[i]);
    }
    printf(".\n[+] Your Account Balance is: %.02f", Balance);
    if (StockAmount)
        printf("\n[+] You got %d stocks in your investment portfolio.\n", StockAmount);
    else
        printf("\n[-] Your portfolio is empty! Make sure you don't miss good opportunities...");
    char mcastUpdate[STOCK_UPDATE_SIZE * STOCKS_COUNT+4];
    int mcast_addrlen = sizeof(mcast_addr);
    int activity = 0;
    fd_set readfds;

    while (choice[0] != '4')
    {
        printf("\n\nMain Menu:\n");
        printf("--------------\n");
        printf("[1] Display Full Investment Portfolio & Account Details\n");
        printf("[2] Real-Time Stock Prices\n");
        printf("[3] Buy/Sell Stocks\n");
        printf("[4] Exit\n");
        scanf("%s", choice);
        switch (choice[0])
        {
        case ('0'):
        {
            // Secret Case -> Just for defences :)
            printf("\nOH NO YOU FOUND ME!\n");
            printf("Lets send a wrong message type -> *");
            my_send(client_socket, "*", sizeof("*"));
            break;
        }
        case ('1'):
        {
            // Case 1 -> send Get Details request & print it to the client
            my_send(client_socket, "G", sizeof("G"));
            account_update(client_socket);
            print_details();
            break;
        }
        case ('2'):
        {
            // Case 2 -> Send 'M' to notify the server to disable timeout
            //           Send 'm' to notify the server to re-enable timeout
            my_send(client_socket, "M", sizeof("M"));

            while (1)
            {
                // Handling multicast updates & print them to the screen
                FD_ZERO(&readfds);
                FD_SET(mcast_socket, &readfds);
                FD_SET(client_socket, &readfds);
                FD_SET(0, &readfds);
                int recvfrom_val;
                int max_sock;
                char buff[2];
                if(mcast_socket > client_socket)
                    max_sock = mcast_socket;
                else max_sock = client_socket;
                activity = select(max_sock + 1, &readfds, NULL, NULL, NULL);
                if (activity <= 0)
                {
                    if (activity < 0)
                        perror("Select error! connection terminated");
                    else
                        printf("\nOops, our server is currently down, please try again later.\n");
                    close(client_socket);
                    close(mcast_socket);
                    exit(EXIT_FAILURE);
                }
                if ((FD_ISSET(client_socket, &readfds)))
                {
                    // TCP Socket Interrupted -> Will happen just in case of server crashing.
                    my_recv(client_socket,buff,sizeof(buff));
                    break;
                }
                if ((FD_ISSET(0, &readfds)))
                {
                    // STDIN File Descriptor Interrupted
                    my_send(client_socket, "m", sizeof("m"));
                    break;
                }
                if ((FD_ISSET(mcast_socket, &readfds)))
                {
                    // Multicast UDP Socket Interrupted
                    memset(mcastUpdate, 0, sizeof(mcastUpdate));
                    recvfrom_val = recvfrom(mcast_socket, mcastUpdate, sizeof(mcastUpdate), 0, (struct sockaddr *)&mcast_addr, &mcast_addrlen);
                    if (recvfrom_val <= 0)
                    {
                        close(client_socket);
                        close(mcast_socket); //mc sock
                        printf("\nOops, our server is currently down, please try again later.\n");
                        exit(EXIT_FAILURE);
                    }
                    memcpy(recv_seqchar,mcastUpdate + STOCKS_COUNT*STOCK_UPDATE_SIZE,4);
                    if (curr_seq_num >= atoi(recv_seqchar) && curr_seq_num!=9999) {
                        printf("A problem has occured, update missed\n");
                        continue;
                    }
                    curr_seq_num = atoi(recv_seqchar);
                    for (int st = 0; st < STOCKS_COUNT; st++)
                    {
                        printf("Stock: ");
                        for (int c = 0; c < MAX_COMPANY_NAME; c++)
                        {
                            printf("%c", mcastUpdate[st * STOCK_UPDATE_SIZE + c]);
                        }
                        int lead = 1;
                        printf("\nCurrent Price: ");
                        for (int c = 42; c < STOCK_UPDATE_SIZE; c++)
                        {
                            if (mcastUpdate[st * STOCK_UPDATE_SIZE + c] != '0')
                                lead = 0;
                            if (lead == 0)
                                printf("%c", mcastUpdate[st * STOCK_UPDATE_SIZE + c]);
                            else
                                printf(" ");
                        }
                        printf("\nCurrent Minimum Ask: ");
                        if (mcastUpdate[st * STOCK_UPDATE_SIZE + MAX_COMPANY_NAME] != '-')
                        {
                            lead = 1;
                            for (int c = MAX_COMPANY_NAME; c < MAX_COMPANY_NAME + 4; c++)
                            {
                                if (mcastUpdate[st * STOCK_UPDATE_SIZE + c] != '0')
                                    lead = 0;
                                if (lead == 0)
                                    printf("%c", mcastUpdate[st * STOCK_UPDATE_SIZE + c]);
                                else
                                    printf(" ");
                            }
                            printf(" Units In Price Of ");
                            lead = 1;
                            for (int c = MAX_COMPANY_NAME + 4; c < MAX_COMPANY_NAME + 11; c++)
                            {
                                if (mcastUpdate[st * STOCK_UPDATE_SIZE + c] != '0')
                                    lead = 0;
                                if (lead == 0)
                                    printf("%c", mcastUpdate[st * STOCK_UPDATE_SIZE + c]);
                                else
                                    printf(" ");
                            }
                            printf(" Each.\n");
                        }
                        else
                            printf("  -- Currently There Are No Ask Offers --\n");
                        printf("Current Maximum Bid: ");
                        if (mcastUpdate[st * STOCK_UPDATE_SIZE + MAX_COMPANY_NAME + 11] != '-')
                        {
                            lead = 1;
                            for (int c = MAX_COMPANY_NAME + 11; c < MAX_COMPANY_NAME + 15; c++)
                            {
                                if (mcastUpdate[st * STOCK_UPDATE_SIZE + c] != '0')
                                    printf("%c", mcastUpdate[st * STOCK_UPDATE_SIZE + c]);
                                else
                                    printf(" ");
                            }
                            printf(" Units In Price Of ");
                            for (int c = MAX_COMPANY_NAME + 15; c < MAX_COMPANY_NAME + 22; c++)
                            {
                                if (mcastUpdate[st * STOCK_UPDATE_SIZE + c] != '0')
                                    lead = 0;
                                if (lead == 0)
                                    printf("%c", mcastUpdate[st * STOCK_UPDATE_SIZE + c]);
                                else
                                    printf(" ");
                            }
                            printf(" Each.\n\n");
                        }
                        else
                            printf("  -- Currently There Are No Bid Offers --\n\n");
                    }
                    time_t curr_t;
                    curr_t = time(NULL);
                    char *time = ctime(&curr_t);
                    time[20] = '\0';
                    printf("Last update: %s\n", time + 11);
                    printf("\n** Press 'Enter' To Return To Main Menu **\n\n");
                }
            }
            break;
        }
        case ('3'):
        {
            // Case 3 -> Get client B/S request's details, send it & waiting for an ack
            char input[14];
            char transaction[2];
            int stock_index;
            int units;
            float ppu;
            int bad_input;
            do
            {
                // Specify client request
                bad_input = 0;
                printf("\n[+] Would you like to buy or sell? B/S ");
                scanf("%1s", transaction);
                printf("\n[+] Enter Stock to buy: ");
                scanf("%d", &stock_index);
                printf("[+] Enter units to buy: ");
                scanf("%d", &units);
                printf("[+] Enter price per unit: ");
                scanf("%f", &ppu);
                stock_index -= 1;
                if ((stock_index > STOCKS_COUNT - 1 || stock_index < 0) ||
                    (transaction[0] != 'B' && transaction[0] != 'S') ||
                    (units < 0 || units > MAX_UNITS) || (ppu < 0 || ppu > 9999.99))
                {
                    printf("\nInvalid input please try again...\n");
                    bad_input = 1;
                }
            } while (bad_input);
            sprintf(input, "%1s%d%04d%07.2f", transaction, stock_index, units, ppu);
            my_send(client_socket, input, sizeof(input));
            my_recv(client_socket, offer_reply, sizeof(offer_reply));
            if (offer_reply[0] == 'V')
            {
                // In this case ack was recieved from the server -> notify the client about it
                printf("\nGood deal! your offer is now online.\n");
                break;
            }
            if (offer_reply[0] == 'I')
            {
                // In this case negative ack was recieved from the server -> notify client about it
                printf("\nInvalid offer.\n");
                break;
            }
            else
            {
                // In this case wrong message received -> indicating an error
                printf("An undifined message was received, disconnecting...\n");
                close(client_socket);
                close(mcast_socket);
                exit(EXIT_FAILURE);
            }
        }
        case ('4'):
        {
            // Case 4 -> Exit.
            printf("\n See you next time...\n");
            break;
        }
        }
    }
    close(mcast_socket);
    close(client_socket);
}

void print_details()
{
    // This function prints client's database which recieved from the server
    int counter = 1;
    double total_profit = 0;
    float curr_profit;
    printf("\n\n-------------------------------------------------- \n");
    printf("Account Details:\n\n");
    printf("Username: ");
    for (int i = 0; i < 8; i++)
    {
        if (username[i] != ' ')
            printf("%c", username[i]);
    }
    printf("\nBalance:  %.02f\n\n", Balance);
    printf("-------------------------------------------------- \n");
    printf("Portfolio:\n\n");
    printf("\n");
    for (int i = 0; i < STOCKS_COUNT; i++)
    {
        if (Stocks[i].units > 0)
        {
            printf("%2d. Stock-Name: %s  | Units: %4d\n\n", counter, Stocks[i].StockName, Stocks[i].units);
            printf("    Current Price Per Unit: %.02f\n", Stocks[i].current_ppu);
            printf("    Buying Price Per Unit:  %.02f\n", Stocks[i].buying_ppu);
            curr_profit = Stocks[i].current_ppu * Stocks[i].units - Stocks[i].buying_ppu * Stocks[i].units;
            printf("    Stock Total Profit:     %.02f\n", curr_profit);
            printf("-------------------------------------------------- \n");
            total_profit += curr_profit;
            counter++;
        }
    }
    printf("Total Investment Portfolio Profit: %.02f\n", total_profit);
    printf("-------------------------------------------------- \n");
}

void account_update(int socket)
{
    // This function waiting for 'Get Details' response from the server and send 'Y'es or 'N'o as an Ack/Nack
    int recvBytes;
    recvBytes = 0;
    char currStockBuff[STOCK_SIZE * STOCKS_COUNT + MAX_INVEST_SIZE];
    memset(currStockBuff, 0, STOCK_SIZE * STOCKS_COUNT + MAX_INVEST_SIZE);
    while (recvBytes != STOCK_SIZE * STOCKS_COUNT + MAX_INVEST_SIZE)
    {
        recvBytes = my_recv(socket, currStockBuff, STOCK_SIZE * STOCKS_COUNT + MAX_INVEST_SIZE);
        if (recvBytes == STOCK_SIZE * STOCKS_COUNT + MAX_INVEST_SIZE)
            my_send(socket, "Y", sizeof("Y"));
        else
            my_send(socket, "N", sizeof("N"));
    }
    for (int m = 0; m < STOCKS_COUNT; m++)
    {
        char f1[5], f2[9], f3[9], f4[MAX_INVEST_SIZE + 1];
        snprintf(Stocks[m].StockName, MAX_COMPANY_NAME, "%s", currStockBuff + STOCK_SIZE * m);
        strncpy(f1, currStockBuff + STOCK_SIZE * m + (MAX_COMPANY_NAME - 1), 4);
        strncpy(f2, currStockBuff + STOCK_SIZE * m + (MAX_COMPANY_NAME - 1) + 4, 7);
        strncpy(f3, currStockBuff + STOCK_SIZE * m + (MAX_COMPANY_NAME - 1) + 11, 7);
        strncpy(f4, currStockBuff + STOCK_SIZE * m + (MAX_COMPANY_NAME - 1) + 18, MAX_INVEST_SIZE);
        Stocks[m].units = atoi(f1);
        if (Stocks[m].units > 0)
            StockAmount++;
        Stocks[m].buying_ppu = atof(f2);
        Stocks[m].current_ppu = atof(f3);
        Balance = atof(f4);
    }
}
int my_send(int client_socket, char *buffer, int size)
{
    // Send function with error handling
    int send_val;
    send_val = (int)send(client_socket, buffer, size, 0);
    if (send_val <= 0)
    {
        if (mcast_on) close(mcast_socket);
        close(client_socket);
        printf("\nOops, our server is currently down, please try again later.\n");
        exit(EXIT_FAILURE);
    }
    return send_val;
}

int my_recv(int client_socket, char *buffer, int size)
{
    // Receive function with error handling
    memset(buffer, 0, size);
    int recv_val;
    recv_val = (int)recv(client_socket, buffer, size, 0);

    if (recv_val <= 0)
    {
        if (mcast_on) close(mcast_socket);
        close(client_socket);
        printf("\nOops, our server is currently down, please try again later.\n");
        exit(EXIT_FAILURE);
    }
    return (int)recv_val;
}
