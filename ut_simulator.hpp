#ifndef UT_SIMULATOR_HPP
#define UT_SIMULATOR_HPP

#include <cstdint>
#include <string>

/* ============================================================
 * 常量定义
 * ============================================================ */

static constexpr uint8_t  DEVICE_ADDR    = 0x62;
static constexpr size_t    REGISTER_COUNT = 37;
static constexpr uint8_t  CMD_NA         = 0xFF;   // 不可读/不可写标记

static constexpr char MSG_START = '*';
static constexpr char MSG_END   = '^';

/*
 * 报文长度：
 *   上位机下发 "*62aabbbbbbbbcc^" = 16 字符
 *   模拟器回复 "*bbbbbbbbcc^"     = 12 字符
 */
static constexpr size_t MSG_TOTAL_LEN = 16;
static constexpr size_t MSG_REPLY_LEN = 12;

/* ============================================================
 * 寄存器定义
 * ============================================================ */

struct RegDef {
    uint8_t write_cmd;
    uint8_t read_cmd;
    bool    writable;
    bool    readable;
};

/* ============================================================
 * 模拟器类
 * ============================================================ */

class UtSimulator {
public:
    using RegValue = uint32_t;

    /* ------------------------------------------------------
     * 构造 & 复位
     * ------------------------------------------------------ */
    UtSimulator();
    void reset();

    /* ------------------------------------------------------
     * 功能1：寄存器 UT 读写接口（属性注入）
     * ------------------------------------------------------ */
    bool reg_write(int reg_id, RegValue val);        // reg_id: 1~37
    bool reg_read (int reg_id, RegValue *p_val) const;

    bool reg_write_by_cmd(uint8_t cmd, RegValue val);
    bool reg_read_by_cmd (uint8_t cmd, RegValue *p_val) const;

    /* ------------------------------------------------------
     * 功能2：报文解析与回复
     *
     * Sim_Send() 是统一入口：
     *   接收上位机原始报文（ASCII 字符串），
     *   自动完成解析、寄存器读/写、回复报文填充。
     *   返回 true=处理成功（校验通过），false=失败。
     * ------------------------------------------------------ */
    bool send(const std::string &msg);

    /* 收发缓冲区 */
    const std::string &tx_buf() const { return tx_buf_; }
    const std::string &rx_buf() const { return rx_buf_; }

    const char *tx_cstr() const { return tx_buf_.c_str(); }
    const char *rx_cstr() const { return rx_buf_.c_str(); }

    /* ------------------------------------------------------
     * 寄存器定义查询（供 UT 测试用）
     * ------------------------------------------------------ */
    static const RegDef &reg_def(size_t reg_index) {  // reg_index: 0~36
        return REG_DEF_TABLE[reg_index];
    }
    static constexpr size_t reg_count() { return REGISTER_COUNT; }

private:
    RegValue regs_[REGISTER_COUNT];

    /* 寄存器定义表 */
    static const RegDef REG_DEF_TABLE[REGISTER_COUNT];

    /* 按命令码索引：cmd_to_reg_write[cmd] = reg_index(0~36) or -1 */
    int8_t cmd_to_reg_write_[256];
    int8_t cmd_to_reg_read_[256];

    /* 收发缓冲区 */
    std::string tx_buf_;
    std::string rx_buf_;

    /* 内部工具 */
    static bool hex_char_to_nibble(char c, uint8_t &nibble) noexcept;
    static char nibble_to_hex(uint8_t nibble) noexcept;
    static bool is_valid_hex_char(char c) noexcept;
    static uint8_t calc_checksum(const char *buf, size_t start, size_t end) noexcept;

    bool parse_message(uint8_t &cmd, RegValue &value) const;
    void build_response(RegValue value);
};

#endif // UT_SIMULATOR_HPP
