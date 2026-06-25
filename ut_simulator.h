#ifndef UT_SIMULATOR_H
#define UT_SIMULATOR_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

/* ============================================================
 * 常量定义
 * ============================================================ */

#define DEVICE_ADDR          0x62
#define REGISTER_COUNT        37
/*
 * 报文实际字符数：
 *   "*62aabbbbbbbbcc^" = 1+2+2+8+2+1 = 16 字符
 *   回复 "*bbbbbbbbcc^"  = 1+8+2+1   = 12 字符
 */
#define MSG_TOTAL_LEN        16   /* "*62aabbbbbbbbcc^" 实际长度 */
#define MSG_REPLY_LEN         12   /* "*bbbbbbbbcc^" 实际长度 */

#define MSG_START            '*'
#define MSG_END              '^'

/* 8 字节寄存器值（无符号 32 位，存 0x00000000 ~ 0xFFFFFFFF） */
typedef uint32_t RegValue;
#define REG_VALUE_ZERO       ((RegValue)0)

/* 不可读/不可写标记 */
#define CMD_NA               0xFF

/* ============================================================
 * 寄存器描述
 * ============================================================ */

/* 每个寄存器的读/写命令码；0xFF = NA（不可读/不可写） */
typedef struct {
    uint8_t write_cmd;   /* 上位机发送写指令时使用的命令码 */
    uint8_t read_cmd;    /* 上位机发送读指令时使用的命令码 */
    bool    writable;
    bool    readable;
} RegDef;

/* 模拟器主体 */
typedef struct {
    RegValue reg[REGISTER_COUNT];          /* 寄存器当前值 */
    RegDef   reg_def[REGISTER_COUNT];       /* 寄存器定义（读/写命令码） */

    /* 按命令码索引寄存器的辅助表（用于报文解析）
     * cmd_to_reg_write[cmd] = reg_index (0-based, or -1)
     * cmd_to_reg_read[cmd]  = reg_index (0-based, or -1) */
    int8_t   cmd_to_reg_write[256];
    int8_t   cmd_to_reg_read[256];

    /* 发送/接收缓冲区（供 UT 回调代码写入/读取） */
    char     tx_buf[MSG_TOTAL_LEN + 1];    /* 模拟器 -> 上位机 */
    char     rx_buf[MSG_TOTAL_LEN + 1];    /* 上位机 -> 模拟器 */
} UtSimulator;

/* ============================================================
 * 初始化 & 复位
 * ============================================================ */
void Sim_Init(UtSimulator *sim);
void Sim_Reset(UtSimulator *sim);

/* ============================================================
 * 功能1：寄存器 UT 读写接口（属性注入）
 * ============================================================ */
/* 按寄存器编号（1~37）访问 */
bool Reg_Write(UtSimulator *sim, int reg_id /* 1~37 */, RegValue val);
bool Reg_Read(UtSimulator *sim, int reg_id /* 1~37 */, RegValue *p_val);

/* 按命令码访问（供内部或 UT 直接用命令码操作） */
bool Reg_WriteByCmd(UtSimulator *sim, uint8_t cmd, RegValue val);
bool Reg_ReadByCmd(UtSimulator *sim, uint8_t cmd, RegValue *p_val);

/* ============================================================
 * 功能2：报文解析与回复
 * ============================================================ */

/**
 * Parse_Message - 解析上位机下发报文
 * @sim:     模拟器句柄
 * @p_cmd:   解析出的命令码（aa）输出
 * @p_value: 解析出的 8 位十六进制值输出
 * @return:  true=校验通过，false=校验失败或格式错误
 *
 * 报文格式（ASCII）："*62aabbbbbbbbcc^"
 *   *      起始符
 *   62     设备地址（固定）
 *   aa     命令字
 *   bbbbbbbb  8 位十六进制值（读时全 0，写时为写入值）
 *   cc     校验码（计算 *~^ 之间所有字符之和 % 128，格式化为两字节 ASCII hex）
 *   ^      结束符
 */
bool Parse_Message(UtSimulator *sim, uint8_t *p_cmd, RegValue *p_value);

/**
 * Build_Response - 构造模拟器回复报文
 * @sim:    模拟器句柄
 * @cmd:    命令码（aa）
 * @value:  要返回的寄存器值（8 位十六进制）
 *
 * 回复格式（ASCII）："*bbbbbbbbcc^"
 */
void Build_Response(UtSimulator *sim, uint8_t cmd, RegValue value);

/**
 * Sim_Send - UT 代码调用此接口将上位机报文送入模拟器
 *           内部自动解析、写寄存器（或读寄存器）并填充回复缓冲区
 * @sim:   模拟器句柄
 * @msg:   上位机原始报文（ASCII 字符串，以 '\0' 结尾）
 * @return: true=处理成功（校验通过），false=失败
 *
 * 处理完成后，回复报文在 sim->tx_buf 中，UT 代码自行取走发送。
 */
bool Sim_Send(UtSimulator *sim, const char *msg);

/**
 * Sim_GetTxBuf / Sim_GetRxBuf - 获取收发缓冲区指针
 */
const char *Sim_GetTxBuf(const UtSimulator *sim);
const char *Sim_GetRxBuf(const UtSimulator *sim);

#endif /* UT_SIMULATOR_H */
