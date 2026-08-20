# Official DOOM (doomgeneric) as an ESP-IDF component for the HighBoy P4.
# All engine sources + doomgeneric.c + the HighBoy platform layer compile here.
file(GLOB DOOM_SRCS "*.c")

idf_component_register(
    SRCS ${DOOM_SRCS}
    INCLUDE_DIRS "include"        # public: only doom_highboy.h (launcher entry)
    PRIV_INCLUDE_DIRS "."         # private: doomgeneric internal headers (config.h, etc.)
    REQUIRES Drivers Service esp_lcd esp_timer esp_system driver
    LDFRAGMENTS "linker.lf"       # park DOOM's ~300 KB static .bss in PSRAM
)

# Render at DOOM's native internal resolution (no upscale). ALL sources must
# agree, so define it component-wide.
target_compile_definitions(${COMPONENT_LIB} PRIVATE
    DOOMGENERIC_RESX=320
    DOOMGENERIC_RESY=200
    FEATURE_SOUND=1        # enable SFX (DG_sound_module in doom_sound_highboy.c)
)

# Legacy id-Software C: silence its many warnings and optimize for framerate
# (the project default is -Og; DOOM needs -O2 to be playable).
target_compile_options(${COMPONENT_LIB} PRIVATE -O2 -w)
