# Note 'spock' is the name of the b-em RP2040 port
# Now we need the SDK
pico_sdk_init()


function(create_embed_file TARGET SOURCE_FILE TARGET_FILE)
    if (NOT EmbedTool_FOUND)
        set(CMAKE_MODULE_PATH ${CMAKE_MODULE_PATH} ${CMAKE_CURRENT_LIST_DIR})
        find_package(EmbedTool)
    endif()

    cmake_parse_arguments(embed "DISC;ROM" "" "" ${ARGN} )

    set(EMBED_TARGET "${TARGET}_embed")

    set(EMBED_ARGS "")
    if (embed_ROM)
        LIST(APPEND EMBED_ARGS "-r")
        STRING(APPEND EMBED_TARGET "_roms")
    else()
        LIST(APPEND EMBED_ARGS "-d")
        STRING(APPEND EMBED_TARGET "_discs")
    endif()
    if (embed_PREFIX)
        LIST(APPEND EMBED_ARGS "-p" "${embed_PREFIX}")
    endif()
    add_custom_target(${EMBED_TARGET} DEPENDS ${TARGET_FILE})

    add_custom_command(OUTPUT ${TARGET_FILE}
            DEPENDS ${SOURCE_FILE}
            COMMAND EmbedTool ${EMBED_ARGS} ${SOURCE_FILE} ${TARGET_FILE}
            )
    add_dependencies(${TARGET} ${EMBED_TARGET})

    target_sources(${TARGET} PRIVATE ${TARGET_FILE})
endfunction()

# PICO_FS_PATH set to try if you are on a recent GCC and can use FS path
function(add_spock_exe NAME GUI VER DISC_FILE ROM_FILE)
    cmake_parse_arguments(FLAGS "1080p" "NO_HW_EVENT;CMD_LINE" "" ${ARGN} )

    # todo override game file
    set(OVERRIDE_DISC_FILE ${NAME}_discs)
    if (DEFINED ${OVERRIDE_DISC_FILE})
        set(DISC_FILE ${OVERRIDE_DISC_FILE})
    endif()
    set(OVERRIDE_ROM_FILE ${NAME}_roms)
    if (DEFINED ${OVERRIDE_ROM_FILE})
        set(ROM_FILE ${OVERRIDE_ROM_FILE})
    endif()
    message("${NAME} DISCS=${DISC_FILE} ROMS=${ROM_FILE}")
    configure_b_em_exe(${NAME}
            VERSION 2.2?-${VER}
            TUBE 0
            DEBUGGER 0
            SID 0
            VDFS 0
            PICO_CPU 1
            ALLEGRO_GUI 0)
    target_compile_options(${NAME} PRIVATE -Wall)
    if (NOT FLAGS_CMD_LINE)
        target_compile_definitions(${NAME} PRIVATE
                    # whole bunch of sscanf crap
                    NO_USE_CMD_LINE
                    )
    endif()
    if (NOT FLAGS_NO_HW_EVENT)
        target_compile_definitions(${NAME} PRIVATE
                USE_HW_EVENT
        )
    endif()
    if (FLAGS_1080p)
        target_compile_definitions(${NAME} PRIVATE
                MODE_1080p
                WIDESCREEN_OPTION
                )
    else()
        target_compile_definitions(${NAME} PRIVATE
                MODE_1280
                )
    endif()

    pico_add_extra_outputs(${NAME})
    target_link_libraries(${NAME} PRIVATE spock_common ${GUI})
    if (PICO_ON_DEVICE AND NOT PICO_NO_FLASH)
        pico_set_linker_script(${NAME} ${CMAKE_CURRENT_LIST_DIR}/memmap_b-em.ld)
    endif ()
    # don't need these
    pico_set_float_implementation(${NAME} none)
    pico_set_double_implementation(${NAME} none)
    pico_enable_stdio_uart(${NAME} 1) # just use raw uart

    get_filename_component(DISC_FILE ${DISC_FILE} ABSOLUTE)
    create_embed_file(${NAME} ${DISC_FILE} ${CMAKE_CURRENT_BINARY_DIR}/${NAME}_discs.c)
    get_filename_component(ROM_FILE ${ROM_FILE} ABSOLUTE)
    create_embed_file(${NAME} ${ROM_FILE} ${CMAKE_CURRENT_BINARY_DIR}/${NAME}_roms.c ROM)
endfunction()

# Ultra simple X/EGL front end
if (X_GUI)
    add_subdirectory(x_gui)
endif()

# ---------------------------------------------------------------------------------
# beeb/master are regular scanvideo based version (either on device or using pico_host_sdl)
if (TARGET pico_scanvideo_dpi)
    add_spock_exe(beeb pico_gui pico discs/beeb_discs.txt roms/beeb_roms.txt)
    add_spock_exe(master pico_gui pico discs/master_discs.txt roms/master_roms.txt)
endif()

# ---------------------------------------------------------------------------------
# xbeeb/xmaster don't use scanvideo, they are not on device and use X/EGL
if (TARGET x_gui)
    message("PI_BUILD ${PI_BUILD}")
    set(VER $<IF:$<BOOL:${PI_BUILD}>,pi_x,x>)
    if (DRM_PRIME)
        set(VER ${VER}_drm)
    endif()
    add_spock_exe(xbeeb x_gui ${VER} discs/beeb_discs.txt roms/beeb_roms.txt CMD_LINE 1)
    add_spock_exe(xmaster x_gui ${VER} discs/master_discs.txt roms/master_roms.txt CMD_LINE 1)
endif()

if (PICO_ON_DEVICE)
    # these versions use a standard 1080p 50Hz mode, so probably work on more monitors
    add_spock_exe(beeb_1080p pico_gui pico-1080p discs/beeb_discs.txt roms/beeb_roms.txt 1080p)
    add_spock_exe(master_1080p pico_gui pico-1080p discs/master_discs.txt roms/master_roms.txt 1080p)
    # ---------------------------------------------------------------------------------
    # beeb/master are scanvideo based versions for the device (but run at 360Mhz not 270Mhz)
    add_spock_exe(beeb_360 pico_gui pico_360 discs/beeb_discs.txt roms/beeb_roms.txt)
    add_spock_exe(master_360 pico_gui pico_360 discs/master_discs.txt roms/master_roms.txt)
else()
    if (TARGET pico_scanvideo_dpi)
        add_spock_exe(beeb-no-hw-event pico_gui pico discs/beeb_discs.txt roms/beeb_roms.txt NO_HW_EVENT 1)
    elseif(NOT X_GUI)
        message(WARNING "no scanvideo available - did you forget to add pico_host_sdl?")
    endif()
endif()

add_library(pico_common INTERFACE)

# This is the scanvideo based GUI
add_library(pico_gui INTERFACE)

if (PICO_NO_HARDWARE)
    if (PI_BUILD)
        target_compile_definitions(pico_gui INTERFACE
                PICO_SCANVIDEO_SCALING_BLUR=0 # not sure if this is needed; curious why things are slow
                PICO_SCANVIDEO_SCALING_NEAREST=1
                ENABLE_FRAME_SKIP=1
                FORCE_44100_OUTPUT=1
                PI_BUILD=1)

        target_compile_definitions(x_gui INTERFACE
                ENABLE_FRAME_SKIP=1
                FORCE_44100_OUTPUT=1
                PI_BUILD=1) # not strictly PI really

        include(ProcessorCount)
        ProcessorCount(N)
        if (N STREQUAL "1")
            message("Setting frame skip of 8 for probably pi 1/zero")
            target_compile_options(pico_common INTERFACE "-mcpu=arm1176jzf-s")
            target_compile_options(pico_common INTERFACE "-mtune=arm1176jzf-s")
            target_compile_definitions(pico_common INTERFACE
                    NO_USE_SOUND
                    #DEFAULT_FRAME_SKIP_COUNT=8
                    )
        endif ()
    endif ()
    # use FS on all host builds now since we support command line
    target_compile_definitions(pico_common INTERFACE
            PICO_FS_PATH=1)
    target_link_libraries(pico_common INTERFACE $<$<AND:$<CXX_COMPILER_ID:GNU>,$<VERSION_LESS:$<CXX_COMPILER_VERSION>,9.0>>:stdc++fs>)
endif ()

target_compile_definitions(pico_common INTERFACE
        PIOC_PRINTF_SUPPORT_FLOAT=0
        PIOC_PRINTF_SUPPORT_LONG_LONG=0

        # not worth it
        NO_USE_PAL
        NO_USE_SOUND_FILTER

        NO_USE_SET_SPEED

        # maybe implement
        NO_USE_NULA_ATTRIBUTE
        NO_USE_NULA_PIXEL_HSCROLL

        # uses double atm
        NO_USE_JOYSTICK

        # unnecessary for now
        NO_USE_COMPACT
        NO_USE_TAPE

        NO_USE_WRITABLE_CONFIG
        NO_USE_CLOSE
        NO_USE_CMOS_SAVE

        #            _DEBUG

        # point directly at ROMs in flash... this seems plenty fast enough
        NO_USE_RAM_ROMS

        # the way forward
        TILE8
        MODE7_TILE8
        # don't video_poll every cycle when horizontal total = 0... someone might rely on this
        #  but mostly it is just the initial unprogramed state (waverunner does set it to 0 for 256 cycles
        #  but that's fine)
        USE_CRTC0_0_OPTIMIZATION

        # get rid of MA tracking (track address instead)
        LOOK_MA_NO_MA

        #            PICO_SCANVIDEO_ENABLE_VIDEO_CLOCK_DOWN=true
        #            PRINT_INSTRUCTIONS
        #            PRINT_EVENTS
        #            DEBUG_DISPLAYED_CHAR
        #           USE_CORE1_SOUND
        USE_DDNOISE_8
        USE_MEM_DDNOISE
        NO_USE_KEY_LOOKUP
        NO_USE_DISC_WRITE
        SHORTCIRCUIT_KEYBOARD_POLL # this may not be quite right; doesn't check current state of sysvia CA2

        CPU_ASM_INLINE_BREAKOUT

        #            DEBUG_SCANLINES
        )


# ENABLE THIS TO INCLUDE MMFS SUPPORT ON THE RP2040 (currently Master Only)
#
# This adds a version of MMFS as ROM 8, and tweaks the config to use this
# as the default filing system.
#
# Note: On the VGA Demo Board, gpio20/21 are used for the default UART,
# and also for DAT1/2 on the SD Card. However, DAT1/2 are only used
# if the SD Card is run in 4-bit mode. MMFS only uses 1-bit SPI mode.
#
# If you want to be very safe, then there are PCB links that can be cut
# that isolate DAT1/2 on the SD card.
set(INCLUDE_MMFS 1)

# >>> ENABLE THIS TO GET USB SUPPORT ON THE RP2040 - however right now this interferes with video (actually it currently causes a panic)
#set(USE_USB_KEYBOARD 1)

add_library(spock_common INTERFACE)
if (PICO_ON_DEVICE)
    if (USE_USB_KEYBOARD)
        add_subdirectory(usb_host_hid)
        target_link_libraries(spock_common INTERFACE usb_host_hid)
        target_compile_definitions(spock_common INTERFACE
                 USE_USB_KEYBOARD
                # USB_SETUP_ON_CORE1

                CFG_TUH_HUB
                CFG_TUH_HID_KEYBOARD
                USB_DPRAM_MAX=512
                PICO_USB_HOST_INTERRUPT_ENDPOINTS=3
                )
    endif ()

    if (INCLUDE_MMFS)
        target_compile_definitions(spock_common INTERFACE
                INCLUDE_MMFS
                )
    endif ()

    if ("${CMAKE_BUILD_TYPE}" STREQUAL "Release")
        target_compile_options(spock_common INTERFACE "-Ofast")
    endif()

    target_compile_definitions(spock_common INTERFACE
            USE_SECTOR_READ
            #                PICO_SCANVIDEO_SCANLINE_BUFFER_COUNT=20 # ok now needs 20... hopefully audio speedup will help there
            PICO_BUFFER_USB_ALLOC_HACK=1

            PICO_STACK_SIZE=0x400
            PICO_CORE1_STACK_SIZE=0x200 # not much going on here
            PRINTF_DISABLE_SUPPORT_FLOAT
            PRINTF_DISABLE_SUPPORT_LONG_LONG
            PICO_IE_26_29_UNCHANGED_ON_RESET
            PICO_USE_OPTIMISTIC_SBRK=1
            USE_MEM_DDNOISE
            PICO_TIME_DEFAULT_ALARM_POOL_DISABLED=1
            )
else ()
    # fixme
    target_link_libraries(pico_gui INTERFACE pico_host_timer)
endif ()

target_compile_definitions(pico_gui INTERFACE
        PICO_SCANVIDEO_ENABLE_VIDEO_CLOCK_DOWN
        PICO_AUDIO_BUFFER_SAMPLE_LENGTH=128
        PICO_AUDIO_BUFFERS_PER_CHANNEL=7
        PICO_SCANVIDEO_PLANE_COUNT=2
        PICO_SCANVIDEO_MAX_SCANLINE_BUFFER2_WORDS=17 #14 (+3 for !widescreen)
        PICO_SCANVIDEO_MISSING_SCANLINE_COLOR=0
        PICO_DEBUG_MALLOC_LOW_WATER=0x20040000
        PICO_DIVIDER_CALL_IDIV0=0
        PICO_DIVIDER_CALL_LDIV0=0

        PICO_SCANVIDEO_LINKED_SCANLINE_BUFFERS=1
)

target_compile_definitions(spock_common INTERFACE
        USE_SECTOR_READ
        #            DUMP_DISC

        USE_NON_INTERLACED_TELETEXT

        )

if (TARGET beeb)
    target_compile_definitions(beeb PRIVATE
            # firetrack has a tight timer polling loop (the one in the GUI is a bug)
            PICO_SCANVIDEO_SCANLINE_BUFFER_COUNT=24 #18 // moving sepgraph into flash gave us a little more

            USE_NON_INTERLACED_TELETEXT
            PICO_SCANVIDEO_LINKED_SCANLINE_BUFFERS=1
            PIXELATED_PAUSE=1
            )
endif()

if (TARGET beeb_1080p)
    if (NOT USE_148MHZ_FLASH_IN_1080p)
        pico_set_boot_stage2(beeb_1080p slower_boot2)
    endif()

    target_compile_definitions(beeb_1080p PRIVATE
            # firetrack has a tight timer polling loop (the one in the GUI is a bug)
            PICO_SCANVIDEO_SCANLINE_BUFFER_COUNT=24 #18 // moving sepgraph into flash gave us a little more

            USE_NON_INTERLACED_TELETEXT
            PICO_SCANVIDEO_LINKED_SCANLINE_BUFFERS=1
            PIXELATED_PAUSE=1
            )
endif()

if (PICO_ON_DEVICE)
    # Issue with SDK1.2.0
    if (NOT PICO_DEFAULT_BOOT_STAGE2_FILE)
        set(PICO_DEFAULT_BOOT_STAGE2_FILE "${PICO_SDK_PATH}/src/rp2_common/boot_stage2/compile_time_choice.S")
    endif()
    pico_define_boot_stage2(slower_boot2 ${PICO_DEFAULT_BOOT_STAGE2_FILE})
    target_compile_definitions(slower_boot2 PRIVATE PICO_FLASH_SPI_CLKDIV=4)
endif()

if (TARGET beeb_360)
    pico_set_boot_stage2(beeb_360 slower_boot2)
    target_compile_definitions(beeb_360 PRIVATE
            # firetrack has a tight timer polling loop (the one in the GUI is a bug)
            PICO_SCANVIDEO_SCANLINE_BUFFER_COUNT=20 #18 // moving sepgraph into flash gave us a little more
            RUN_AT_360
            PIXELATED_PAUSE=1
            )
endif()

if (TARGET xmaster)
    target_compile_definitions(xmaster PRIVATE
            MODEL_MASTER
    )
endif()

if (TARGET master)
    target_compile_definitions(master PRIVATE
            MODEL_MASTER
            PICO_SCANVIDEO_SCANLINE_BUFFER_COUNT=16 # faster so save some RAM
            PIXELATED_PAUSE=1
            )
endif()

if (TARGET master_1080p)
    if (NOT USE_148MHZ_FLASH_IN_1080p)
        pico_set_boot_stage2(master_1080p slower_boot2)
    endif()
    target_compile_definitions(master_1080p PRIVATE
            MODEL_MASTER
            PICO_SCANVIDEO_SCANLINE_BUFFER_COUNT=16 # faster so save some RAM
            PIXELATED_PAUSE=1
            )
endif()

if (TARGET master_360)
    pico_set_boot_stage2(master_360 slower_boot2)
    target_compile_definitions(master_360 PRIVATE
            MODEL_MASTER
            PICO_SCANVIDEO_SCANLINE_BUFFER_COUNT=14 # faster so save some RAM
            RUN_AT_360
            PIXELATED_PAUSE=1
            )
endif()

target_link_libraries(spock_common INTERFACE pico_common)

add_subdirectory(sector_read)

target_sources(spock_common INTERFACE
        ${CMAKE_CURRENT_LIST_DIR}/stub_allegro5/al_stub.cpp
        ${CMAKE_CURRENT_LIST_DIR}/audio/audio.c
        ${CMAKE_CURRENT_LIST_DIR}/video/video.c
        ${CMAKE_CURRENT_LIST_DIR}/video/display.c
        ${CMAKE_CURRENT_LIST_DIR}/video/gui_stub.c
        ${CMAKE_CURRENT_LIST_DIR}/video/menu.c
        ${CMAKE_CURRENT_LIST_DIR}/mmc/mmc.c
)

target_include_directories(spock_common INTERFACE
        ${CMAKE_CURRENT_LIST_DIR}/video)

if (PICO_ON_DEVICE OR (PI_BUILD AND PI_ASM32))
    target_sources(spock_common INTERFACE
            ${CMAKE_CURRENT_LIST_DIR}/video/pixels.S
    )
endif()

target_include_directories(spock_common INTERFACE
        ${CMAKE_CURRENT_LIST_DIR}/roms
        ${CMAKE_CURRENT_LIST_DIR}
)

#OPTION(USE_AUDIO_I2S "Use I2S Audio" OFF)
#if (USE_AUDIO_I2S)
#  target_compile_definitions(pico_6502 INTERFACE
#    USE_AUDIO_I2S
#    AUDIO_I2S_PIO1
#    AUDIO_I2S_DMA_IRQ1
#  )
#  target_link_libraries(pico_6502 INTERFACE audio_i2s)
#else()
#  target_compile_definitions(pico_6502 INTERFACE
#    USE_PWM_AUDIO
#    PWM_AUDIO_PIO1
#    PIO_PWM_INTERP_SAVE
#  )
# # target_link_libraries(pico_6502 INTERFACE pio_pwm_audio)
#endif()

target_compile_definitions(pico_gui INTERFACE
    BLOCKING_GIVE_SYNCHRONIZE_BUFFERS
    PICO_SCANVIDEO_MAX_SCANLINE_BUFFER_WORDS=324
    #NDEBUG
    PICO_AUDIO_I2S_MONO_INPUT=1
    PICO_AUDIO_I2S_MONO_OUTPUT=1
    PICO_AUDIO_I2S_DMA_IRQ=1
    PICO_AUDIO_I2S_PIO=1
)

target_include_directories(pico_gui INTERFACE ${CMAKE_CURRENT_LIST_DIR})

if (PICO_NO_HARDWARE)
    target_sources(pico_gui INTERFACE
            ${CMAKE_CURRENT_LIST_DIR}/linux/keycode_to_allegro.cpp)
    target_include_directories(pico_gui INTERFACE
        ${CMAKE_CURRENT_LIST_DIR}/linux)
    if (TARGET x_gui)
        target_sources(x_gui INTERFACE
                ${CMAKE_CURRENT_LIST_DIR}/linux/keycode_to_allegro.cpp)
        target_include_directories(x_gui INTERFACE
                ${CMAKE_CURRENT_LIST_DIR}/linux)
    endif()
else()
    pico_generate_pio_header(pico_gui ${CMAKE_CURRENT_LIST_DIR}/video/menu.pio)
endif()

target_link_libraries(pico_gui INTERFACE
        pico_scanvideo_dpi
        pico_audio_i2s
        pico_stdlib
        pico_multicore
        sector_read)

if (TARGET debug_malloc)
    target_link_libraries(pico_gui INTERFACE debug_malloc)
endif()

if (PICO_NO_HARDWARE)
    add_subdirectory(gen_tables)
endif()
