#include "DataAcquisition.h"

using namespace std;

DataAcquisition* DataAcquisition::instance = nullptr;

static void interruptHandler(int signum)
{
    if (signum == SIGINT) {
        DataAcquisition::instance->shutdown();
    }
}

void* sharedMemoryReadThread(void* arg)
{
    DataAcquisition* unit = (DataAcquisition*)arg;
    unit->readFromSharedMemory();
    pthread_exit(NULL);
}

void* udpReadThread(void* arg)
{
    DataAcquisition* unit = (DataAcquisition*)arg;
    unit->readFromUDP();
    pthread_exit(NULL);
}

void* udpWriteThread(void* arg)
{
    DataAcquisition* unit = (DataAcquisition*)arg;
    unit->writeToSubscribers();
    pthread_exit(NULL);
}

DataAcquisition::DataAcquisition()
{
    is_running = false;
    sem_id1 = nullptr;
    ShmPTR = nullptr;
    ShmID = -1;
    seismicDataIndex = 0;
    fd = -1;

    pthread_mutex_init(&queueMutex, NULL);
    pthread_mutex_init(&subscriberMutex, NULL);
    pthread_mutex_init(&rogueMutex, NULL);

    DataAcquisition::instance = this;
}

DataAcquisition::~DataAcquisition()
{
    pthread_mutex_destroy(&queueMutex);
    pthread_mutex_destroy(&subscriberMutex);
    pthread_mutex_destroy(&rogueMutex);
}

void DataAcquisition::shutdown()
{
    cout << "DataAcquisition::shutdown:" << endl;
    is_running = false;
}

vector<string> DataAcquisition::split(const string& text, char delimiter)
{
    vector<string> parts;
    string current = "";

    for (size_t i = 0; i < text.size(); i++) {
        if (text[i] == delimiter) {
            parts.push_back(current);
            current = "";
        } else {
            current += text[i];
        }
    }

    parts.push_back(current);
    return parts;
}

bool DataAcquisition::subscriberExists(const string& username)
{
    for (size_t i = 0; i < subscribers.size(); i++) {
        if (subscribers[i].username == username) {
            return true;
        }
    }
    return false;
}

void DataAcquisition::addSubscriber(const string& username, const sockaddr_in& clientAddr)
{
    Subscriber sub;
    sub.username = username;
    sub.ipAddress = inet_ntoa(clientAddr.sin_addr);
    sub.port = ntohs(clientAddr.sin_port);
    sub.addr = clientAddr;

    subscribers.push_back(sub);

    cout << "username:" << username << " Subscribed!!" << endl;
}

void DataAcquisition::removeSubscriber(const string& username)
{
    for (vector<Subscriber>::iterator it = subscribers.begin(); it != subscribers.end(); ++it) {
        if (it->username == username) {
            cout << "username:" << username << " Cancelled" << endl;
            subscribers.erase(it);
            return;
        }
    }
}

bool DataAcquisition::isRogueIP(const string& ipAddress)
{
    for (size_t i = 0; i < rogueIPs.size(); i++) {
        if (rogueIPs[i] == ipAddress) {
            return true;
        }
    }
    return false;
}

void DataAcquisition::addRogueIP(const string& ipAddress)
{
    if (!isRogueIP(ipAddress)) {
        rogueIPs.push_back(ipAddress);
        cout << "ROGUE CLIENT DETECTED: " << ipAddress << endl;
    }
}

void DataAcquisition::recordIncomingIP(const string& ipAddress)
{
    pthread_mutex_lock(&rogueMutex);

    lastThreeIPs.push_back(ipAddress);
    if (lastThreeIPs.size() > 3) {
        lastThreeIPs.erase(lastThreeIPs.begin());
    }

    if (lastThreeIPs.size() == 3 &&
        lastThreeIPs[0] == lastThreeIPs[1] &&
        lastThreeIPs[1] == lastThreeIPs[2]) {
        addRogueIP(ipAddress);
    }

    pthread_mutex_unlock(&rogueMutex);
}

void DataAcquisition::readFromSharedMemory()
{
    while (is_running) {
        sem_wait(sem_id1);

        if (ShmPTR->seismicData[seismicDataIndex].status == WRITTEN) {
            DataPacket packet;

            packet.packetNo = (unsigned short)ShmPTR->packetNo;
            packet.packetLen = ShmPTR->seismicData[seismicDataIndex].packetLen;

            memcpy(packet.data,
                   ShmPTR->seismicData[seismicDataIndex].data,
                   packet.packetLen);

            pthread_mutex_lock(&queueMutex);
            dataQueue.push(packet);
            pthread_mutex_unlock(&queueMutex);

            ShmPTR->seismicData[seismicDataIndex].status = READ;

            seismicDataIndex++;
            if (seismicDataIndex >= NUM_DATA) {
                seismicDataIndex = 0;
            }
        }

        sem_post(sem_id1);
        sleep(1);
    }
}

void DataAcquisition::readFromUDP()
{
    char buf[4096];
    sockaddr_in clientAddr;
    socklen_t addrLen = sizeof(clientAddr);

    while (is_running) {
        memset(buf, 0, sizeof(buf));

        int len = recvfrom(fd, buf, sizeof(buf) - 1, 0,
                           (sockaddr*)&clientAddr, &addrLen);

        if (len <= 0) {
            sleep(1);
            continue;
        }

        string clientIP = inet_ntoa(clientAddr.sin_addr);

        recordIncomingIP(clientIP);

        pthread_mutex_lock(&rogueMutex);
        bool rogue = isRogueIP(clientIP);
        pthread_mutex_unlock(&rogueMutex);

        if (rogue) {
            cout << "Ignoring packet from rogue client " << clientIP << endl;
            continue;
        }

        buf[len] = '\0';
        string message(buf);

        vector<string> parts = split(message, ',');

        if (parts.size() >= 3 && parts[0] == "Subscribe") {
            string username = parts[1];
            string password = parts[2];

            if (password == "Leaf") {
                pthread_mutex_lock(&subscriberMutex);

                if (!subscriberExists(username)) {
                    addSubscriber(username, clientAddr);
                    sendto(fd, "Subscribed", 10, 0,
                           (sockaddr*)&clientAddr, sizeof(clientAddr));
                } else {
                    cout << username << " has already subscribed" << endl;
                }

                pthread_mutex_unlock(&subscriberMutex);
            } else {
                cout << "Bad password from "
                     << clientIP << ":"
                     << ntohs(clientAddr.sin_port) << endl;
            }
        }
        else if (parts.size() >= 2 && parts[0] == "Cancel") {
            string username = parts[1];

            pthread_mutex_lock(&subscriberMutex);
            removeSubscriber(username);
            pthread_mutex_unlock(&subscriberMutex);
        }
        else {
            cout << "unknown command " << message << endl;
        }
    }
}

void DataAcquisition::writeToSubscribers()
{
    while (is_running) {
        while (true) {
            DataPacket packet;
            bool hasPacket = false;

            pthread_mutex_lock(&queueMutex);
            if (!dataQueue.empty()) {
                packet = dataQueue.front();
                dataQueue.pop();
                hasPacket = true;
            }
            pthread_mutex_unlock(&queueMutex);

            if (!hasPacket) {
                break;
            }

            unsigned char sendBuffer[2 + 1 + BUF_LEN];
            memset(sendBuffer, 0, sizeof(sendBuffer));

            unsigned short packetNoNet = htons(packet.packetNo);
            memcpy(sendBuffer, &packetNoNet, 2);

            sendBuffer[2] = (unsigned char)packet.packetLen;

            memcpy(sendBuffer + 3, packet.data, packet.packetLen);

            pthread_mutex_lock(&subscriberMutex);
            for (size_t i = 0; i < subscribers.size(); i++) {
                sendto(fd,
                       sendBuffer,
                       3 + packet.packetLen,
                       0,
                       (sockaddr*)&subscribers[i].addr,
                       sizeof(subscribers[i].addr));
            }
            pthread_mutex_unlock(&subscriberMutex);
        }

        sleep(1);
    }
}

int DataAcquisition::run()
{
    struct sigaction action;
    action.sa_handler = interruptHandler;
    sigemptyset(&action.sa_mask);
    action.sa_flags = 0;
    sigaction(SIGINT, &action, NULL);

    ShmKey = ftok(MEMNAME, 65);
    if (ShmKey == -1) {
        cout << "DataAcquisition: ftok() error" << endl;
        cout << strerror(errno) << endl;
        return -1;
    }

    ShmID = shmget(ShmKey, sizeof(SeismicMemory), 0666);
    if (ShmID < 0) {
        cout << "DataAcquisition: shmget() error" << endl;
        cout << strerror(errno) << endl;
        return -1;
    }

    ShmPTR = (SeismicMemory*)shmat(ShmID, NULL, 0);
    if (ShmPTR == (void*)-1) {
        cout << "DataAcquisition: shmat() error" << endl;
        cout << strerror(errno) << endl;
        return -1;
    }

    sem_id1 = sem_open(SEMNAME, 0);
    if (sem_id1 == SEM_FAILED) {
        cout << "DataAcquisition: sem_open() error" << endl;
        cout << strerror(errno) << endl;
        return -1;
    }

    fd = socket(AF_INET, SOCK_DGRAM | SOCK_NONBLOCK, 0);
    if (fd < 0) {
        cout << "DataAcquisition: socket() error" << endl;
        cout << strerror(errno) << endl;
        return -1;
    }

    sockaddr_in myaddr;
    memset(&myaddr, 0, sizeof(myaddr));
    myaddr.sin_family = AF_INET;
    myaddr.sin_addr.s_addr = inet_addr("127.0.0.1");
    myaddr.sin_port = htons(1153);

    if (bind(fd, (sockaddr*)&myaddr, sizeof(myaddr)) < 0) {
        cout << "DataAcquisition: bind() error" << endl;
        cout << strerror(errno) << endl;
        return -1;
    }

    is_running = true;

    pthread_t shmThread;
    pthread_t readThread;
    pthread_t writeThread;

    if (pthread_create(&shmThread, NULL, sharedMemoryReadThread, this) != 0) {
        cout << "Cannot create shared memory read thread" << endl;
        cout << strerror(errno) << endl;
        return -1;
    }

    if (pthread_create(&readThread, NULL, udpReadThread, this) != 0) {
        cout << "Cannot create UDP read thread" << endl;
        cout << strerror(errno) << endl;
        return -1;
    }

    if (pthread_create(&writeThread, NULL, udpWriteThread, this) != 0) {
        cout << "Cannot create UDP write thread" << endl;
        cout << strerror(errno) << endl;
        return -1;
    }

    pthread_join(shmThread, NULL);
    pthread_join(readThread, NULL);
    pthread_join(writeThread, NULL);

    if (fd >= 0) close(fd);
    sem_close(sem_id1);
    shmdt((void*)ShmPTR);

    cout << "DataAcquisition: DONE" << endl;
    return 0;
}