#include <stdlib.h>
#include <iostream>
#include <string.h>

#include "ghostServer.h"

#ifdef _WIN32
#include <Windows.h>
#endif
volatile int CMOMGhostServer::numPlayers;
std::vector<playerData* > CMOMGhostServer::m_vecPlayers;
std::mutex CMOMGhostServer::m_vecPlayers_mutex;
std::mutex CMOMGhostServer::m_bShouldExit_mutex;

zed_net_socket_t CMOMGhostServer::m_Socket;
bool CMOMGhostServer::m_bShouldExit = false;
int CMOMGhostServer::m_iTickRate;
char CMOMGhostServer::m_szMapName[64];

typedef std::chrono::high_resolution_clock Clock;

int main(int argc, char** argv)
{
    int status = -1;
    if (argc == 4)
    {
        status = CMOMGhostServer::runGhostServer((unsigned short)atoi(argv[1]), argv[3]);
    }
    else
    {
        status = CMOMGhostServer::runGhostServer(DEFAULT_PORT, DEFAULT_MAP);
    }

    if (status != 0)
        return 0;
#ifdef _WIN32
    SetConsoleMode(stdin, ENABLE_LINE_INPUT); //ignores mouse input on the input buffer
#endif
    std::thread t(CMOMGhostServer::acceptNewConnections); //create a new thread that listens for incoming client connections
    t.detach(); //continuously run thread

    while (!CMOMGhostServer::m_bShouldExit)
    {
        CMOMGhostServer::handleConsoleInput();
    }
    zed_net_shutdown();

    return 0;
}

int CMOMGhostServer::runGhostServer(const unsigned short port, const char* mapName)
{
    zed_net_init();

    zed_net_tcp_socket_open(&m_Socket, port, 0, 1);
    conMsg("Running ghost server on %s on port %d!\n", mapName, port);
    _snprintf(m_szMapName, sizeof(m_szMapName), "%s", mapName);
     
    return 0;

}
const void CMOMGhostServer::newConnection(zed_net_socket_t socket, zed_net_address_t address)
{ 
    const char* host;
    int data;
    host = zed_net_host_to_str(address.host);
    conMsg("Accepted connection from %s:%d\n", host, address.port);

    host = zed_net_host_to_str(address.host);
    int bytes_read = zed_net_tcp_socket_receive(&socket, &data, sizeof(data));
    if (bytes_read)
    {
        //printf("Received %d bytes from %s:%d:\n", bytes_read, host, address.port);
        if (data == MOM_SIGNON) //Player signs on for the first time
        {
            //printf("Data matches MOM_SIGNON pattern!\n");

            //send ACK to client that they are connected. This is the current mapname.
            zed_net_tcp_socket_send(&socket, &m_szMapName, sizeof(m_szMapName));

            //Describes a newly connected player with client idx equal to the current number of players
            playerData *newPlayer = new playerData(socket, address, numPlayers); 

            m_vecPlayers_mutex.lock();

            m_vecPlayers.push_back(newPlayer);
            numPlayers = m_vecPlayers.size();

            m_vecPlayers_mutex.unlock();

            conMsg("There are now %i connected players.\n", numPlayers);
            //listen(sock->handle, SOMAXCONN) != 0
            while (newPlayer->remote_socket.ready == 0) //socket is open
            {
                handlePlayer(newPlayer);
            }
            delete newPlayer;
        }
    }
    //printf("Thread terminating\n");
    //End of thread
}
void CMOMGhostServer::acceptNewConnections()
{
    zed_net_socket_t remote_socket;
    zed_net_address_t remote_address;

    while (!m_bShouldExit)
    {
        zed_net_tcp_accept(&m_Socket, &remote_socket, &remote_address);

        std::thread t(newConnection, remote_socket, remote_address); //create a new thread to deal with the connection
        t.detach(); //each connection is dealt with in a seperate thread
    }
}
void CMOMGhostServer::handleConsoleInput()
{
    char buffer[256], command[256], argument[256];
    fgets(buffer, 256, stdin);
    buffer[strlen(buffer) - 1] = '\0';

    if (strlen(buffer) > 0)
    {
        _snprintf(argument-1, sizeof(argument), "%s", strpbrk(buffer, " "));
        _snprintf(command, sizeof(command), "%s", strtok(buffer, " "));
    }
    if (strcmp(command, "say") == 0)
    {
        conMsg("You tried to say: \"%s\" to the server\n", argument);
    }
    if (strcmp(command, "exit") == 0 || strcmp(command, "quit") == 0)
    {
        m_bShouldExit_mutex.lock();
        m_bShouldExit = true;
        m_bShouldExit_mutex.unlock();
    }
    if (strcmp(command, "help") == 0)
    {
        conMsg("Usage: ghost_server <port> -map <mapname> to start on custom port/map. \n");
        conMsg("Commands: numplayers, currentmap, say, map, exit\n");
    }
    if (strcmp(command, "numplayers") == 0)
    {
        conMsg("Number of connected players: %i\n", numPlayers);
    }
    if (strcmp(command, "map") == 0)
    {
        _snprintf(m_szMapName, sizeof(m_szMapName), argument); 
        conMsg("Changing map to %s...\n", argument);
    }
    if (strcmp(command, "currentmap") == 0)
    {
        conMsg("Current map is: %s\n", m_szMapName);
    }
}
void CMOMGhostServer::handlePlayer(playerData *newPlayer)
{
    int data, bytes_read = 0;
    auto t1 = Clock::now(); 
    bytes_read = zed_net_tcp_socket_receive(&newPlayer->remote_socket, &data, sizeof(data));
    while (bytes_read != sizeof(data)) //Oops, we didn't get anything from the client!
    {
        auto t2 = Clock::now();
        auto deltaT = std::chrono::duration_cast<std::chrono::seconds>(t2 - t1);
        conMsg("Lost connection! Waiting to time out... %ll\n", deltaT.count());
        std::this_thread::sleep_for(std::chrono::seconds(1));
        if (deltaT > std::chrono::seconds(SECONDS_TO_TIMEOUT))
        {
            conMsg("Player timed out...\n");
            disconnectPlayer(newPlayer);
            break;
        }
    }
    if (bytes_read && newPlayer)
    {
        if (data == MOM_C_SENDING_NEWFRAME)
        {
            //printf("Data matches MOM_C_SENDING_NEWFRAME pattern! \n"); 
            data = MOM_C_RECIEVING_NEWFRAME;
            zed_net_tcp_socket_send(&newPlayer->remote_socket, &data, sizeof(data)); //SYN-ACK
            // Wait for client to get our acknowledgement, and recieve frame update from client
            zed_net_tcp_socket_receive(&newPlayer->remote_socket, &newPlayer->currentFrame, sizeof(ghostNetFrame)); //ACK
        }
        if (data == MOM_S_RECIEVING_NEWFRAME) //SYN
        {
            //printf("Data matches MOM_S_RECIEVING_NEWFRAME pattern! ..\n");
            // Send out a ACK to the client that we're going to be sending them an update
            data = MOM_S_SENDING_NEWFRAME;
            zed_net_tcp_socket_send(&newPlayer->remote_socket, &data, sizeof(data)); //ACK

            int playerNum = numPlayers;
            //printf("Sending number of players: %i\n", playerNum);
            zed_net_tcp_socket_send(&newPlayer->remote_socket, &playerNum, sizeof(playerNum));

            //printf("Sending player data \n");
            for (int i = 0; i < playerNum; i++)
            {
                m_vecPlayers_mutex.lock();
                zed_net_tcp_socket_send(&newPlayer->remote_socket, &m_vecPlayers[i]->currentFrame, sizeof(ghostNetFrame)); 
                m_vecPlayers_mutex.unlock();
            }
            
        }
        if (data == MOM_SIGNOFF)
        {
            conMsg("Data matches MOM_SIGNOFF pattern! Closing socket...\n");
            disconnectPlayer(newPlayer);
            //printf("Remote socket status: %i\n", newPlayer->remote_socket.ready);
        }
    }
    //std::this_thread::sleep_for(std::chrono::milliseconds(10));
}
void CMOMGhostServer::disconnectPlayer(playerData *player)
{
    int data = MOM_SIGNOFF;
    zed_net_tcp_socket_send(&player->remote_socket, &data, sizeof(data)); //send back an ACK that they are disconnecting.
    zed_net_socket_close(&player->remote_socket);
    m_vecPlayers_mutex.lock();

    m_vecPlayers.erase(m_vecPlayers.begin() + player->clientIndex);
    numPlayers = m_vecPlayers.size();

    m_vecPlayers_mutex.unlock();
    conMsg("There are now %i connected players.\n", numPlayers);
}
//A replacement for printf that prints the time as well as the message. 
void CMOMGhostServer::conMsg(const char* msg, ...)
{
    va_list list;
    va_start(list, msg);
    char time[64], msgBuffer[256];
    _snprintf(time, sizeof(time), "%s", currentDateTime().c_str());
    _snprintf(msgBuffer, sizeof(msgBuffer), "%s - %s", time, msg);
    vprintf(msgBuffer, list);
}