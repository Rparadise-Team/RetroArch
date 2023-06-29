This source is the diff from this version: https://github.com/libretro/RetroArch/releases/tag/v1.15.0

Toolchain Directory is /opt/miyoomini , specified in Makefile , Makefile_ml , gfx/video_filters/make , change accordingly to your environment
MI libraries and headres are in SYSROOT/usr/lib , SYSROOT/usr/include (previously used /sdkdir has been removed)

Makefile is based on Makefile.miyoo/dingux
dingux/dingux_utils.c is based on https://github.com/TechDevangelist/RetroArch

Modification Appendix:
gfx/video_filter.c : Slightly modified to recognize not only built-in filters, but also filters added to .retroarch/filters/video
libretro-common/audio/conversion/float_to_s16.c : Fixed a compile error when HAVE_ARM_NEON_ASM_OPTIMIZATIONS is specified
retroarch.c : Modified Hotkey: Fullscreen (Toggle) to be a Scaling Option change
