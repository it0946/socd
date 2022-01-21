#include <stdio.h>
#include <stdlib.h>
#include <dirent.h>
#include <string.h>

const char *BY_ID = "/dev/input/by-id/";
const char *BY_PATH = "/dev/input/by-path/";

char *get_keyboard() {
    DIR *d;
    struct dirent *dir;

    char *abs_device_path = malloc(sizeof(char) * 275);

    if ((d = opendir(BY_ID)) != NULL)
        strcpy(abs_device_path, BY_ID);
    else if ((d = opendir(BY_PATH)) != NULL)
        strcpy(abs_device_path, BY_PATH);
    else 
        return NULL;

    char *possible_devices[42];
    int j = -1;

    while ((dir = readdir(d)) != NULL) {
        char *tmp = dir->d_name;
        int len = strlen(tmp);
        if (len < 10) continue;

        if (strncmp(tmp + len - 10, "-event-kbd", 10) != 0
            || strncmp(tmp + len - 15, "-if", 3) == 0) continue;

        j += 1;
        possible_devices[j] = tmp;
    }

    if (j == -1) {
        free(abs_device_path);
        return NULL;
    }

    strcat(abs_device_path, possible_devices[0]);

    closedir(d);

    return abs_device_path;
}

int main(int argc, char **argv) {
    char *device_path = get_keyboard();
    if (!device_path) {
        printf("Failed to get keyboards\n");
        return 1;
    }

    printf("keyboard path: %s\n", device_path);

    free(device_path);

    return 0;
}