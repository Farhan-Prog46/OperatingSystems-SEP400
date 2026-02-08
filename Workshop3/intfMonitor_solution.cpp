//intfMonitor_solution.cpp - An interface monitor


#include <fcntl.h>
#include <cstring>
#include <fstream>
#include <iostream>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#include <signal.h>   
#include <errno.h>    

using namespace std;

const int MAXBUF=128;
bool isRunning=false;

// ✅ TODO: Declare your signal handler function prototype
static void sigHandler(int sig);

int main(int argc, char *argv[])
{
    // ✅ TODO: Declare a variable of type struct sigaction
    struct sigaction action;
    action.sa_handler = sigHandler;
    sigemptyset(&action.sa_mask);
    action.sa_flags = 0;

    char interface[MAXBUF];
    char statPath[MAXBUF];
    const char logfile[]="Network.log";//store network data in Network.log
    int retVal=0;

    // ✅ TODO: Register signal handlers for SIGUSR1, SIGUSR2, ctrl-C and ctrl-Z
    retVal = sigaction(SIGUSR1, &action, NULL);
    if(retVal < 0) { cout << strerror(errno) << endl; return 1; }

    retVal = sigaction(SIGUSR2, &action, NULL);
    if(retVal < 0) { cout << strerror(errno) << endl; return 1; }

    retVal = sigaction(SIGINT, &action, NULL);   // ctrl-C
    if(retVal < 0) { cout << strerror(errno) << endl; return 1; }

    retVal = sigaction(SIGTSTP, &action, NULL);  // ctrl-Z
    if(retVal < 0) { cout << strerror(errno) << endl; return 1; }

    strncpy(interface, argv[1], MAXBUF);//The interface has been passed as an argument to intfMonitor
    int fd=open(logfile, O_RDWR | O_CREAT | O_APPEND, S_IRUSR | S_IWUSR);
    cout<<"intfMonitor:main: interface:"<<interface<<":  pid:"<<getpid()<<endl;

    // ✅ TODO: Wait for SIGUSR1 - the start signal from the parent
    while(!isRunning) {
        pause(); // sleep until a signal arrives
    }

    while(isRunning) {
        //gather some stats
        int tx_bytes=0;
        int rx_bytes=0;
        int tx_packets=0;
        int rx_packets=0;
        ifstream infile;
        sprintf(statPath, "/sys/class/net/%s/statistics/tx_bytes", interface);
        infile.open(statPath);
        if(infile.is_open()) {
            infile>>tx_bytes;
            infile.close();
        }
        sprintf(statPath, "/sys/class/net/%s/statistics/rx_bytes", interface);
        infile.open(statPath);
        if(infile.is_open()) {
            infile>>rx_bytes;
            infile.close();
        }
        sprintf(statPath, "/sys/class/net/%s/statistics/tx_packets", interface);
        infile.open(statPath);
        if(infile.is_open()) {
            infile>>tx_packets;
            infile.close();
        }
        sprintf(statPath, "/sys/class/net/%s/statistics/rx_packets", interface);
        infile.open(statPath);
        if(infile.is_open()) {
            infile>>rx_packets;
            infile.close();
        }
        char data[MAXBUF];
        //write the stats into Network.log
        int len=sprintf(data, "%s: tx_bytes:%d rx_bytes:%d tx_packets:%d rx_packets: %d\n",
                        interface, tx_bytes, rx_bytes, tx_packets, rx_packets);
        write(fd, data, len);
        sleep(1);
    }
    close(fd);

    return 0;
}

// ✅ TODO: Create a signal handler...
static void sigHandler(int sig)
{
    switch(sig) {
        case SIGUSR1:
            cout << "intfMonitor: starting up" << endl;
            isRunning = true;
            break;

        case SIGINT: // ctrl-C
            cout << "intfMonitor: ctrl-C discarded" << endl;
            break;

        case SIGTSTP: // ctrl-Z
            cout << "intfMonitor: ctrl-Z discarded" << endl;
            break;

        case SIGUSR2:
            cout << "intfMonitor: shutting down" << endl;
            isRunning = false;
            break;

        default:
            cout << "intfMonitor: undefined signal" << endl;
            break;
    }
}
