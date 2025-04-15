
#include <stdio.h>
#include <string.h>
#include <syslog.h>
#include <errno.h>

int main(int argc, char *argv[]) {

    openlog(NULL, 0, LOG_USER);

    if (argc != 3) {
        syslog(LOG_ERR, "ERROR: Invalid number of arguments. Expected 2 arguments, but got %d.", argc - 1);
        printf("ERROR: Invalid number of arguments. Expected 2 arguments, but got %d.\n", argc - 1);
        printf("Usage: writer <writefile> <writestr>\n");
        printf("<writefile> is the file to write to.\n");
        printf("<writestr> is the string to write to the file.\n");
        printf("Example: ./writer /tmp/writer.txt \"Hello World!\"\n");
        return 1;
    }

    const char *writefile = argv[1];
    const char *writestr = argv[2];

    FILE* fd = fopen(writefile, "w");

    if (fd == NULL) {
        syslog(LOG_ERR, "ERROR: Failed to open file %s: %s", writefile, strerror(errno));
        return 1;
    }

    syslog(LOG_DEBUG, "INFO: Writing %s to %s", writestr, writefile);
    if (fputs(writestr, fd) == EOF) {
        syslog(LOG_ERR, "ERROR: Failed to write to file %s: %s", writefile, strerror(errno));
        fclose(fd);
        return 1;
    }

    fclose(fd);
    closelog();

    return 0;
}
