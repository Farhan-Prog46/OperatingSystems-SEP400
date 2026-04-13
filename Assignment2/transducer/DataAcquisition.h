#ifndef _DATAACQUISITION_H_
#define _DATAACQUISITION_H_

#include <iostream>
#include <queue>
#include <vector>
#include <string>
#include <cstring>
#include <cerrno>
#include <cstdlib>
#include <algorithm>
#include <fcntl.h>
#include <pthread.h>
#include <semaphore.h>
#include <signal.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/ipc.h>
#include <sys/shm.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#include "SeismicData.h"

static void interruptHandler(int signum);
void* sharedMemoryReadThread(void* arg);
void* udpReadThread(void* arg);
void* udpWriteThread(void* arg);

struct DataPacket {
    unsigned short packetNo;
    unsigned short packetLen;
    char data[BUF_LEN];
};

struct Subscriber {
    std::string username;
    std::string ipAddress;
    int port;
    sockaddr_in addr;
};

class DataAcquisition {
    bool is_running;

    sem_t* sem_id1;
    key_t ShmKey;
    int ShmID;
    SeismicMemory* ShmPTR;
    int seismicDataIndex;

    int fd;

public:
    std::queue<DataPacket> dataQueue;
    std::vector<Subscriber> subscribers;
    std::vector<std::string> rogueIPs;
    std::vector<std::string> lastThreeIPs;

    pthread_mutex_t queueMutex;
    pthread_mutex_t subscriberMutex;
    pthread_mutex_t rogueMutex;

    DataAcquisition();
    ~DataAcquisition();

    int run();
    void shutdown();

    void readFromSharedMemory();
    void readFromUDP();
    void writeToSubscribers();

    bool subscriberExists(const std::string& username);
    void addSubscriber(const std::string& username, const sockaddr_in& clientAddr);
    void removeSubscriber(const std::string& username);
    std::vector<std::string> split(const std::string& text, char delimiter);

    bool isRogueIP(const std::string& ipAddress);
    void recordIncomingIP(const std::string& ipAddress);
    void addRogueIP(const std::string& ipAddress);

    static DataAcquisition* instance;
};

#endif