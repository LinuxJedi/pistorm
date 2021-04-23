// SPDX-License-Identifier: MIT

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "pistorm-dev.h"
#include "pistorm-dev-enums.h"
#include "platforms/platforms.h"
#include "gpio/ps_protocol.h"
#include "platforms/amiga/rtg/rtg.h"
#include "platforms/amiga/piscsi/piscsi.h"
#include "platforms/amiga/net/pi-net.h"

#define DEBUG_PISTORM_DEVICE

#ifdef DEBUG_PISTORM_DEVICE
#define DEBUG printf

#define PIDEV_SWREV 0x0105

static const char *op_type_names[4] = {
    "BYTE",
    "WORD",
    "LONGWORD",
    "MEM",
};
#else
#define DEBUG(...)
#endif

extern uint32_t pistorm_dev_base;
extern uint32_t do_reset;

extern void adjust_ranges_amiga(struct emulator_config *cfg);
extern uint8_t rtg_enabled, rtg_on, pinet_enabled, piscsi_enabled, load_new_config;
extern struct emulator_config *cfg;

char cfg_filename[256] = "default.cfg";
char tmp_string[256];

static uint8_t pi_byte[8];
static uint16_t pi_word[4];
static uint32_t pi_longword[4];
static uint32_t pi_string[4];

static uint32_t pi_dbg_val[4];
static uint32_t pi_dbg_string[4];

static uint32_t pi_cmd_result = 0;

int32_t grab_amiga_string(uint32_t addr, uint8_t *dest, uint32_t str_max_len) {
    int32_t r = get_mapped_item_by_address(cfg, addr);
    uint32_t index = 0;

    if (r == -1) {
        DEBUG("[GRAB_AMIGA_STRING] No mapped range found for address $%.8X. Grabbing string data over the bus.\n", addr);
        do {
            dest[index] = read8(addr + index);
            index++;
        } while (dest[index - 1] != 0x00 && index < str_max_len);
    }
    else {
        uint8_t *src = cfg->map_data[r] + (addr - cfg->map_offset[r]);
        do {
            dest[index] = src[index];
            index++;
        } while (dest[index - 1] != 0x00 && index < str_max_len);
    }
    if (index == str_max_len) {
        memset(dest, 0x00, str_max_len + 1);
        return -1;
    }
    DEBUG("[GRAB_AMIGA_STRING] Grabbed string: %s\n", dest);
    return (int32_t)strlen((const char*)dest);
}

char *get_pistorm_devcfg_filename() {
    return cfg_filename;
}

void set_pistorm_devcfg_filename(char *filename) {
    strcpy(cfg_filename, filename);
}

void handle_pistorm_dev_write(uint32_t addr_, uint32_t val, uint8_t type) {
    uint32_t addr = (addr_ & 0xFFFF);

    switch((addr)) {
        case PI_DBG_MSG:
            // Output debug message based on value written and data in val/str registers.
            break;
        case PI_DBG_VAL1: case PI_DBG_VAL2: case PI_DBG_VAL3: case PI_DBG_VAL4:
        case PI_DBG_VAL5: case PI_DBG_VAL6: case PI_DBG_VAL7: case PI_DBG_VAL8:
            DEBUG("[PISTORM-DEV] Set DEBUG VALUE %d to %d ($%.8X)\n", (addr - PI_DBG_VAL1) / 4, val, val);
            pi_dbg_val[(addr - PI_DBG_VAL1) / 4] = val;
            break;
        case PI_DBG_STR1: case PI_DBG_STR2: case PI_DBG_STR3: case PI_DBG_STR4:
            DEBUG("[PISTORM-DEV] Set DEBUG STRING POINTER %d to $%.8X\n", (addr - PI_DBG_STR1) / 4, val);
            pi_dbg_string[(addr - PI_DBG_STR1) / 4] = val;
            break;

        case PI_BYTE1: case PI_BYTE2: case PI_BYTE3: case PI_BYTE4:
        case PI_BYTE5: case PI_BYTE6: case PI_BYTE7: case PI_BYTE8:
            DEBUG("[PISTORM-DEV] Set BYTE %d to %d ($%.2X)\n", addr - PI_BYTE1, (val & 0xFF), (val & 0xFF));
            pi_byte[addr - PI_BYTE1] = (val & 0xFF);
            break;
        case PI_WORD1: case PI_WORD2: case PI_WORD3: case PI_WORD4:
            DEBUG("[PISTORM-DEV] Set WORD %d to %d ($%.4X)\n", (addr - PI_WORD1) / 2, (val & 0xFFFF), (val & 0xFFFF));
            pi_word[(addr - PI_WORD1) / 2] = (val & 0xFFFF);
            break;
        case PI_LONGWORD1: case PI_LONGWORD2: case PI_LONGWORD3: case PI_LONGWORD4:
            DEBUG("[PISTORM-DEV] Set LONGWORD %d to %d ($%.8X)\n", (addr - PI_LONGWORD1) / 4, val, val);
            pi_longword[(addr - PI_LONGWORD1) / 4] = val;
            break;
        case PI_STR1: case PI_STR2: case PI_STR3: case PI_STR4:
            DEBUG("[PISTORM-DEV] Set STRING POINTER %d to $%.8X\n", (addr - PI_STR1) / 4, val);
            pi_string[(addr - PI_STR1) / 4] = val;
            break;

        case PI_CMD_RTGSTATUS:
            DEBUG("[PISTORM-DEV] Write to RTGSTATUS: %d\n", val);
            if (val == 1 && !rtg_enabled) {
                init_rtg_data();
                rtg_enabled = 1;
                pi_cmd_result = PI_RES_OK;
            } else if (val == 0 && rtg_enabled) {
                if (!rtg_on) {
                    shutdown_rtg();
                    rtg_enabled = 0;
                    pi_cmd_result = PI_RES_OK;
                } else {
                    // Refuse to disable RTG if it's currently in use.
                    pi_cmd_result = PI_RES_FAILED;
                }
            } else {
                pi_cmd_result = PI_RES_NOCHANGE;
            }
            adjust_ranges_amiga(cfg);
            break;
        case PI_CMD_NETSTATUS:
            DEBUG("[PISTORM-DEV] Write to NETSTATUS: %d\n", val);
            if (val == 1 && !pinet_enabled) {
                pinet_init(NULL);
                pinet_enabled = 1;
                pi_cmd_result = PI_RES_OK;
            } else if (val == 0 && pinet_enabled) {
                pinet_shutdown();
                pinet_enabled = 0;
                pi_cmd_result = PI_RES_OK;
            } else {
                pi_cmd_result = PI_RES_NOCHANGE;
            }
            adjust_ranges_amiga(cfg);
            break;
        case PI_CMD_PISCSI_CTRL:
            DEBUG("[PISTORM-DEV] Write to PISCSI_CTRL: ");
            switch(val) {
                case PISCSI_CTRL_DISABLE:
                    DEBUG("DISABLE\n");
                    if (piscsi_enabled) {
                        piscsi_shutdown();
                        piscsi_enabled = 0;
                        // Probably not OK... depends on if you booted from floppy, I guess.
                        pi_cmd_result = PI_RES_OK;
                    } else {
                        pi_cmd_result = PI_RES_NOCHANGE;
                    }
                    break;
                case PISCSI_CTRL_ENABLE:
                    DEBUG("ENABLE\n");
                    if (!piscsi_enabled) {
                        piscsi_init();
                        piscsi_enabled = 1;
                        piscsi_refresh_drives();
                        pi_cmd_result = PI_RES_OK;
                    } else {
                        pi_cmd_result = PI_RES_NOCHANGE;
                    }
                    break;
                case PISCSI_CTRL_MAP:
                    DEBUG("MAP\n");
                    if (pi_string[0] == 0 || grab_amiga_string(pi_string[0], (uint8_t *)tmp_string, 255) == -1)  {
                        printf("[PISTORM-DEV] Failed to grab string for PISCSI drive filename. Aborting.\n");
                        pi_cmd_result = PI_RES_FAILED;
                    } else {
                        FILE *tmp = fopen(tmp_string, "rb");
                        if (tmp == NULL) {
                            printf("[PISTORM-DEV] Failed to open file %s for PISCSI drive mapping. Aborting.\n", cfg_filename);
                            pi_cmd_result = PI_RES_FILENOTFOUND;
                        } else {
                            fclose(tmp);
                            printf("[PISTORM-DEV] Attempting to map file %s as PISCSI drive %d...\n", cfg_filename, pi_word[0]);
                            piscsi_unmap_drive(pi_word[0]);
                            piscsi_map_drive(tmp_string, pi_word[0]);
                            pi_cmd_result = PI_RES_OK;
                        }
                    }
                    pi_string[0] = 0;
                    break;
                case PISCSI_CTRL_UNMAP:
                    DEBUG("UNMAP\n");
                    if (pi_word[0] > 7) {
                        printf("[PISTORM-DEV] Invalid drive ID %d for PISCSI unmap command.", pi_word[0]);
                        pi_cmd_result = PI_RES_INVALIDVALUE;
                    } else {
                        if (piscsi_get_dev(pi_word[0])->fd != -1) {
                            piscsi_unmap_drive(pi_word[0]);
                            pi_cmd_result = PI_RES_OK;
                        } else {
                            pi_cmd_result = PI_RES_NOCHANGE;
                        }
                    }
                    break;
                case PISCSI_CTRL_EJECT:
                    DEBUG("EJECT (NYI)\n");
                    pi_cmd_result = PI_RES_NOCHANGE;
                    break;
                case PISCSI_CTRL_INSERT:
                    DEBUG("INSERT (NYI)\n");
                    pi_cmd_result = PI_RES_NOCHANGE;
                    break;
                default:
                    DEBUG("UNKNOWN/UNHANDLED. Aborting.\n");
                    pi_cmd_result = PI_RES_INVALIDVALUE;
                    break;
            }
            adjust_ranges_amiga(cfg);
            break;
        
        case PI_CMD_KICKROM:
            DEBUG("[PISTORM-DEV] Write to KICKROM.\n");
            if (pi_string[0] == 0 || grab_amiga_string(pi_string[0], (uint8_t *)tmp_string, 255) == -1)  {
                printf("[PISTORM-DEV] Failed to grab string KICKROM filename. Aborting.\n");
                pi_cmd_result = PI_RES_FAILED;
            } else {
                FILE *tmp = fopen(tmp_string, "rb");
                if (tmp == NULL) {
                    printf("[PISTORM-DEV] Failed to open file %s for KICKROM mapping. Aborting.\n", cfg_filename);
                    pi_cmd_result = PI_RES_FILENOTFOUND;
                } else {
                    fclose(tmp);
                    if (get_named_mapped_item(cfg, "kickstart") != -1) {
                        uint32_t index = get_named_mapped_item(cfg, "kickstart");
                        free(cfg->map_data[index]);
                        free(cfg->map_id[index]);
                        cfg->map_type[index] = MAPTYPE_NONE;
                        // Dirty hack, I am sleepy and lazy.
                        add_mapping(cfg, MAPTYPE_ROM, cfg->map_offset[index], cfg->map_size[index], 0, tmp_string, "kickstart");
                        pi_cmd_result = PI_RES_OK;
                        do_reset = 1;
                    } else {
                        printf ("[PISTORM-DEV] Could not find mapped range 'kickstart', cannot remap KICKROM.\n");
                        pi_cmd_result = PI_RES_FAILED;
                    }
                }
            }
            adjust_ranges_amiga(cfg);
            pi_string[0] = 0;
            break;
        case PI_CMD_EXTROM:
            DEBUG("[PISTORM-DEV] Write to EXTROM.\n");
            if (pi_string[0] == 0 || grab_amiga_string(pi_string[0], (uint8_t *)tmp_string, 255) == -1)  {
                printf("[PISTORM-DEV] Failed to grab string EXTROM filename. Aborting.\n");
                pi_cmd_result = PI_RES_FAILED;
            } else {
                FILE *tmp = fopen(tmp_string, "rb");
                if (tmp == NULL) {
                    printf("[PISTORM-DEV] Failed to open file %s for EXTROM mapping. Aborting.\n", cfg_filename);
                    pi_cmd_result = PI_RES_FILENOTFOUND;
                } else {
                    fclose(tmp);
                    if (get_named_mapped_item(cfg, "extended") != -1) {
                        uint32_t index = get_named_mapped_item(cfg, "extended");
                        free(cfg->map_data[index]);
                        free(cfg->map_id[index]);
                        cfg->map_type[index] = MAPTYPE_NONE;
                        // Dirty hack, I am tired and lazy.
                        add_mapping(cfg, MAPTYPE_ROM, cfg->map_offset[index], cfg->map_size[index], 0, tmp_string, "extended");
                        pi_cmd_result = PI_RES_OK;
                        do_reset = 1;
                    } else {
                        printf ("[PISTORM-DEV] Could not find mapped range 'extrom', cannot remap EXTROM.\n");
                        pi_cmd_result = PI_RES_FAILED;
                    }
                }
            }
            adjust_ranges_amiga(cfg);
            pi_string[0] = 0;
            break;

        case PI_CMD_RESET:
            DEBUG("[PISTORM-DEV] System reset called, code %d\n", (val & 0xFFFF));
            do_reset = 1;
            break;
        case PI_CMD_SWITCHCONFIG:
            DEBUG("[PISTORM-DEV] Config switch called, command: ");
            switch (val) {
                case PICFG_LOAD:
                    DEBUG("LOAD\n");
                    if (pi_string[0] == 0 || grab_amiga_string(pi_string[0], (uint8_t *)cfg_filename, 255) == -1) {
                        printf("[PISTORM-DEV] Failed to grab string for CONFIG filename. Aborting.\n");
                        pi_cmd_result = PI_RES_FAILED;
                    } else {
                        FILE *tmp = fopen(cfg_filename, "rb");
                        if (tmp == NULL) {
                            printf("[PISTORM-DEV] Failed to open CONFIG file %s for reading. Aborting.\n", cfg_filename);
                            pi_cmd_result = PI_RES_FILENOTFOUND;
                        } else {
                            fclose(tmp);
                            printf("[PISTORM-DEV] Attempting to load config file %s...\n", cfg_filename);
                            load_new_config = val + 1;
                            pi_cmd_result = PI_RES_OK;
                        }
                    }
                    pi_string[0] = 0;
                    break;
                case PICFG_RELOAD:
                    DEBUG("RELOAD\n");
                    printf("[PISTORM-DEV] Reloading current config file (%s)...\n", cfg_filename);
                    load_new_config = val + 1;
                    break;
                case PICFG_DEFAULT:
                    DEBUG("DEFAULT\n");
                    printf("[PISTORM-DEV] Loading default.cfg...\n");
                    load_new_config = val + 1;
                    break;
                default:
                    DEBUG("UNKNOWN/UNHANDLED. Command ignored.\n");
                    pi_cmd_result = PI_RES_INVALIDVALUE;
                    break;
            }
            break;
        default:
            DEBUG("[PISTORM-DEV] WARN: Unhandled %s register write to %.4X: %d\n", op_type_names[type], addr - pistorm_dev_base, val);
            pi_cmd_result = PI_RES_INVALIDCMD;
            break;
    }
}
 
uint32_t handle_pistorm_dev_read(uint32_t addr_, uint8_t type) {
    uint32_t addr = (addr_ & 0xFFFF);

    switch((addr)) {
        case PI_CMD_HWREV:
            // Probably replace this with some read from the CPLD to get a simple hardware revision.
            DEBUG("[PISTORM-DEV] %s Read from HWREV\n", op_type_names[type]);
            return 0x0101; // 1.1
            break;
        case PI_CMD_SWREV:
            DEBUG("[PISTORM-DEV] %s Read from SWREV\n", op_type_names[type]);
            return PIDEV_SWREV;
            break;
        case PI_CMD_RTGSTATUS:
            DEBUG("[PISTORM-DEV] %s Read from RTGSTATUS\n", op_type_names[type]);
            return (rtg_on << 1) | rtg_enabled;
            break;
        case PI_CMD_NETSTATUS:
            DEBUG("[PISTORM-DEV] %s Read from NETSTATUS\n", op_type_names[type]);
            return pinet_enabled;
            break;
        case PI_CMD_PISCSI_CTRL:
            DEBUG("[PISTORM-DEV] %s Read from PISCSI_CTRL\n", op_type_names[type]);
            return piscsi_enabled;
            break;

        case PI_DBG_VAL1: case PI_DBG_VAL2: case PI_DBG_VAL3: case PI_DBG_VAL4:
        case PI_DBG_VAL5: case PI_DBG_VAL6: case PI_DBG_VAL7: case PI_DBG_VAL8:
            DEBUG("[PISTORM-DEV] Read DEBUG VALUE %d (%d / $%.8X)\n", (addr - PI_DBG_VAL1) / 4, pi_dbg_val[(addr - PI_DBG_VAL1) / 4], pi_dbg_val[(addr - PI_DBG_VAL1) / 4]);
            return pi_dbg_val[(addr - PI_DBG_VAL1) / 4];
            break;

        case PI_BYTE1: case PI_BYTE2: case PI_BYTE3: case PI_BYTE4:
        case PI_BYTE5: case PI_BYTE6: case PI_BYTE7: case PI_BYTE8:
            DEBUG("[PISTORM-DEV] Read BYTE %d (%d / $%.2X)\n", addr - PI_BYTE1, pi_byte[addr - PI_BYTE1], pi_byte[addr - PI_BYTE1]);
            return pi_byte[addr - PI_BYTE1];
            break;
        case PI_WORD1: case PI_WORD2: case PI_WORD3: case PI_WORD4:
            DEBUG("[PISTORM-DEV] Read WORD %d (%d / $%.4X)\n", (addr - PI_WORD1) / 2, pi_word[(addr - PI_WORD1) / 2], pi_word[(addr - PI_WORD1) / 2]);
            return pi_word[(addr - PI_WORD1) / 2];
            break;
        case PI_LONGWORD1: case PI_LONGWORD2: case PI_LONGWORD3: case PI_LONGWORD4:
            DEBUG("[PISTORM-DEV] Read LONGWORD %d (%d / $%.8X)\n", (addr - PI_LONGWORD1) / 4, pi_longword[(addr - PI_LONGWORD1) / 4], pi_longword[(addr - PI_LONGWORD1) / 4]);
            return pi_longword[(addr - PI_LONGWORD1) / 4];
            break;

        case PI_CMDRESULT:
            DEBUG("[PISTORM-DEV] %s Read from CMDRESULT\n", op_type_names[type]);
            return pi_cmd_result;
            break;

        default:
            DEBUG("[PISTORM-DEV] WARN: Unhandled %s register read from %.4X\n", op_type_names[type], addr - pistorm_dev_base);
            break;
    }
    return 0;
}
