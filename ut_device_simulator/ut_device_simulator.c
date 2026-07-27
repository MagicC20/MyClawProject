#include "ut_device_simulator.h"
#include <string.h>
#include <stdio.h>
#include <ctype.h>

/*===========================================================================
 * 全局变量
 *==========================================================================*/

char g_rx_buf[RX_BUF_SIZE];
char g_tx_buf[TX_BUF_SIZE];
uint32_t g_rx_len = 0;
uint32_t g_tx_len = 0;

/*===========================================================================
 * 内部数据结构
 *==========================================================================*/

/** 寄存器表，按编号1~37索引 */
static RegInfo_t g_regs[REG_COUNT_MAX + 1]; /* [1..37] */

/*===========================================================================
 * 内部函数声明
 *==========================================================================*/

static uint8_t HexCharToVal(char c);
static void ValToHexStr(uint32_t val, char *out);
static int FindRegByWriteCmd(uint8_t cmd, uint8_t *p_reg_id);
static int FindRegByReadCmd(uint8_t cmd, uint8_t *p_reg_id);
static int BuildTxMsg(uint32_t value);

/*===========================================================================
 * 寄存器表初始化
 *==========================================================================*/

/**
 * @brief 初始化寄存器表
 * @note  读写命令以ASCII字符形式存储，如0x03存储为'03'
 *
 * 寄存器定义（按编号1~37顺序）：
 *  1: NA-01    不可写，可读(01)
 *  2: NA-03    不可写，可读(03)
 *  3: NA-02    不可写，可读(02)
 *  4: NA-05    不可写，可读(05)
 *  5: NA-06    不可写，可读(06)
 *  6: NA-07    不可写，可读(07)
 *  7: 28-41    写(28)，读(41)
 *  8: 29-42    写(29)，读(42)
 *  9: 2a-43    写(2a)，读(43)
 * 10: 2b-44    写(2b)，读(44)
 * 11: 2c-45    写(2c)，读(45)
 * 12: 3d-46    写(3d)，读(46)
 * 13: 2e-47    写(2e)，读(47)
 * 14: 1c-50    写(1c)，读(50)
 * 15: 1d-51    写(1d)，读(51)
 * 16: 1e-52    写(1e)，读(52)
 * 17: 1f-53    写(1f)，读(53)
 * 18: 20-54    写(20)，读(54)
 * 19: 21-55    写(21)，读(55)
 * 20: 22-56    写(22)，读(56)
 * 21: 23-57    写(23)，读(57)
 * 22: 24-58    写(24)，读(58)
 * 23: 25-59    写(25)，读(59)
 * 24: 26-5a    写(26)，读(5a)
 * 25: 27-5b    写(27)，读(5b)
 * 26: 0c-5c    写(0c)，读(5c)
 * 27: 0d-5d    写(0d)，读(5d)
 * 28: 0e-5e    写(0e)，读(5e)
 * 29: 2f-48    写(2f)，读(48)
 * 30: 30-49    写(30)，读(49)
 * 31: 33-NA    写(33)，不可读
 * 32: 31-4a    写(31)，读(4a)
 * 33: 32-4b    写(32)，读(4b)
 * 34: 34-4c    写(34)，读(4c)
 * 35: 35-4d    写(35)，读(4d)
 * 36: 0f-5f    写(0f)，读(5f)
 * 37: 36-4e    写(36)，读(4e)
 */
static void InitRegTable(void)
{
    /* 写命令表（按寄存器编号1~37） */
    static const uint8_t write_cmds[REG_COUNT_MAX + 1] = {
        0,  /* [0] 占位，无效 */
        0xFF, /* 1: NA  */
        0xFF, /* 2: NA  */
        0xFF, /* 3: NA  */
        0xFF, /* 4: NA  */
        0xFF, /* 5: NA  */
        0xFF, /* 6: NA  */
        0x28, /* 7: 28  */
        0x29, /* 8: 29  */
        0x2A, /* 9: 2a  */
        0x2B, /* 10: 2b */
        0x2C, /* 11: 2c */
        0x3D, /* 12: 3d */
        0x2E, /* 13: 2e */
        0x1C, /* 14: 1c */
        0x1D, /* 15: 1d */
        0x1E, /* 16: 1e */
        0x1F, /* 17: 1f */
        0x20, /* 18: 20 */
        0x21, /* 19: 21 */
        0x22, /* 20: 22 */
        0x23, /* 21: 23 */
        0x24, /* 22: 24 */
        0x25, /* 23: 25 */
        0x26, /* 24: 26 */
        0x27, /* 25: 27 */
        0x0C, /* 26: 0c */
        0x0D, /* 27: 0d */
        0x0E, /* 28: 0e */
        0x2F, /* 29: 2f */
        0x30, /* 30: 30 */
        0x33, /* 31: 33  */
        0x31, /* 32: 31  */
        0x32, /* 33: 32  */
        0x34, /* 34: 34  */
        0x35, /* 35: 35  */
        0x0F, /* 36: 0f  */
        0x36  /* 37: 36  */
    };

    /* 读命令表（按寄存器编号1~37） */
    static const uint8_t read_cmds[REG_COUNT_MAX + 1] = {
        0,   /* [0] 占位，无效 */
        0x01, /* 1: 01  */
        0x03, /* 2: 03  */
        0x02, /* 3: 02  */
        0x05, /* 4: 05  */
        0x06, /* 5: 06  */
        0x07, /* 6: 07  */
        0x41, /* 7: 41  */
        0x42, /* 8: 42  */
        0x43, /* 9: 43  */
        0x44, /* 10: 44 */
        0x45, /* 11: 45 */
        0x46, /* 12: 46 */
        0x47, /* 13: 47 */
        0x50, /* 14: 50 */
        0x51, /* 15: 51 */
        0x52, /* 16: 52 */
        0x53, /* 17: 53 */
        0x54, /* 18: 54 */
        0x55, /* 19: 55 */
        0x56, /* 20: 56 */
        0x57, /* 21: 57 */
        0x58, /* 22: 58 */
        0x59, /* 23: 59 */
        0x5A, /* 24: 5a */
        0x5B, /* 25: 5b */
        0x5C, /* 26: 5c */
        0x5D, /* 27: 5d */
        0x5E, /* 28: 5e */
        0x48, /* 29: 48 */
        0x49, /* 30: 49 */
        0xFF, /* 31: NA 不可读 */
        0x4A, /* 32: 4a */
        0x4B, /* 33: 4b */
        0x4C, /* 34: 4c */
        0x4D, /* 35: 4d */
        0x5F, /* 36: 5f */
        0x4E  /* 37: 4e */
    };

    for (int i = 1; i <= REG_COUNT_MAX; i++) {
        g_regs[i].reg_id     = (uint8_t)i;
        g_regs[i].write_cmd  = write_cmds[i];
        g_regs[i].read_cmd   = read_cmds[i];
        g_regs[i].value      = 0;
        g_regs[i].readable   = (read_cmds[i] != 0xFF);
        g_regs[i].writable   = (write_cmds[i] != 0xFF);
    }
}

/*===========================================================================
 * 工具函数
 *==========================================================================*/

/**
 * @brief 将ASCII十六进制字符转换为数值
 */
static uint8_t HexCharToVal(char c)
{
    if (c >= '0' && c <= '9') return (uint8_t)(c - '0');
    if (c >= 'A' && c <= 'F') return (uint8_t)(c - 'A' + 10);
    if (c >= 'a' && c <= 'f') return (uint8_t)(c - 'a' + 10);
    return 0xFF;
}

/**
 * @brief 将数值转换为8位十六进制字符串（大端，ASCII格式）
 * @param val  待转换数值
 * @param out  输出缓冲区，至少9字节
 */
static void ValToHexStr(uint32_t val, char *out)
{
    static const char hex_chars[] = "0123456789ABCDEF";
    /* 大端序输出，bbbbbbbb为8个十六进制字符 */
    out[0] = hex_chars[(val >> 28) & 0x0F];
    out[1] = hex_chars[(val >> 24) & 0x0F];
    out[2] = hex_chars[(val >> 20) & 0x0F];
    out[3] = hex_chars[(val >> 16) & 0x0F];
    out[4] = hex_chars[(val >> 12) & 0x0F];
    out[5] = hex_chars[(val >>  8) & 0x0F];
    out[6] = hex_chars[(val >>  4) & 0x0F];
    out[7] = hex_chars[(val >>  0) & 0x0F];
    out[8] = '\0';
}

/**
 * @brief 根据读命令字查找对应寄存器编号
 * @param cmd   命令字
 * @param p_reg_id 输出参数，寄存器编号
 * @retval 0   成功
 * @retval -1  未找到
 */
static int FindRegByReadCmd(uint8_t cmd, uint8_t *p_reg_id)
{
    for (int i = 1; i <= REG_COUNT_MAX; i++) {
        if (g_regs[i].readable && g_regs[i].read_cmd == cmd) {
            *p_reg_id = (uint8_t)i;
            return 0;
        }
    }
    return -1;
}

/**
 * @brief 根据写命令字查找对应寄存器编号
 * @param cmd   命令字
 * @param p_reg_id 输出参数，寄存器编号
 * @retval 0   成功
 * @retval -1  未找到
 */
static int FindRegByWriteCmd(uint8_t cmd, uint8_t *p_reg_id)
{
    for (int i = 1; i <= REG_COUNT_MAX; i++) {
        if (g_regs[i].writable && g_regs[i].write_cmd == cmd) {
            *p_reg_id = (uint8_t)i;
            return 0;
        }
    }
    return -1;
}

/**
 * @brief 将十六进制字符串转换为数值（ASCII格式的大端序）
 * @param str  8字符十六进制字符串
 * @return 解析出的数值
 */
static uint32_t HexStrToVal(const char *str)
{
    uint32_t val = 0;
    for (int i = 0; i < 8; i++) {
        uint8_t nibble = HexCharToVal(str[i]);
        val = (val << 4) | nibble;
    }
    return val;
}

/*===========================================================================
 * 公共接口实现
 *==========================================================================*/

void Simulator_Init(void)
{
    memset(g_rx_buf, 0, sizeof(g_rx_buf));
    memset(g_tx_buf, 0, sizeof(g_tx_buf));
    g_rx_len = 0;
    g_tx_len = 0;
    InitRegTable();
}

void Simulator_SetRx(const char *data, uint32_t len)
{
    if (len >= RX_BUF_SIZE) {
        len = RX_BUF_SIZE - 1;
    }
    memcpy(g_rx_buf, data, len);
    g_rx_buf[len] = '\0';
    g_rx_len = len;
}

const char* Simulator_GetTx(uint32_t *p_len)
{
    if (p_len) {
        *p_len = g_tx_len;
    }
    return g_tx_buf;
}

int Simulator_SetReg(uint8_t reg_id, uint32_t value)
{
    if (reg_id < 1 || reg_id > REG_COUNT_MAX) {
        return -1; /* 无效寄存器编号 */
    }
    if (!g_regs[reg_id].writable) {
        return -2; /* 不可写 */
    }
    g_regs[reg_id].value = value;
    return 0;
}

int Simulator_GetReg(uint8_t reg_id, uint32_t *p_value)
{
    if (reg_id < 1 || reg_id > REG_COUNT_MAX) {
        return -1; /* 无效寄存器编号 */
    }
    if (!g_regs[reg_id].readable) {
        return -2; /* 不可读 */
    }
    if (p_value) {
        *p_value = g_regs[reg_id].value;
    }
    return 0;
}

uint8_t Calc_Checksum(const char *data, uint32_t len)
{
    uint32_t sum = 0;
    for (uint32_t i = 0; i < len; i++) {
        sum += (uint8_t)data[i];
    }
    return (uint8_t)(sum & 0x7F); /* 模128 */
}

/**
 * @brief 解析上位机报文并构造回复
 *
 * 报文格式：
 *   下发：*62aabbbbbbbbcc^
 *   回复：*bbbbbbbbcc^
 *
 * 解析流程：
 *   1. 校验报文头尾
 *   2. 校验设备地址(0x62)
 *   3. 解析命令字aa，判断读/写
 *   4. 解析bbbbbbbb值
 *   5. 校验校验码
 *   6. 执行读/写操作
 *   7. 构造回复报文
 */
int Simulator_ParseRx(void)
{
    uint8_t dev_addr;
    uint8_t cmd;
    uint32_t value;
    uint8_t checksum_received;
    uint8_t checksum_calc;
    uint8_t reg_id;
    int ret;

    /* 最小报文长度检查：*62aabbbbbbbbcc^ = 14字符 */
    if (g_rx_len < 14) {
        return -1;
    }

    /* 头尾字符检查 */
    if (g_rx_buf[0] != MSG_START || g_rx_buf[g_rx_len - 1] != MSG_END) {
        return -1;
    }

    /* 设备地址 */
    dev_addr = HexCharToVal(g_rx_buf[1]);
    dev_addr = (dev_addr << 4) | HexCharToVal(g_rx_buf[2]);
    if (dev_addr != DEVICE_ADDR) {
        return -1;
    }

    /* 命令字 */
    cmd = HexCharToVal(g_rx_buf[3]);
    cmd = (cmd << 4) | HexCharToVal(g_rx_buf[4]);

    /* 值 */
    value = HexStrToVal(&g_rx_buf[5]);

    /* 校验码 */
    checksum_received = HexCharToVal(g_rx_buf[13]);
    checksum_received = (checksum_received << 4) | HexCharToVal(g_rx_buf[14]);

    /* 计算校验和（不含*、^和cc，即data[1..12]共12字节） */
    checksum_calc = Calc_Checksum(&g_rx_buf[1], 12);
    if (checksum_received != checksum_calc) {
        return -2;
    }

    /*
     * 判断是读还是写：遍历寄存器表
     * 写命令匹配 -> 写操作
     * 读命令匹配 -> 读操作
     */
    if (FindRegByWriteCmd(cmd, &reg_id) == 0) {
        /* 写操作 */
        g_regs[reg_id].value = value;
        ret = BuildTxMsg(value);
        return ret;
    } else if (FindRegByReadCmd(cmd, &reg_id) == 0) {
        /* 读操作 */
        ret = BuildTxMsg(g_regs[reg_id].value);
        return ret;
    } else {
        return -3; /* 未知命令 */
    }
}

/**
 * @brief 构造回复报文
 * @param value 要返回的值
 * @retval 0 成功
 */
static int BuildTxMsg(uint32_t value)
{
    char body[16]; /* 存放*bbbbbbbbcc共10字节 */
    uint8_t checksum;

    /* 构造报文体：*bbbbbbbbcc^ */
    g_tx_buf[0] = MSG_START;

    ValToHexStr(value, &g_tx_buf[1]); /* bbbbbbbb */

    /* 计算校验和：bbbbbbbb（8字节） */
    checksum = Calc_Checksum(&g_tx_buf[1], 8);

    /* 校验码 */
    static const char hex_chars[] = "0123456789ABCDEF";
    g_tx_buf[9]  = hex_chars[(checksum >> 4) & 0x0F];
    g_tx_buf[10] = hex_chars[checksum & 0x0F];

    g_tx_buf[11] = MSG_END;
    g_tx_buf[12] = '\0';

    g_tx_len = 11; /* * + 8hex + 2cc + ^ = 11 */

    (void)body; /* 消除未使用警告 */
    return 0;
}
