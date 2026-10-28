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

#define MI_AO_SETVOLUME	0x4008690b
#define MI_AO_GETVOLUME	0xc008690c
#define MI_AO_SETMUTE	0x4008690d

char* load_configMM(char const* path) {
    char* buffer = NULL;
    long length = 0;

    FILE * f = fopen(path, "rb");
    if (!f)
        return NULL;
    fseek(f, 0, SEEK_END);
    length = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (length >= 0)
        buffer = (char*) malloc(length + 1);
    if (buffer) {
        length = (long)fread(buffer, 1, length, f);
        buffer[length] = '\0';
    }
    fclose(f);

    return buffer;
}

/* Which system.json this console uses never changes while running, and
 * finding it out spawns a shell, so only do it once. */
static const char *settings_file_path(void)
{
    static const char *settings_file = NULL;

    if (settings_file)
        return settings_file;

    settings_file = getenv("SETTINGS_FILE");
    if (settings_file == NULL) {
        FILE* pipe = popen("dmesg | fgrep '[FSP] Flash is detected (0x1100, 0x68, 0x40, 0x18) ver1.1'", "r");
        if (!pipe) {
            FILE* configv4 = fopen("/appconfigs/system.json.old", "r");
            if (!configv4) {
                settings_file = "/appconfigs/system.json";
            } else {
                settings_file = "/mnt/SDCARD/system.json";
                fclose(configv4);
            }
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

    return settings_file;
}

int getValueMM(char const *key) 
{
    cJSON* request_json = NULL;
    cJSON* item = NULL;
    int result = 0;
    char *request_body = load_configMM(settings_file_path());

    if (!request_body)
        return 0;
    request_json = cJSON_Parse(request_body);
    item = cJSON_GetObjectItem(request_json, key);
    if (cJSON_IsNumber(item))
        result = (int)cJSON_GetNumberValue(item);
    cJSON_Delete(request_json);
    free(request_body);
    return result;
}

int getVolumeMM()
{
  // set volumen lever save from last sesion
    int volume = getValueMM("vol");
	return volume;
}

int setBrightnessMM()
{
  // set Brightness lever save from last sesion
    int brightness = getValueMM("brightness");
    int set = 0;
	if (brightness == 0) {
#ifdef NO_MMP
#ifdef MMIYOOV4
    	set = (brightness+3);
#else
    	set = (brightness+6);
#endif
#else
    	set = (brightness+3);
#endif
	} else {
		set = (brightness*10);
	}
	return set;
}

void set_snd_level(int target_vol) {
    int current_vol;
    int fd = open("/dev/mi_ao", O_RDWR);
    int vol = getValueMM("vol");
    int mute = getValueMM("mute");
    int audiofix = getValueMM("audiofix");

    if (mute == 0) {
        if (audiofix == 1 || audiofix == 0) {
            if (fd >= 0) {
                int buf2[] = {0, 0};
                uint64_t buf1[] = {sizeof(buf2), (uintptr_t)buf2};
                ioctl(fd, MI_AO_GETVOLUME, buf1);
                current_vol = ((vol * 3) - 60);
                buf2[1] = current_vol;
                ioctl(fd, MI_AO_SETVOLUME, buf1);
            }
        }

        if (audiofix == 0) {
            char command[100];
            int tiny;
            tiny = (vol * 3) + 40;
            snprintf(command, sizeof(command), "tinymix set 6 %d", tiny);
            system(command);
        }

        if (vol > 0) {
            if (fd >= 0) {
                int buf2[] = {0, 0};
                uint64_t buf1[] = {sizeof(buf2), (uintptr_t)buf2};
                ioctl(fd, MI_AO_SETMUTE, buf1);
            }
        }
    } else if (mute == 1) {
        if (fd >= 0) {
            int buf2[] = {0, 1};
            uint64_t buf1[] = {sizeof(buf2), (uintptr_t)buf2};
            ioctl(fd, MI_AO_SETMUTE, buf1);
        }
    }

    if (fd >= 0)
        close(fd);

    current_vol = getVolumeMM();

    if (current_vol == target_vol) {
        printf("Volume set to %ddB\n", current_vol);
    }
}
