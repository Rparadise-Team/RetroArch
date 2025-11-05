#include <stdbool.h>

char* load_configMM(char const* path);
int getValueMM(char const *key);
int setVolumeMM(void);
int getVolumeMM(void);
int setBrightnessMM(void);
void set_snd_level(int target_vol);
bool apply_miyoomini_volume(bool audioserver_mode);
