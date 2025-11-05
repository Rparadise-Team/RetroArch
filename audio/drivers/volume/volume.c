#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <unistd.h>
#include <string.h>
#include <fcntl.h>
#include <dirent.h>
#include <time.h>
#include <stdbool.h>
#include <limits.h>

#include <sys/types.h>
#include <sys/stat.h>
#include <sys/ioctl.h>

#include "cJSON.h"
#include "volume.h"
#include "../../verbosity.h"

char* load_configMM(char const* path)
{
    char *buffer   = NULL;
    size_t length  = 0;
    FILE *f        = fopen(path, "rb");

    if (!f)
        return NULL;

    if (fseek(f, 0, SEEK_END) == 0)
    {
        long file_length = ftell(f);

        if (file_length > 0)
            length = (size_t)file_length;

        rewind(f);
    }

    buffer = (char*)calloc(length + 1, sizeof(char));
    if (!buffer)
    {
        fclose(f);
        return NULL;
    }

    if (length > 0)
        fread(buffer, sizeof(char), length, f);

    fclose(f);

    buffer[length] = '\0';
    return buffer;
}

static const char *miyoomini_get_settings_path(void)
{
    const char *env_path = getenv("SETTINGS_FILE");

    if (env_path && *env_path)
        return env_path;

    static bool path_cached = false;
    static char cached_path[PATH_MAX];

    if (!path_cached)
    {
        const char *default_path = "/appconfigs/system.json";
        const char *sd_path      = "/mnt/SDCARD/system.json";
        const char *chosen_path  = default_path;
        bool prefer_sdcard       = false;

        FILE *configv4 = fopen("/appconfigs/system.json.old", "r");
        if (configv4)
        {
            prefer_sdcard = true;
            fclose(configv4);
        }
        else
        {
            FILE* pipe = popen("dmesg | fgrep '[FSP] Flash is detected (0x1100, 0x68, 0x40, 0x18) ver1.1'", "r");
            if (pipe)
            {
                char buffer[64];

                while (fgets(buffer, sizeof(buffer), pipe) != NULL)
                {
                    if (strstr(buffer, "[FSP] Flash is detected (0x1100, 0x68, 0x40, 0x18) ver1.1") != NULL)
                    {
                        prefer_sdcard = true;
                        break;
                    }
                }

                pclose(pipe);
            }
        }

        chosen_path = prefer_sdcard ? sd_path : default_path;
        snprintf(cached_path, sizeof(cached_path), "%s", chosen_path);
        path_cached = true;
    }

    return path_cached ? cached_path : NULL;
}

int getValueMM(char const *key)
{
    cJSON* request_json = NULL;
    cJSON* item = NULL;
    int result = 0;
    const char *settings_file = miyoomini_get_settings_path();

    if (!settings_file)
        return result;

    char *request_body = load_configMM(settings_file);
    if (!request_body)
        return result;

    request_json = cJSON_Parse(request_body);
    if (request_json)
    {
        item = cJSON_GetObjectItem(request_json, key);
        if (cJSON_IsNumber(item))
            result = (int)item->valuedouble;
        cJSON_Delete(request_json);
    }

    free(request_body);
    return result;
}

int setVolumeMM(void)
{
  // set volumen lever save from last sesion
    int volume = getValueMM("vol");
    int set = 0;
    set = ((volume*3)+40); //tinymix work in 100-40 // 0-(-60)
	return set;
}

int getVolumeMM(void)
{
  // set volumen lever save from last sesion
    int volume = getValueMM("vol");
    int set = 0;
    set = ((volume*3)-60);
	return set;
}

int setBrightnessMM(void)
{
  // set Brightness lever save from last sesion
    int brightness = getValueMM("brightness");
    int set = 0;
    set = (brightness*10);
	return set;
}

void set_snd_level(int target_vol) {
    int retry_count = 0;
    int max_retries = 50;  /* 5 segundos máximo */

    /* Try to write volume to /proc, with retries if not ready */
    while (retry_count < max_retries) {
        FILE *file = fopen("/proc/mi_modules/mi_ao/mi_ao0", "w");
        if (file) {
            char command[150];
            snprintf(command, sizeof(command), 
                "echo \"set_ao_volume 0 %d\" > /proc/mi_modules/mi_ao/mi_ao0 && "
                "echo \"set_ao_volume 1 %d\" > /proc/mi_modules/mi_ao/mi_ao0", 
                target_vol, target_vol);
            system(command);
            fclose(file);
            printf("Volume set to %ddB\n", target_vol);
            return;  /* Done! No verification needed */
        }
        usleep(100000);  /* 100ms retry */
        retry_count++;
    }

    printf("Timed out waiting for /proc/mi_modules/mi_ao/mi_ao0\n");
}

bool apply_miyoomini_volume(bool audioserver_mode)
{
    bool success     = true;
    int  target_vol  = getVolumeMM();

    if (!audioserver_mode)
    {
        int  volumeMM = setVolumeMM();
        char command[64];
        int  written  = snprintf(command, sizeof(command),
              "tinymix set 6 %d", volumeMM);

        if (written < 0 || written >= (int)sizeof(command))
        {
            RARCH_ERR("[MiyooMini]: Failed to compose tinymix command for volume %d.\n",
                  volumeMM);
            success = false;
        }
        else
        {
            int ret = system(command);
            if (ret != 0)
            {
                RARCH_WARN("[MiyooMini]: tinymix command returned %d.\n", ret);
                success = false;
            }
        }
    }

    set_snd_level(target_vol);
    return success;
}
