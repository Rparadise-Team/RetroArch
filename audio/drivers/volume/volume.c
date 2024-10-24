#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <unistd.h>
#include <string.h>
#include <fcntl.h>
#include <dirent.h>
#include <time.h>

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

int getValueMM(char const *key) 
{
    cJSON* request_json = NULL;
    cJSON* item = NULL;
    int result = 0;

    const char *settings_file = getenv("SETTINGS_FILE");
	if (settings_file == NULL) {
		FILE* pipe = popen("dmesg | fgrep '[FSP] Flash is detected (0x1100, 0x68, 0x40, 0x18) ver1.1'", "r");
		if (!pipe) {
			FILE* configv4 = fopen("/appconfigs/system.json.old", "r");
			if (!configv4) {
				settings_file = "/appconfigs/system.json";
			} else {
				settings_file = "/mnt/SDCARD/system.json";
			}
			
			pclose(configv4);
			
		} else {
			char buffer[64];
			int flash_detected = 0;
			
			while (fgets(buffer, sizeof(buffer), pipe) != NULL) {
				if (strstr(buffer, "[FSP] Flash is detected (0x1100, 0x68, 0x40, 0x18) ver1.1") != NULL) {
					flash_detected = 1;
					break;
				}
			}
			
			pclose(pipe);
			
			if (flash_detected) {
				settings_file = "/mnt/SDCARD/system.json";
			} else {
				settings_file = "/appconfigs/system.json";
			}
		}
	}

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
    int volume = getValueMM("vol");
    int set = 0;
    set = ((volume*3)+40); //tinymix work in 100-40 // 0-(-60)
	return set;
}

int getVolumeMM()
{
  // set volumen lever save from last sesion
    int volume = getValueMM("vol");
    int set = 0;
    set = ((volume*3)-60);
	return set;
}

int setBrightnessMM()
{
  // set Brightness lever save from last sesion
    int brightness = getValueMM("brightness");
    int set = 0;
    set = (brightness*10);
	return set;
}

void set_snd_level(int target_vol) {
    int current_vol;
    time_t start_time;
    double elapsed_time;

    start_time = time(NULL);
    while (1) {
        FILE *file = fopen("/proc/mi_modules/mi_ao/mi_ao0", "w");
        if (file) {
            char command[150];
            snprintf(command, sizeof(command), "echo \"set_ao_volume 0 %d\" > /proc/mi_modules/mi_ao/mi_ao0 && echo \"set_ao_volume 1 %d\" > /proc/mi_modules/mi_ao/mi_ao0", target_vol, target_vol);
            system(command);
            fclose(file);
            break;
        }
        usleep(200000);
        elapsed_time = difftime(time(NULL), start_time);
        if (elapsed_time >= 30) {
            printf("Timed out waiting for /proc/mi_modules/mi_ao/mi_ao0\n");
            return;
        }
    }

    start_time = time(NULL);
    while (1) {
        current_vol = getVolumeMM();

        if (current_vol == target_vol) {
            printf("Volume set to %ddB\n", current_vol);
            return;
        }

        usleep(200000);
        elapsed_time = difftime(time(NULL), start_time);
        if (elapsed_time >= 30) {
            printf("Timed out trying to set volume\n");
            return;
        }
    }
}
