#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <linux/i2c-dev.h>
#include <linux/i2c.h>
#include <linux/ioctl.h>
#include <pthread.h>
#include <sys/ioctl.h>
#include <sys/resource.h>
#include <unistd.h>

#include "chipid.h"
#include "hal_common.h"
#include "tools.h"

static unsigned char sony_addrs[] = {0x34, 0};
static unsigned char soi_addrs[] = {0x80, 0};
static unsigned char onsemi_addrs[] = {0x20, 0};
static unsigned char ssens_addrs[] = {0x60, 0};
static unsigned char omni_addrs[] = {0x60, 0};

sensor_addr_t hisi_possible_i2c_addrs[] = {{SENSOR_SONY, sony_addrs},
                                           {SENSOR_SOI, soi_addrs},
                                           {SENSOR_ONSEMI, onsemi_addrs},
                                           {SENSOR_SMARTSENS, ssens_addrs},
                                           {SENSOR_OMNIVISION, omni_addrs},
                                           {0, NULL}};

int hisi_open_sensor_fd() {
    int adapter_nr = 0; /* probably dynamically determined */
    char filename[FILENAME_MAX];

    snprintf(filename, sizeof(filename), "/dev/i2c-%d", adapter_nr);

    return common_open_sensor_fd(filename);
}

// Set I2C slave address
int hisi_sensor_i2c_change_addr(int fd, unsigned char addr) {
    // use i2c address shift only for generations other than 2
    if (chip_generation != 0x3518E200) {
        addr >>= 1;
    }

    int ret = ioctl(fd, I2C_SLAVE_FORCE, addr);
    if (ret < 0) {
        fprintf(stderr, "CMD_SET_DEV error!\n");
        return ret;
    }
    return ret;
}

#define I2C_16BIT_REG 0x0709  /* 16BIT REG WIDTH */
#define I2C_16BIT_DATA 0x070a /* 16BIT DATA WIDTH */
int hisi_gen2_set_width(int fd, unsigned int reg_width,
                        unsigned int data_width) {
    int ret;
    if (reg_width == 2)
        ret = ioctl(fd, I2C_16BIT_REG, 1);
    else
        ret = ioctl(fd, I2C_16BIT_REG, 0);
    if (ret < 0) {
        fprintf(stderr, "CMD_SET_REG_WIDTH error!\n");
        return -1;
    }

    if (data_width == 2)
        ret = ioctl(fd, I2C_16BIT_DATA, 1);
    else
        ret = ioctl(fd, I2C_16BIT_DATA, 0);

    if (ret < 0) {
        fprintf(stderr, "CMD_SET_DATA_WIDTH error!\n");
        return -1;
    }
    return 0;
}

int hisi_gen2_sensor_write_register(int fd, unsigned char i2c_addr,
                                    unsigned int reg_addr,
                                    unsigned int reg_width, unsigned int data,
                                    unsigned int data_width) {

    int ret;
    unsigned int index = 0;
    unsigned int reg_value;
    char buf[4];

    if (hisi_gen2_set_width(fd, reg_width, data_width))
        return -1;

    if (reg_width == 2) {
        buf[index] = reg_addr & 0xff;
        index++;
        buf[index] = (reg_addr >> 8) & 0xff;
        index++;
    } else {
        buf[index] = reg_addr & 0xff;
        index++;
    }

    if (data_width == 2) {
        buf[index] = reg_value & 0xff;
        index++;
        buf[index] = (reg_value >> 8) & 0xff;
        index++;
    } else {
        buf[index] = reg_value & 0xff;
        index++;
    }

    ret = write(fd, buf, reg_width + data_width);
    if (ret < 0) {
        fprintf(stderr, "I2C_WRITE error!\n");
        return -1;
    }

    return 0;
}

int hisi_sensor_write_register(int fd, unsigned char i2c_addr,
                               unsigned int reg_addr, unsigned int reg_width,
                               unsigned int data, unsigned int data_width) {
    int idx = 0;
    int ret;
    char buf[8];

    if (reg_width == 2) {
        buf[idx] = (reg_addr >> 8) & 0xff;
        idx++;
        buf[idx] = reg_addr & 0xff;
        idx++;
    } else {
        buf[idx] = reg_addr & 0xff;
        idx++;
    }

    if (data_width == 2) {
        buf[idx] = (data >> 8) & 0xff;
        idx++;
        buf[idx] = data & 0xff;
        idx++;
    } else {
        buf[idx] = data & 0xff;
        idx++;
    }

    ret = write(fd, buf, (reg_addr + data));
    if (ret < 0) {
        printf("I2C_WRITE error!\n");
        return -1;
    }
    return 0;
}

int hisi_gen2_sensor_read_register(int fd, unsigned char i2c_addr,
                                   unsigned int reg_addr,
                                   unsigned int reg_width,
                                   unsigned int data_width) {
    int ret;
    char recvbuf[4];
    unsigned int data;

    if (hisi_gen2_set_width(fd, reg_width, data_width))
        return -1;

    if (reg_width == 2) {
        recvbuf[0] = reg_addr & 0xff;
        recvbuf[1] = (reg_addr >> 8) & 0xff;
    } else {
        recvbuf[0] = reg_addr & 0xff;
    }

    ret = read(fd, recvbuf, reg_width);
    if (ret < 0) {
        return -1;
    }

    if (data_width == 2) {
        data = recvbuf[0] | (recvbuf[1] << 8);
    } else
        data = recvbuf[0];

    return data;
}

int hisi_sensor_read_register(int fd, unsigned char i2c_addr,
                              unsigned int reg_addr, unsigned int reg_width,
                              unsigned int data_width) {
    static struct i2c_rdwr_ioctl_data rdwr;
    static struct i2c_msg msg[2];
    unsigned int reg_addr_end = reg_addr;
    unsigned char buf[4];
    unsigned int data;

    // measure ioctl execution time to exit early in too slow response
    struct rusage start_time;
    int ret = getrusage(RUSAGE_SELF, &start_time);

    memset(buf, 0x0, sizeof(buf));

    msg[0].addr = i2c_addr >> 1;
    msg[0].flags = 0;
    msg[0].len = reg_width;
    msg[0].buf = buf;

    msg[1].addr = i2c_addr >> 1;
    msg[1].flags = 0;
    msg[1].flags |= I2C_M_RD;
    msg[1].len = data_width;
    msg[1].buf = buf;

    rdwr.msgs = &msg[0];
    rdwr.nmsgs = (__u32)2;

    for (int cur_addr = reg_addr; cur_addr <= reg_addr_end; cur_addr += 1) {
        if (reg_width == 2) {
            buf[0] = (cur_addr >> 8) & 0xff;
            buf[1] = cur_addr & 0xff;
        } else
            buf[0] = cur_addr & 0xff;

        int retval = ioctl(fd, I2C_RDWR, &rdwr);
        struct rusage end_time;
        int ret = getrusage(RUSAGE_SELF, &end_time);
        if (end_time.ru_stime.tv_sec - start_time.ru_stime.tv_sec > 2) {
            fprintf(stderr, "Buggy I2C driver detected! Load all ko modules\n");
            exit(2);
        }
        start_time = end_time;

        if (retval != 2) {
            return -1;
        }

        if (data_width == 2) {
            data = buf[1] | (buf[0] << 8);
        } else
            data = buf[0];
    }

    return data;
}

void setup_hal_hisi() {
    open_sensor_fd = hisi_open_sensor_fd;
    sensor_i2c_change_addr = hisi_sensor_i2c_change_addr;
    if (chip_generation == 0x3518E200) {
        sensor_read_register = hisi_gen2_sensor_read_register;
        sensor_write_register = hisi_gen2_sensor_write_register;
    } else {
        sensor_read_register = hisi_sensor_read_register;
        sensor_write_register = hisi_sensor_write_register;
    }
    possible_i2c_addrs = hisi_possible_i2c_addrs;
    strcpy(short_manufacturer, "HI");
}

int hisi_SYS_DRV_GetChipId() {
    int fd = open("/dev/sys", O_RDWR);
    if (fd < 0)
        return -1;

    uint32_t id;
    int res = ioctl(fd, 0x80045910, &id);
    if (res)
        return -1;
    return id;
}

enum CV300_MIPI_PHY {
    PHY_MIPI_MODE = 0,
    PHY_LVDS_MODE,
    PHY_CMOS_MODE,
    PHY_RESERVED
};

struct CV300_MISC_CTRL0 {
    unsigned int res0 : 1;
    unsigned int spi0_cs0_pctrl : 1;
    unsigned int spi1_cs0_pctrl : 1;
    unsigned int spi1_cs1_pctrl : 1;
    unsigned int ssp1_cs_sel : 1;
    enum CV300_MIPI_PHY mipi_phy_mode : 3;
    bool vicap_vpss_online_mode : 1;
    unsigned int test_clksel : 4;
    unsigned int res1 : 2;
    bool commtx_rx_int_en : 1;
};

struct CV300_PERI_CRG11 {
    unsigned int vi0_cken : 1;
    unsigned int vi0_pctrl : 1;
    unsigned int vi0_sc_sel : 3;
    unsigned int res0 : 3;
    unsigned int vi0_srst_req : 1;
    unsigned int vi_hrst_req : 1;
    unsigned int vi_ch0_srst_req : 1;
    unsigned int mipi_srst_req : 1;
    unsigned int res1 : 1;
    unsigned int isp_core_srst_req : 1;
    unsigned int isp_cfg_srst_req : 1;
    unsigned int mipi_cken : 1;
    unsigned int mipi_core_srst_req : 1;
    unsigned int isp_clksel : 1;
    unsigned int isp_cken : 1;
    unsigned int phy_hs_lat : 2;
    unsigned int phy_cmos_lat : 2;
    unsigned int sensor_clksel : 3;
    unsigned int sensor_cken : 1;
    unsigned int sensor_srst_req : 1;
    unsigned int res2 : 4;
};

const unsigned int CV300_MISC_CTRL0_ADDR = 0x12030000;
const char *hisi_cv300_get_sensor_data_type() {
    struct CV300_MISC_CTRL0 ctrl0;
    bool res = read_mem_reg(CV300_MISC_CTRL0_ADDR, (uint32_t *)&ctrl0);
    if (res) {
        switch (ctrl0.mipi_phy_mode) {
        case PHY_MIPI_MODE:
            return "MIPI";
        case PHY_CMOS_MODE:
            return "DC";
        case PHY_LVDS_MODE:
            return "LVDS";
        default:
            return NULL;
        }
    }
    return NULL;
}

const unsigned int CV300_PERI_CRG11_ADRR = 0x1201002c;
const char *hisi_cv300_get_sensor_clock() {
    struct CV300_PERI_CRG11 crg11;
    int res = read_mem_reg(CV300_PERI_CRG11_ADRR, (uint32_t *)&crg11);
    if (res) {
        switch (crg11.sensor_clksel) {
        case 0:
            return "74.25MHz";
        case 1:
            return "37.125MHz";
            break;
        case 2:
            return "54MHz";
            break;
        case 3:
            return "27MHz";
            break;
        default:
            if (crg11.sensor_clksel & 1) {
                return "25MHz";
            } else {
                return "24MHz";
            }
        }
    }
    return NULL;
}
