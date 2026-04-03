// client2.cpp - A client that communicates with a second client using RSA encryption/decryption
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
int srcPort = 1155;
int destPort = 1153;

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
        "You were lucky to have a room. We used to have to live in a corridor.",
        "Oh we used to dream of livin' in a corridor! Woulda' been a palace to us.",
        "We used to live in an old water tank on a rubbish tip.",
        "We got woken up every morning by having a load of rotting fish dumped all over us.",
        "Quit"
    };

    signal(SIGINT, shutdownHandler);
