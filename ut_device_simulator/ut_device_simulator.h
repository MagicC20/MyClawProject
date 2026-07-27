#ifndef UT_DEVICE_SIMULATOR_H
#define UT_DEVICE_SIMULATOR_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/*===========================================================================
 * 常量定义
 *==========================================================================*/

/** 寄存器数量 */
#define REG_COUNT_MAX  37

/** 报文缓冲区大小 */
#define RX_BUF_SIZE    64
#define TX_BUF_SIZE    64

/** 固定设备地址 */
#define DEVICE_ADDR    0x62

/** 报文特殊字符 */
#define MSG_START      '*'
#define MSG_END        '^'

/*===========================================================================
 * 寄存器定义
 *==========================================================================*/

/**
 * @brief 寄存器信息结构
 * @note  读写命令为ASCII字符形式，如0x03对应字符'03'
 */
typedef struct {
    uint8_t  reg_id;          /* 寄存器编号 1~37 */
    uint8_t  write_cmd;       /* 写命令字，0xFF表示不可写 */
    uint8_t  read_cmd;        /* 读命令字，0xFF表示不可读 */
    uint32_t value;           /* 寄存器当前值 */
    bool     readable;         /* 是否可读 */
    bool     writable;        /* 是否可写 */
} RegInfo_t;

/*===========================================================================
 * 全局变量（模拟器实例，对外不体现实例概念）
 *==========================================================================*/

/** 接收缓冲区，上位机下发报文存放于此 */
extern char g_rx_buf[RX_BUF_SIZE];

/** 发送缓冲区，模拟器回复报文存放于此 */
extern char g_tx_buf[TX_BUF_SIZE];

/** 接收缓冲区有效数据长度 */
extern uint32_t g_rx_len;

/** 发送缓冲区有效数据长度 */
extern uint32_t g_tx_len;

/*===========================================================================
 * 模拟器接口（供UT调用）
 *==========================================================================*/

/**
 * @brief 初始化模拟器
 * @note  需在UT测试开始时调用
 */
void Simulator_Init(void);

/**
 * @brief 解析上位机下发的报文
 * @note  解析完成后将回复报文写入g_tx_buf
 * @retval 0  解析成功
 * @retval -1 格式错误
 * @retval -2 校验失败
 * @retval -3 未知命令
 * @retval -4 只读寄存器不支持写
 * @retval -5 只写寄存器不支持读
 */
int Simulator_ParseRx(void);

/**
 * @brief 写入指定寄存器的值（UT属性注入接口）
 * @param reg_id  寄存器编号 1~37
 * @param value   写入的值
 * @retval 0   成功
 * @retval -1  无效寄存器编号
 * @retval -2  寄存器不可写
 */
int Simulator_SetReg(uint8_t reg_id, uint32_t value);

/**
 * @brief 读取指定寄存器的值（UT属性注入接口）
 * @param reg_id  寄存器编号 1~37
 * @param p_value 输出参数，读取到的值
 * @retval 0   成功
 * @retval -1  无效寄存器编号
 * @retval -2  寄存器不可读
 */
int Simulator_GetReg(uint8_t reg_id, uint32_t *p_value);

/**
 * @brief 设置上位机下发的原始报文
 * @param data  报文数据
 * @param len   报文长度
 * @note  调用Simulator_ParseRx之前需先调用本接口填充g_rx_buf
 */
void Simulator_SetRx(const char *data, uint32_t len);

/**
 * @brief 获取模拟器回复报文
 * @param p_len 输出参数，回复报文长度
 * @return 指向回复缓冲区的指针
 */
const char* Simulator_GetTx(uint32_t *p_len);

/**
 * @brief 计算校验码
 * @param data  报文字符串（不含首尾*和^）
 * @param len   长度
 * @return 校验码（0x00~0x7F）
 */
uint8_t Calc_Checksum(const char *data, uint32_t len);

#ifdef __cplusplus
}
#endif

#endif /* UT_DEVICE_SIMULATOR_H */
