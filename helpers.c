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

int pmasks[10] = {S_IFMT, S_IRUSR, S_IWUSR, S_IXUSR, S_IRGRP, S_IWGRP, S_IXGRP, S_IROTH, S_IWOTH, S_IXOTH};

void printMsg(char *msg, char *text, char *end)
{
    printf("[JCH-SERVER]: %s%s%s", msg, text, end);
}

//quit the final '/' of the directory
void fixDir(char *d)
{
    int n = strlen(d);
    if (n && d[--n] == '/')
        d[n] = '\0';
}

void writeHTML(int fd, char *buf, int size)
{
    int r = size;
    while (1)
    {
        int n = write(fd, buf, r);
        r -= n;
        if (n <= 0 || r <= 0)
            break;
    }
}

//read sections of the html page
void readHtml(char *path, off_t of, long long bytes, char *answer)
{
    int fd = open(path, O_RDONLY);
    lseek(fd, of, SEEK_SET);
    int b = read(fd, answer, bytes);
    close(fd);
}

//Check all type of permissions
char *ObtainPermissions(mode_t st_mode)
{
    char *per = calloc(11 * sizeof(char), sizeof(char));
    sprintf(per, "mrwxrwxrwx");

    switch (st_mode & pmasks[0])
    {
    case S_IFBLK:
        per[0] = 'b';
        break;
    case S_IFCHR:
        per[0] = 'c';
        break;
    case S_IFDIR:
        per[0] = 'd';
        break;
    case S_IFIFO:
        per[0] = 'p';
        break;
    case S_IFLNK:
        per[0] = 'l';
        break;
    case S_IFREG:
        per[0] = '-';
        break;
    case S_IFSOCK:
        per[0] = 's';
        break;
    default:
        per[0] = 'u';
        break;
    }

    int i;
    for (i = 1; i < 10; ++i)
        if (!(st_mode & pmasks[i]))
            per[i] = '-';

    return per;
}

//Obtain the extension of the file
char *ObtainExtension(char *dname, mode_t st_mode)
{
    if (st_mode & S_IFDIR)
    {
        return "dir\0";
    }
    printf("an explosion\n");
    char *ext = rindex(dname + 1, '.');
    if (ext != NULL)
    {
        return ext + 1;
    }

    return "---\0";
}

//Calculate the size in a format "00.00XB"
char *ObtainSize(off_t st_size, mode_t st_mode)
{
    if (st_mode & S_IFDIR)
    {
        return "---\0";
    }
    char *size = calloc(64 * sizeof(char), sizeof(char));

    double val;
    int i;
    val = (double)st_size;
    char *tbyte[4] = {"B", "KB", "MB", "GB"};
    for (i = 0; val >= 1024 && i < 4; ++i)
        val /= 1024;
    sprintf(size, "%.2f%s", val, strdup(tbyte[i]));

    return size;
}

//Order the folder list
void Sort(struct stat *dir, char **dirName, int len, int sortData, char *field, char *order)
{
    struct stat temp;
    char *name;
    int index, val, i, j;
    for (i = 0; i < len; ++i)
    {
        index = i;
        for (j = i + 1; j < len; ++j)
        {
            if (sortData == 0 || strcmp(field, "name") == 0)
                val = strcasecmp(dirName[index], dirName[j]);
            else if (strcmp(field, "size") == 0)
                val = (dir[index].st_size > dir[j].st_size) - (dir[index].st_size < dir[j].st_size);
            else
                val = (dir[index].st_ctime > dir[j].st_ctime) - (dir[index].st_ctime < dir[j].st_ctime);
            if (sortData == 0 || strcmp(order, "dsc") == 0)
                index = (val <= 0 ? index : j);
            else
                index = (val >= 0 ? index : j);
        }
        temp = dir[index];
        name = dirName[index];
        dir[index] = dir[i];
        dirName[index] = dirName[i];
        dir[i] = temp;
        dirName[i] = name;
    }
}

//Add files to the folder list
void AddToHTML(struct stat *dir, char **dirName, int len, char *html, int rootlen, int n)
{
    char *ext, *per, *size, *dat, eof = '\0';
    int i;
    for (i = 0; i < len; ++i)
    {
        ext = ObtainExtension(dirName[i], dir[i].st_mode);
        per = ObtainPermissions(dir[i].st_mode);
        size = ObtainSize(dir[i].st_size, dir[i].st_mode);
        dat = ctime(&dir[i].st_mtime);

        sprintf(html + strlen(html), "<tr class=\"file\"><td><a href=\"%s\"><svg width=\"1.5em\" height=\"1em\" version=\"1.1\" viewBox=\"0 0 317 259\">%c", dirName[i] + rootlen, eof);
        if (per[0] == 'd')
        {
            sprintf(html + strlen(html), "<use xlink:href=\"#folder\"></use>%c", eof);
        }
        else if (per[0] == '-')
        {
            sprintf(html + strlen(html), "<use xlink:href=\"#file\"></use>%c", eof);
        }
        sprintf(html + strlen(html), "</svg><span class=\"name\">%s</span></a></td><td>%s</td><td class=\"hideable\">%s</td></tr>\n%c", dirName[i] + n + 1, size, dat, eof);

        sprintf(dirName[i] + n, "%c", eof);
    }
}
