#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/types.h>
#include <unistd.h>
#include <poll.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/sendfile.h>
#include <signal.h>
#include <dirent.h>
#include <time.h>
#include "./helpers.h"

#define MAX 4096
#define CLIENTS 1024
#define HTML_SZ 1 << 30
#define TIME 1000000
#define FOLDER_CONT 5000

#pragma region Variables
char htmlPath[MAX];
char *rootDir; //root directory

//clients data
int count, activeUsers;
struct pollfd *fds;
int *dfds;
off_t *offsets;

//used to read conections (req)
char buf[MAX];

//used to separate request parts
char *method, *url, *field, *order;
int sortRequest;

//used to safe an response of one request
char response[MAX];

//request directory
char req_dir[MAX];

//html data
char html[HTML_SZ];

#pragma endregion

#pragma region Delete a client
void DeleteClient(int client)
{
    close(fds[client].fd);
    close(dfds[client]);
    fds[client].fd = -1;
    --activeUsers;
}

#pragma endregion

#pragma region Add a client

//Update clients data adding a new client
void addClient(int connctfd)
{
    fds[count].fd = connctfd;
    fds[count].events = POLLIN;
    dfds[count++] = -1;
    ++activeUsers;
}

#pragma endregion

#pragma region Initializations
//initializate all variables
void initializateAll(int fd)
{
    count = 0;
    activeUsers = 0;
    fds = calloc(MAX * sizeof(struct pollfd), sizeof(struct pollfd));
    dfds = calloc(MAX * sizeof(int), sizeof(int));
    offsets = calloc(MAX * sizeof(off_t), sizeof(off_t));

    addClient(fd);
}

#pragma endregion

#pragma region Download File

int downloadFile(int client, char *req)
{
    if (dfds[client] == -1)
    {
        dfds[client] = open(req, O_RDONLY);

        if (dfds[client] == -1)
            return 0;

        fds[client].events |= POLLOUT;
        offsets[client] = 0;

        struct stat sb;
        stat(req, &sb);

        bzero(&response, MAX);
        sprintf(response + strlen(response), "HTTP/1.1 200 OK\n");
        sprintf(response + strlen(response), "Content: %s\n", rindex(req, '/') + 1);
        sprintf(response + strlen(response), "Content-Length: %ld\n", sb.st_size);
        sprintf(response + strlen(response), "Content-Disposition: attachment\n\n");

        printMsg("Header of response:\n", response, "");

        writeHTML(fds[client].fd, response, strlen(response));
    }
    else
    {
        if (sendfile(fds[client].fd, dfds[client], &offsets[client], 1 << 20) > 0)
            return 0;
        DeleteClient(client);
    }

    return 1;
}

#pragma endregion

#pragma region Not implemented method

//Send an error, cause a request is not a GET method
int NotImplementedMethod(int client)
{
    bzero(&response, MAX);
    sprintf(response + strlen(response), "HTTP/1.1 501 Method Not Implemented\n");
    sprintf(response + strlen(response), "Allow: GET\n");
    sprintf(response + strlen(response), "Content-Length: 35\n\n");

    printMsg("Header of response:\n", response, "");

    sprintf(response + strlen(response), "ERROR 501 Method Not Implemented :(");
    writeHTML(fds[client].fd, response, strlen(response));

    DeleteClient(client);
    return 1;
}

#pragma endregion

#pragma region Get request

int getReq(int client)
{
    bzero(&buf, MAX);
    if (read(fds[client].fd, buf, MAX))
    {
        printMsg("Client's request:\n", buf, "");

        method = calloc(MAX * sizeof(char), sizeof(char));
        url = calloc(MAX * sizeof(char), sizeof(char));

        sscanf(buf, "%s %s", method, url);

        if (strcasecmp(method, "GET") && NotImplementedMethod(client))
        {
            printMsg("This server only support GET methods! %s", req_dir, "\n");
            return 0;
        }
        sortRequest = 0;
        if (strstr(url, "?"))
        {
            char *ptr = strdup(rindex(url, '?'));
            if (strlen(ptr) == 20) //in this point we have ?sort=name&order=asc
            {
                ptr[5] = ptr[16] = 0;
                if (strcmp(ptr + 1, "sort") == 0 && strcmp(ptr + 11, "order") == 0)
                {
                    ptr[5] = ptr[16] = '=';
                    printf("ok %s\n", ptr + 1);
                    url[rindex(url, '?') - url] = 0; //only route
                    field = calloc(4 * sizeof(char), sizeof(char));
                    order = calloc(3 * sizeof(char), sizeof(char));
                    sscanf(ptr + 1, "sort=%4s&order=%3s", field, order);
                    sortRequest = 1;
                }
            }
        }

        bzero(&req_dir, MAX);
        sprintf(req_dir, "%s%s", rootDir, url); //fill req_dir

        fixDir(req_dir);

        return 1;
    }
    return 0;
}

#pragma endregion

#pragma region Get HTML

//Generate HTML answer
int getHTML(char *dirName, int rootlen, char *html, int sortData, char *field, char *order)
{
    fixDir(dirName);

    char *decode, c;
    while (decode = index(dirName, '%'))
    {
        dirName[decode - dirName] = 0;
        sscanf(decode + 1, "%2hhx", &c);
        sprintf(dirName, "%s%c%s", dirName, c, decode + 3);
    }

    printMsg("Opening directory: ", dirName, "...\n");

    DIR *dirp = opendir(dirName);

    if (dirp == NULL)
    {
        printMsg("ERROR opening: ", dirName," is not a directory!\n\n");
        return 0;
    }

    printMsg("Directory: ", dirName," opened!\n\n");

    struct dirent *dir;
    int n = strlen(dirName);

    long unsigned int byte = 0;
    bzero(html, HTML_SZ);
    readHtml(htmlPath, byte, 12065, html);
    printf("%s", html);
    sprintf(html, "%s%c", html, '\0');
    byte += 12065;

    sprintf(html, "%s<header><h1>", html);
    // Build prefixes
    char *curp = (char *)calloc(512, sizeof(char));
    int last = 0;
    char *prefix = dirName;
    prefix = strdup(&prefix[rootlen]);

    for (int i = 0; i < strlen(prefix); i++)
    {
        if (prefix[i] == '/' || i == strlen(prefix) - 1)
        {
            strncat(curp, &prefix[i], 1);
            last += strlen(curp);
            sprintf(html, "%s<a href=\"%s\">%s</a>", html, strndup(prefix, last), strdup(curp));
            memset(curp, 0, 512);
        }
        else
            strncat(curp, &prefix[i], 1);
    }

    sprintf(html, "%s</h1></header>", html);

    readHtml(htmlPath, byte, 1983, html + strlen(html));
    sprintf(html, "%s%c", html, '\0');
    byte += 1983;

    struct stat folder[FOLDER_CONT], files[FOLDER_CONT];
    char *folderName[FOLDER_CONT], *filesName[FOLDER_CONT];
    int folderCount = 0, filesCount = 0;

    while (dir = readdir(dirp))
    {
        if (strcmp(dir->d_name, ".") == 0)
            continue;
        struct stat sb;
        sprintf(dirName + n, "/%s%c", dir->d_name, '\0');
        stat(dirName, &sb);

        if (ObtainPermissions(sb.st_mode)[0] == 'd')
        {
            folder[folderCount] = sb;
            folderName[folderCount++] = strdup(dirName);
        }
        else
        {
            files[filesCount] = sb;
            filesName[filesCount++] = strdup(dirName);
        }
    }

    Sort(folder, folderName, folderCount, sortData, field, order);

    int pos = 0, i = 0;
    while (strcmp(rindex(folderName[pos], '/') + 1, "..") != 0)
        ++pos;
    struct stat pp = folder[pos];
    char *ppdir = folderName[pos];
    while (pos > 0)
    {
        folder[pos] = folder[pos - 1];
        folderName[pos] = folderName[pos - 1];
        pos--;
    }
    folder[0] = pp;
    folderName[0] = ppdir;

    Sort(files, filesName, filesCount, sortData, field, order);

    AddToHTML(folder, folderName, folderCount, html, rootlen, n);
    AddToHTML(files, filesName, filesCount, html, rootlen, n);

    readHtml(htmlPath, byte, 1463, html + strlen(html));
    sprintf(html, "%s%c", html, '\0');

    printMsg("Closing directory: ", dirName, " ...\n");
    closedir(dirp);
    printMsg("Directory: ", dirName, " closed!\n\n");

    return 1;
}

#pragma endregion

#pragma region Send HTML

int sendHTML(int client, char *req)
{
    if (getHTML(req, strlen(rootDir), html, sortRequest, field, order))
    {
        bzero(&response, MAX);
        sprintf(response + strlen(response), "HTTP/1.1 200 \n");
        sprintf(response + strlen(response), "Content-type: text/html; charset: UTF-8\n");
        sprintf(response + strlen(response), "Content-Length: %li\n\n", strlen(html));

        printMsg("Header of response: \n", response, "");

        writeHTML(fds[client].fd, response, strlen(response));
        writeHTML(fds[client].fd, html, strlen(html));

        DeleteClient(client);
        return 1;
    }
    return 0;
}

#pragma endregion

#pragma region Response a client
void responseClient(int client)
{
    if (fds[client].revents & POLLIN)
    {
        if (getReq(client)) //direction to req in req_dir
        {
            if (sendHTML(client, req_dir))
                printMsg("Html of folder: ", req_dir," was sended!\n");
            else if (downloadFile(client, req_dir))
                printMsg("File: ", req_dir, " is sending ...\n");
            else
            {
                //Return an "Not Found" html page
                bzero(&response, MAX);
                sprintf(response + strlen(response), "HTTP/1.1 404 Not Found\n");
                sprintf(response + strlen(response), "Content-Type: text/html; charset=UTF-8\n");
                sprintf(response + strlen(response), "Content-Length: 28\n\n");

                printMsg("Header of response:\n", response, "");

                sprintf(response + strlen(response), "ERROR 404: File not found :(");
                writeHTML(fds[client].fd, response, strlen(response));

                DeleteClient(client);
                printMsg("Can't resolve: ", req_dir, "\n");
            }

            if (fds[client].fd == -1)
                printMsg("Socket close! \n\n", "", "");
        }
        else
            DeleteClient(client);
    }
    else if (fds[client].revents & POLLOUT)
    {
        if (downloadFile(client, req_dir))
            printMsg("A file was sended!\n", "", "");
        else
            printMsg("A file is sending ...\n", "", "");
    }
}
#pragma endregion

int main(int argc, char **argv)
{
    signal(SIGPIPE, SIG_IGN);
    strcpy(htmlPath, strdup(getenv("PWD")));
    strcat(htmlPath, "/caddy.html");

    int listenfd; //socken listen fd
    int connctfd; //client comunication fd
    int port;

    struct sockaddr_in serveraddr;
    struct sockaddr_in clientaddr;

    if (argc != 3)
    {
        printf("Usage %s <port> <path>", argv[0]);
        exit(1);
    }

    port = atoi(argv[1]);
    rootDir = argv[2];

    //Create a socket descriptior
    if ((listenfd = socket(AF_INET, SOCK_STREAM, 0)) < 0)
        printMsg("ERROR opening socket :(\n\n", "", "");

    // Eliminates "Address already in use" error from bind
    int optval = 1;
    setsockopt(listenfd, SOL_SOCKET, SO_REUSEADDR, (const void *)&optval, sizeof(int));

    // obtainig server information
    bzero((char *)&serveraddr, sizeof(serveraddr));
    serveraddr.sin_family = AF_INET;                   // we are using the Internet
    serveraddr.sin_addr.s_addr = htonl(INADDR_ANY);    // accept reqs to any IP addr
    serveraddr.sin_port = htons((unsigned short)port); // port to listen on

    // connecting the socket and the port
    if (bind(listenfd, (struct sockaddr *)&serveraddr, sizeof(serveraddr)) < 0)
        printMsg("ERROR on binding :(\n\n", "", "");

    // listening request
    if (listen(listenfd, CLIENTS) < 0)
        printMsg("ERROR on listen :(\n\n", "", "");

    initializateAll(listenfd);

    //processing request
    int clientLen = sizeof(clientaddr);
    while (1)
    {
        printMsg("Waiting for connection ...\n", "", "");
        if (poll(fds, count, TIME) > 0)
        {
            if (fds[0].revents & POLLIN)
            {
                connctfd = accept(listenfd, (struct sockaddr *)&clientaddr, &clientLen);
                if (connctfd < 0)
                {
                    printMsg("ERROR on accept\n\n", "", "");
                    continue;
                }
                printMsg("Connection established with: ", inet_ntoa(clientaddr.sin_addr), "\n");
                addClient(connctfd);
            }

            int clients;
            for (clients = 1; clients < count; ++clients)
                responseClient(clients);
        }
    }
}