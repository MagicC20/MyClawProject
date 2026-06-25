#include "ut_simulator.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

/* ============================================================
 * 寄存器定义表
 * 按寄存器编号 1~37 依次定义：
 *   write_cmd = 0xFF 表示不可写
 *   read_cmd  = 0xFF 表示不可读
 * ============================================================ */

static const RegDef REG_DEF_TABLE[REGISTER_COUNT] = {
    /* Reg 1  */ { .write_cmd = 0xFF, .read_cmd = 0x01, .writable = false, .readable = true  },
    /* Reg 2  */ { .write_cmd = 0xFF, .read_cmd = 0x03, .writable = false, .readable = true  },
    /* Reg 3  */ { .write_cmd = 0xFF, .read_cmd = 0x02, .writable = false, .readable = true  },
    /* Reg 4  */ { .write_cmd = 0xFF, .read_cmd = 0x05, .writable = false, .readable = true  },
    /* Reg 5  */ { .write_cmd = 0xFF, .read_cmd = 0x06, .writable = false, .readable = true  },
    /* Reg 6  */ { .write_cmd = 0xFF, .read_cmd = 0x07, .writable = false, .readable = true  },
    /* Reg 7  */ { .write_cmd = 0x28, .read_cmd = 0x41, .writable = true,  .readable = true  },
    /* Reg 8  */ { .write_cmd = 0x29, .read_cmd = 0x42, .writable = true,  .readable = true  },
    /* Reg 9  */ { .write_cmd = 0x2A, .read_cmd = 0x43, .writable = true,  .readable = true  },
    /* Reg 10 */ { .write_cmd = 0x2B, .read_cmd = 0x44, .writable = true,  .readable = true  },
    /* Reg 11 */ { .write_cmd = 0x2C, .read_cmd = 0x45, .writable = true,  .readable = true  },
    /* Reg 12 */ { .write_cmd = 0x3D, .read_cmd = 0x46, .writable = true,  .readable = true  },
    /* Reg 13 */ { .write_cmd = 0x2E, .read_cmd = 0x47, .writable = true,  .readable = true  },
    /* Reg 14 */ { .write_cmd = 0x1C, .read_cmd = 0x50, .writable = true,  .readable = true  },
    /* Reg 15 */ { .write_cmd = 0x1D, .read_cmd = 0x51, .writable = true,  .readable = true  },
    /* Reg 16 */ { .write_cmd = 0x1E, .read_cmd = 0x52, .writable = true,  .readable = true  },
    /* Reg 17 */ { .write_cmd = 0x1F, .read_cmd = 0x53, .writable = true,  .readable = true  },
    /* Reg 18 */ { .write_cmd = 0x20, .read_cmd = 0x54, .writable = true,  .readable = true  },
    /* Reg 19 */ { .write_cmd = 0x21, .read_cmd = 0x55, .writable = true,  .readable = true  },
    /* Reg 20 */ { .write_cmd = 0x22, .read_cmd = 0x56, .writable = true,  .readable = true  },
    /* Reg 21 */ { .write_cmd = 0x23, .read_cmd = 0x57, .writable = true,  .readable = true  },
    /* Reg 22 */ { .write_cmd = 0x24, .read_cmd = 0x58, .writable = true,  .readable = true  },
    /* Reg 23 */ { .write_cmd = 0x25, .read_cmd = 0x59, .writable = true,  .readable = true  },
    /* Reg 24 */ { .write_cmd = 0x26, .read_cmd = 0x5A, .writable = true,  .readable = true  },
    /* Reg 25 */ { .write_cmd = 0x27, .read_cmd = 0x5B, .writable = true,  .readable = true  },
    /* Reg 26 */ { .write_cmd = 0x0C, .read_cmd = 0x5C, .writable = true,  .readable = true  },
    /* Reg 27 */ { .write_cmd = 0x0D, .read_cmd = 0x5D, .writable = true,  .readable = true  },
    /* Reg 28 */ { .write_cmd = 0x0E, .read_cmd = 0x5E, .writable = true,  .readable = true  },
    /* Reg 29 */ { .write_cmd = 0x2F, .read_cmd = 0x48, .writable = true,  .readable = true  },
    /* Reg 30 */ { .write_cmd = 0x30, .read_cmd = 0x49, .writable = true,  .readable = true  },
    /* Reg 31 */ { .write_cmd = 0x33, .read_cmd = 0xFF, .writable = true,  .readable = false },
    /* Reg 32 */ { .write_cmd = 0x31, .read_cmd = 0x4A, .writable = true,  .readable = true  },
    /* Reg 33 */ { .write_cmd = 0x32, .read_cmd = 0x4B, .writable = true,  .readable = true  },
    /* Reg 34 */ { .write_cmd = 0x34, .read_cmd = 0x4C, .writable = true,  .readable = true  },
    /* Reg 35 */ { .write_cmd = 0x35, .read_cmd = 0x4D, .writable = true,  .readable = true  },
    /* Reg 36 */ { .write_cmd = 0x0F, .read_cmd = 0x5F, .writable = true,  .readable = true  },
    /* Reg 37 */ { .write_cmd = 0x36, .read_cmd = 0x4E, .writable = true,  .readable = true  },
};

/* ============================================================
 * 内部工具函数
 * ============================================================ */

static inline bool is_valid_hex_char(char c)
{
    return ((c >= '0' && c <= '9') ||
            (c >= 'A' && c <= 'F') ||
            (c >= 'a' && c <= 'f'));
}

static uint8_t hex_char_to_nibble(char c)
{
    if (c >= '0' && c <= '9') return (uint8_t)(c - '0');
    if (c >= 'A' && c <= 'F') return (uint8_t)(c - 'A' + 10);
    if (c >= 'a' && c <= 'f') return (uint8_t)(c - 'a' + 10);
    return 0;
}

/**
 * hex_to_reg_value - 将 8 字符十六进制字符串转换为 RegValue
 * 例: "000000AB" -> 0xAB
 */
static bool hex_to_reg_value(const char *hex_str, RegValue *p_val)
{
    RegValue val = 0;
    for (int i = 0; i < 8; i++) {
        if (!is_valid_hex_char(hex_str[i]))
            return false;
        val = (val << 4) | hex_char_to_nibble(hex_str[i]);
    }
    *p_val = val;
    return true;
}

/**
 * reg_value_to_hex - 将 RegValue 转换为 8 字符十六进制字符串
 */
static void reg_value_to_hex(RegValue val, char *out_hex)
{
    static const char hex_chars[] = "0123456789ABCDEF";
    for (int i = 7; i >= 0; i--) {
        out_hex[i] = hex_chars[val & 0xF];
        val >>= 4;
    }
    out_hex[8] = '\0';
}

/**
 * calc_checksum - 计算报文的校验码（模 128）
 * @buf:   报文指针
 * @start: 参与校验的起始索引（含）
 * @end:   参与校验的结束索引（含）
 *         返回 8 位校验和（0~127）
 */
static uint8_t calc_checksum(const char *buf, int start, int end)
{
    uint16_t sum = 0;
    for (int i = start; i <= end; i++) {
        sum += (uint8_t)buf[i];
    }
    return (uint8_t)(sum & 0x7F);  /* modulo 128 */
}

/* ============================================================
 * 初始化 & 复位
 * ============================================================ */

void Sim_Init(UtSimulator *sim)
{
    memset(sim, 0, sizeof(*sim));

    /* 初始化所有寄存器值为 0 */
    for (int i = 0; i < REGISTER_COUNT; i++) {
        sim->reg[i] = REG_VALUE_ZERO;
        sim->reg_def[i] = REG_DEF_TABLE[i];
    }

    /* 初始化命令码 -> 寄存器索引 表（默认 -1 表示无效） */
    for (int i = 0; i < 256; i++) {
        sim->cmd_to_reg_write[i] = -1;
        sim->cmd_to_reg_read[i]  = -1;
    }

    /* 填充索引表 */
    for (int i = 0; i < REGISTER_COUNT; i++) {
        uint8_t wcmd = sim->reg_def[i].write_cmd;
        uint8_t rcmd = sim->reg_def[i].read_cmd;
        if (wcmd != 0xFF) sim->cmd_to_reg_write[wcmd] = (int8_t)i;
        if (rcmd != 0xFF) sim->cmd_to_reg_read[rcmd]  = (int8_t)i;
    }

    /* 清空缓冲区 */
    sim->tx_buf[0] = '\0';
    sim->rx_buf[0] = '\0';
}

void Sim_Reset(UtSimulator *sim)
{
    for (int i = 0; i < REGISTER_COUNT; i++) {
        sim->reg[i] = REG_VALUE_ZERO;
    }
    sim->tx_buf[0] = '\0';
    sim->rx_buf[0] = '\0';
}

/* ============================================================
 * 功能1：寄存器 UT 读写接口
 * ============================================================ */

/* 按寄存器编号（1~37） */
bool Reg_Write(UtSimulator *sim, int reg_id, RegValue val)
{
    if (reg_id < 1 || reg_id > REGISTER_COUNT)
        return false;
    int idx = reg_id - 1;
    if (!sim->reg_def[idx].writable)
        return false;
    sim->reg[idx] = val;
    return true;
}

bool Reg_Read(UtSimulator *sim, int reg_id, RegValue *p_val)
{
    if (reg_id < 1 || reg_id > REGISTER_COUNT)
        return false;
    int idx = reg_id - 1;
    if (!sim->reg_def[idx].readable)
        return false;
    *p_val = sim->reg[idx];
    return true;
}

/* 按命令码 */
bool Reg_WriteByCmd(UtSimulator *sim, uint8_t cmd, RegValue val)
{
    int8_t idx = sim->cmd_to_reg_write[cmd];
    if (idx < 0)
        return false;
    sim->reg[idx] = val;
    return true;
}

bool Reg_ReadByCmd(UtSimulator *sim, uint8_t cmd, RegValue *p_val)
{
    int8_t idx = sim->cmd_to_reg_read[cmd];
    if (idx < 0)
        return false;
    *p_val = sim->reg[idx];
    return true;
}

/* ============================================================
 * 功能2：报文解析与回复
 * ============================================================ */

/*
 * 报文格式（16 字符）："*62aabbbbbbbbcc^"
 *
 *  0: '*'
 *  1: '6'  -> 0x62 高位
 *  2: '2'  -> 0x62 低位
 *  3: 'a'  -> aa 命令码高位
 *  4: 'a'  -> aa 命令码低位
 *  5~12:    8 位十六进制值
 * 13: 'c'  -> cc 校验码高位
 * 14: 'c'  -> cc 校验码低位
 * 15: '^'
 *
 * 校验范围：索引 1~14（含设备地址、命令码、8hex值、校验码），
 *           即 "62aabbbbbbbbcc"（14字节），模128
 */

bool Parse_Message(UtSimulator *sim, uint8_t *p_cmd, RegValue *p_value)
{
    const char *buf = sim->rx_buf;

    /* 基本长度校验 */
    if (strlen(buf) != MSG_TOTAL_LEN)
        return false;
    if (buf[0] != MSG_START || buf[MSG_TOTAL_LEN - 1] != MSG_END)
        return false;

    /* 固定设备地址 "62" */
    if (buf[1] != '6' || buf[2] != '2')
        return false;

    /* 起始符和结束符已在上面覆盖 */

    /* 解析命令码 aa */
    if (!is_valid_hex_char(buf[3]) || !is_valid_hex_char(buf[4]))
        return false;
    uint8_t cmd = (hex_char_to_nibble(buf[3]) << 4) | hex_char_to_nibble(buf[4]);
    *p_cmd = cmd;

    /* 解析 8 位十六进制值 bbbbbbbb（索引 5~12） */
    char hex_val_str[9];
    memcpy(hex_val_str, &buf[5], 8);
    hex_val_str[8] = '\0';
    if (!hex_to_reg_value(hex_val_str, p_value))
        return false;

    /* 校验码验证（索引 1~12 参与校验：不含 cc 自身） */
    uint8_t expected_sum = calc_checksum(buf, 1, 12);
    uint8_t provided_sum = (hex_char_to_nibble(buf[13]) << 4) | hex_char_to_nibble(buf[14]);
    if (expected_sum != provided_sum)
        return false;

    return true;
}

void Build_Response(UtSimulator *sim, uint8_t cmd, RegValue value)
{
    (void)cmd;  /* 回复格式 "*bbbbbbbbcc^" 不含命令码，保留接口对称性 */
    char *buf = sim->tx_buf;

    /*
     * 回复报文 "*bbbbbbbbcc^"（12字符）：
     *   索引  0: '*'
     *   索引  1~8:  8位hex值
     *   索引  9~10: cc 校验码
     *   索引 11:   '^'
     *   校验范围：索引 1~8（仅 "bbbbbbbb"），模128
     */
    static const char hex_chars[] = "0123456789ABCDEF";

    buf[0] = MSG_START;

    /* bbbbbbbb：8 位十六进制值 */
    char hex_val[9];
    reg_value_to_hex(value, hex_val);
    memcpy(&buf[1], hex_val, 8);

    /* 先用占位符填充 cc，再计算校验和（基于 1~8，不含 cc） */
    buf[9]  = '0';
    buf[10] = '0';
    uint8_t cs = calc_checksum(buf, 1, 8);

    /* 格式化校验码为两个 ASCII 字符 */
    buf[9]  = hex_chars[(cs >> 4) & 0xF];
    buf[10] = hex_chars[cs & 0xF];

    buf[11] = MSG_END;
    buf[12] = '\0';
}

/**
 * Sim_Send - 将上位机报文送入模拟器，触发解析、寄存器操作、回复填充
 *
 * 处理逻辑：
 *   1. 复制到 rx_buf
 *   2. 解析报文（校验码 + 格式）
 *   3. 判断是写操作（cmd == write_cmd）还是读操作（cmd == read_cmd）
 *      - 写：写入对应寄存器，返回写入值
 *      - 读：读取寄存器当前值返回
 *      - 未知命令/不可写/不可读：返回全 0 或特定错误标记
 *   4. 构造回复写入 tx_buf
 */
bool Sim_Send(UtSimulator *sim, const char *msg)
{
    /* 复制上位机报文到 rx_buf */
    size_t len = strlen(msg);
    if (len >= sizeof(sim->rx_buf))
        return false;
    strcpy(sim->rx_buf, msg);

    /* 解析 */
    uint8_t cmd;
    RegValue value;
    if (!Parse_Message(sim, &cmd, &value)) {
        /* 校验失败，返回错误回复（全 0） */
        Build_Response(sim, cmd, 0);
        return false;
    }

    /* 通过命令码查找寄存器 */
    int8_t w_idx = sim->cmd_to_reg_write[cmd];
    int8_t r_idx = sim->cmd_to_reg_read[cmd];

    RegValue ret_value = 0;

    if (w_idx >= 0 && sim->reg_def[w_idx].writable) {
        /* 写操作 */
        sim->reg[w_idx] = value;
        ret_value = value;
    } else if (r_idx >= 0 && sim->reg_def[r_idx].readable) {
        /* 读操作 */
        ret_value = sim->reg[r_idx];
    } else {
        /* 命令码无法识别或寄存器不可访问 */
        ret_value = 0;
    }

    Build_Response(sim, cmd, ret_value);
    return true;
}

const char *Sim_GetTxBuf(const UtSimulator *sim) { return sim->tx_buf; }
const char *Sim_GetRxBuf(const UtSimulator *sim) { return sim->rx_buf; }
