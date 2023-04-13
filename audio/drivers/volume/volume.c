#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <unistd.h>
#include <string.h>
#include <fcntl.h>
#include <dirent.h>

#include <sys/types.h>
#include <sys/stat.h>
#include <sys/ioctl.h>

#include "cJSON.h"
#include "volume.h"

char* load_configMM(char const* path) {
    char* buffer = 0;
    long length = 0;

    FILE * f = fopen(path, "rb"); //was "rb"
    if (f) {
        fseek(f, 0, SEEK_END);
        length = ftell(f);
        fseek(f, 0, SEEK_SET);
        buffer = (char*) malloc((length+1)*sizeof(char));
        if (buffer) {
            fread(buffer, sizeof(char), length, f);
        }
        fclose(f);
    }
    buffer[length] = '\0';

    return buffer;
}

int getVolumeMM(char const *key) 
{
    cJSON* request_json = NULL;
    cJSON* item = NULL;
    int result = 0;

    const char *settings_file = "/appconfigs/system.json";

    char *request_body = load_configMM(settings_file);
    request_json = cJSON_Parse(request_body);
    item = cJSON_GetObjectItem(request_json, key);
    result = cJSON_GetNumberValue(item);
    free(request_body);
    return result;
}

int setVolumeMM()
{
  // set volumen lever save from last sesion
    uint32_t fa = open("/dev/mi_ao", O_RDWR);
    int volume = getVolumeMM("vol");
    int set = 0;
    set = ((volume*3)+40); //tinymix work in 100-40 // 0-(-60)
	return set;
}