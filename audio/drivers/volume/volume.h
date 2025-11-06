#define MI_AO_SETVOLUME	0x4008690b
#define MI_AO_GETVOLUME	0xc008690c
#define MI_AO_SETMUTE	0x4008690d

char* load_configMM(char const* path);
int getValueMM(char const *key);
int getVolumeMM();
int setBrightnessMM();
void set_snd_level(int target_vol);
