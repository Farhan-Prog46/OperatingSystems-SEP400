// client1.cpp - A client that communicates with a second client using RSA encryption/decryption
#include <arpa/inet.h>
#include <iostream>
#include <math.h>
#include <netinet/in.h>
#include <pthread.h>
#include <queue>
#include <signal.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>

using namespace std;

const char IP_ADDR[] = "127.0.0.1";
const int BUF_LEN = 256;

bool is_running = true;
int srcPort = 1153;
int destPort = 1155;

// Encryption/Decryption variables
double n;
double e;
double d;
double phi;

queue<string> messageQueue;
pthread_mutex_t lock_x;

struct ThreadArgs {
    int sockfd;
};

struct Packet {
    int len;
    int data[BUF_LEN];
};

void *recv_func(void *arg);

static void shutdownHandler(int sig)
{
    if (sig == SIGINT) {
        is_running = false;
    }
}

// Returns a^b mod c
unsigned char PowerMod(int a, int b, int c)
{
    int res = 1;
    for (int i = 0; i < b; ++i) {
        res = (res * a) % c;
    }
    return (unsigned char)res;
}

// Returns gcd of a and b
int gcd(int a, int h)
{
    int temp;
    while (1) {
        temp = a % h;
        if (temp == 0)
            return h;
        a = h;
        h = temp;
    }
}

void encryptMessage(const unsigned char *msg, Packet &pkt)
{
    pkt.len = (int)strlen((const char *)msg);

    for (int i = 0; i < pkt.len; i++) {
        pkt.data[i] = (int)PowerMod((int)msg[i], (int)e, (int)n);
    }

    for (int i = pkt.len; i < BUF_LEN; i++) {
        pkt.data[i] = 0;
    }
}

string decryptMessage(const Packet &pkt)
{
    char plain[BUF_LEN];
    int length = pkt.len;

    if (length >= BUF_LEN)
        length = BUF_LEN - 1;

    for (int i = 0; i < length; i++) {
        plain[i] = (char)PowerMod(pkt.data[i], (int)d, (int)n);
    }

    plain[length] = '\0';
    return string(plain);
}

void printQueuedMessages()
{
    pthread_mutex_lock(&lock_x);

    while (!messageQueue.empty()) {
        cout << "Received: " << messageQueue.front() << endl;
        messageQueue.pop();
    }

    pthread_mutex_unlock(&lock_x);
}

int main()
{
    // Two prime numbers
    double p = 11;
    double q = 23;

    // First part of public key
    n = p * q;

    // Finding e
    e = 2;
    phi = (p - 1) * (q - 1);

    while (e < phi) {
        if (gcd((int)e, (int)phi) == 1)
            break;
        else
            e++;
    }

    // Finding d
    int k = 2;
    d = (1 + (k * phi)) / e;

    cout << "p:" << (int)p
         << " q:" << (int)q
         << " n:" << (int)n
         << " phi:" << (int)phi
         << " e:" << (int)e
         << " d:" << (int)d << endl;

    const int numMessages = 5;
    const unsigned char messages[numMessages][BUF_LEN] = {
        "House? You were lucky to have a house!",
        "We used to live in one room, all hundred and twenty-six of us, no furniture.",
        "Half the floor was missing;",
        "we were all huddled together in one corner for fear of falling.",
        "Quit"
    };

    signal(SIGINT, shutdownHandler);

    pthread_mutex_init(&lock_x, NULL);

    int sockfd = socket(AF_INET, SOCK_DGRAM, 0);
    if (sockfd < 0) {
        cerr << "socket() failed" << endl;
        return 1;
    }

    sockaddr_in srcAddr, destAddr;
    memset(&srcAddr, 0, sizeof(srcAddr));
    memset(&destAddr, 0, sizeof(destAddr));

    srcAddr.sin_family = AF_INET;
    srcAddr.sin_port = htons(srcPort);
    srcAddr.sin_addr.s_addr = inet_addr(IP_ADDR);

    if (bind(sockfd, (sockaddr *)&srcAddr, sizeof(srcAddr)) < 0) {
        cerr << "bind() failed" << endl;
        close(sockfd);
        return 1;
    }

    destAddr.sin_family = AF_INET;
    destAddr.sin_port = htons(destPort);
    destAddr.sin_addr.s_addr = inet_addr(IP_ADDR);

    timeval tv;
    tv.tv_sec = 1;
    tv.tv_usec = 0;
    setsockopt(sockfd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

    pthread_t recvThread;
    ThreadArgs args;
    args.sockfd = sockfd;

    if (pthread_create(&recvThread, NULL, recv_func, &args) != 0) {
        cerr << "pthread_create() failed" << endl;
        close(sockfd);
        return 1;
    }

    sleep(5);

    for (int i = 0; i < numMessages && is_running; i++) {
        printQueuedMessages();

        Packet pkt;
        encryptMessage(messages[i], pkt);

        sendto(sockfd, &pkt, sizeof(pkt), 0, (sockaddr *)&destAddr, sizeof(destAddr));

        sleep(1);
    }

    while (is_running) {
        printQueuedMessages();
        sleep(1);
    }

    pthread_join(recvThread, NULL);

    printQueuedMessages();

    close(sockfd);
    pthread_mutex_destroy(&lock_x);

    cout << "client1 is quitting..." << endl;
    return 0;
}

void *recv_func(void *arg)
{
    ThreadArgs *args = (ThreadArgs *)arg;
    int sockfd = args->sockfd;

    while (is_running) {
        Packet pkt;
        sockaddr_in senderAddr;
        socklen_t addrLen = sizeof(senderAddr);

        int bytes = recvfrom(sockfd, &pkt, sizeof(pkt), 0,
                             (sockaddr *)&senderAddr, &addrLen);

        if (bytes < 0) {
            continue;
        }

        string decrypted = decryptMessage(pkt);

        if (decrypted == "Quit") {
            is_running = false;
            break;
        }

        pthread_mutex_lock(&lock_x);
        messageQueue.push(decrypted);
        pthread_mutex_unlock(&lock_x);
    }

    return NULL;
}