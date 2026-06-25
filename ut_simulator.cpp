#include "ut_simulator.hpp"
#include <cstring>

/* ============================================================
 * 寄存器定义表（使用 0xFFu 确保 unsigned char 类型）
 * ============================================================ */

const RegDef UtSimulator::REG_DEF_TABLE[REGISTER_COUNT] = {
    /* Reg 1  */ { 0xFFu, 0x01u, false, true  },   // NA-01
    /* Reg 2  */ { 0xFFu, 0x03u, false, true  },   // NA-03
    /* Reg 3  */ { 0xFFu, 0x02u, false, true  },   // NA-02
    /* Reg 4  */ { 0xFFu, 0x05u, false, true  },   // NA-05
    /* Reg 5  */ { 0xFFu, 0x06u, false, true  },   // NA-06
    /* Reg 6  */ { 0xFFu, 0x07u, false, true  },   // NA-07
    /* Reg 7  */ { 0x28u, 0x41u, true,  true  },   // 28-41
    /* Reg 8  */ { 0x29u, 0x42u, true,  true  },   // 29-42
    /* Reg 9  */ { 0x2Au, 0x43u, true,  true  },   // 2a-43
    /* Reg 10 */ { 0x2Bu, 0x44u, true,  true  },   // 2b-44
    /* Reg 11 */ { 0x2Cu, 0x45u, true,  true  },   // 2c-45
    /* Reg 12 */ { 0x3Du, 0x46u, true,  true  },   // 3d-46
    /* Reg 13 */ { 0x2Eu, 0x47u, true,  true  },   // 2e-47
    /* Reg 14 */ { 0x1Cu, 0x50u, true,  true  },   // 1c-50
    /* Reg 15 */ { 0x1Du, 0x51u, true,  true  },   // 1d-51
    /* Reg 16 */ { 0x1Eu, 0x52u, true,  true  },   // 1e-52
    /* Reg 17 */ { 0x1Fu, 0x53u, true,  true  },   // 1f-53
    /* Reg 18 */ { 0x20u, 0x54u, true,  true  },   // 20-54
    /* Reg 19 */ { 0x21u, 0x55u, true,  true  },   // 21-55
    /* Reg 20 */ { 0x22u, 0x56u, true,  true  },   // 22-56
    /* Reg 21 */ { 0x23u, 0x57u, true,  true  },   // 23-57
    /* Reg 22 */ { 0x24u, 0x58u, true,  true  },   // 24-58
    /* Reg 23 */ { 0x25u, 0x59u, true,  true  },   // 25-59
    /* Reg 24 */ { 0x26u, 0x5Au, true,  true  },   // 26-5a
    /* Reg 25 */ { 0x27u, 0x5Bu, true,  true  },   // 27-5b
    /* Reg 26 */ { 0x0Cu, 0x5Cu, true,  true  },   // 0c-5c
    /* Reg 27 */ { 0x0Du, 0x5Du, true,  true  },   // 0d-5d
    /* Reg 28 */ { 0x0Eu, 0x5Eu, true,  true  },   // 0e-5e
    /* Reg 29 */ { 0x2Fu, 0x48u, true,  true  },   // 2f-48
    /* Reg 30 */ { 0x30u, 0x49u, true,  true  },   // 30-49
    /* Reg 31 */ { 0x33u, 0xFFu, true,  false },   // 33-NA
    /* Reg 32 */ { 0x31u, 0x4Au, true,  true  },   // 31-4a
    /* Reg 33 */ { 0x32u, 0x4Bu, true,  true  },   // 32-4b
    /* Reg 34 */ { 0x34u, 0x4Cu, true,  true  },   // 34-4c
    /* Reg 35 */ { 0x35u, 0x4Du, true,  true  },   // 35-4d
    /* Reg 36 */ { 0x0Fu, 0x5Fu, true,  true  },   // 0f-5f
    /* Reg 37 */ { 0x36u, 0x4Eu, true,  true  },   // 36-4e
};

/* ============================================================
 * 工具函数（private static）
 * ============================================================ */

bool UtSimulator::is_valid_hex_char(char c) noexcept
{
    return (c >= '0' && c <= '9') ||
           (c >= 'A' && c <= 'F') ||
           (c >= 'a' && c <= 'f');
}

bool UtSimulator::hex_char_to_nibble(char c, uint8_t &nibble) noexcept
{
    if (c >= '0' && c <= '9') { nibble = static_cast<uint8_t>(c - '0');       return true; }
    if (c >= 'A' && c <= 'F') { nibble = static_cast<uint8_t>(c - 'A' + 10);  return true; }
    if (c >= 'a' && c <= 'f') { nibble = static_cast<uint8_t>(c - 'a' + 10);  return true; }
    return false;
}

char UtSimulator::nibble_to_hex(uint8_t nibble) noexcept
{
    static constexpr char HEX_CHARS[] = "0123456789ABCDEF";
    return HEX_CHARS[nibble & 0xF];
}

uint8_t UtSimulator::calc_checksum(const char *buf, size_t start, size_t end) noexcept
{
    uint16_t sum = 0;
    for (size_t i = start; i <= end; ++i)
        sum += static_cast<uint8_t>(buf[i]);
    return static_cast<uint8_t>(sum & 0x7F);  // modulo 128
}

/* ============================================================
 * 构造 & 复位
 * ============================================================ */

UtSimulator::UtSimulator()
{
    std::memset(cmd_to_reg_write_, -1, sizeof(cmd_to_reg_write_));
    std::memset(cmd_to_reg_read_,  -1, sizeof(cmd_to_reg_read_));

    for (size_t i = 0; i < REGISTER_COUNT; ++i) {
        uint8_t wcmd = REG_DEF_TABLE[i].write_cmd;
        uint8_t rcmd = REG_DEF_TABLE[i].read_cmd;
        if (wcmd != CMD_NA) cmd_to_reg_write_[wcmd] = static_cast<int8_t>(i);
        if (rcmd != CMD_NA) cmd_to_reg_read_[rcmd]  = static_cast<int8_t>(i);
    }

    reset();
}

void UtSimulator::reset()
{
    std::memset(regs_, 0, sizeof(regs_));
    tx_buf_.clear();
    rx_buf_.clear();
}

/* ============================================================
 * 寄存器 UT 读写接口（属性注入）
 * ============================================================ */

bool UtSimulator::reg_write(int reg_id, RegValue val)
{
    if (reg_id < 1 || static_cast<size_t>(reg_id) > REGISTER_COUNT)
        return false;
    size_t idx = static_cast<size_t>(reg_id - 1);
    if (!REG_DEF_TABLE[idx].writable)
        return false;
    regs_[idx] = val;
    return true;
}

bool UtSimulator::reg_read(int reg_id, RegValue *p_val) const
{
    if (reg_id < 1 || static_cast<size_t>(reg_id) > REGISTER_COUNT)
        return false;
    size_t idx = static_cast<size_t>(reg_id - 1);
    if (!REG_DEF_TABLE[idx].readable)
        return false;
    *p_val = regs_[idx];
    return true;
}

bool UtSimulator::reg_write_by_cmd(uint8_t cmd, RegValue val)
{
    int8_t idx = cmd_to_reg_write_[cmd];
    if (idx < 0)
        return false;
    regs_[static_cast<size_t>(idx)] = val;
    return true;
}

bool UtSimulator::reg_read_by_cmd(uint8_t cmd, RegValue *p_val) const
{
    int8_t idx = cmd_to_reg_read_[cmd];
    if (idx < 0)
        return false;
    *p_val = regs_[static_cast<size_t>(idx)];
    return true;
}

/* ============================================================
 * 报文解析（private）
 *
 * 报文格式（16字符）："*62aabbbbbbbbcc^"
 *   索引  0:  '*'
 *   索引  1~2: "62" 设备地址
 *   索引  3~4: "aa" 命令码
 *   索引  5~12: 8位十六进制值
 *   索引 13~14: "cc" 校验码
 *   索引 15:  '^'
 *
 * 校验范围：索引 1~12（不含 '*' '^' 和 cc 自身）
 * ============================================================ */

bool UtSimulator::parse_message(uint8_t &cmd, RegValue &value) const
{
    const std::string &buf = rx_buf_;

    if (buf.size() != MSG_TOTAL_LEN)
        return false;
    if (buf[0] != MSG_START || buf[MSG_TOTAL_LEN - 1] != MSG_END)
        return false;

    // 固定设备地址 "62"
    if (buf[1] != '6' || buf[2] != '2')
        return false;

    // 解析命令码 aa（索引 3~4）
    uint8_t cmd_hi, cmd_lo;
    if (!hex_char_to_nibble(buf[3], cmd_hi) || !hex_char_to_nibble(buf[4], cmd_lo))
        return false;
    cmd = static_cast<uint8_t>((cmd_hi << 4) | cmd_lo);

    // 解析 8 位十六进制值（索引 5~12）
    value = 0;
    for (size_t i = 5; i <= 12; ++i) {
        if (!is_valid_hex_char(buf[i]))
            return false;
        uint8_t nibble;
        hex_char_to_nibble(buf[i], nibble);
        value = (value << 4) | nibble;
    }

    // 校验码验证（索引 1~12，不含 cc 自身）
    uint8_t expected = calc_checksum(buf.c_str(), 1, 12);
    uint8_t nib_hi = 0, nib_lo = 0;
    hex_char_to_nibble(buf[13], nib_hi);
    hex_char_to_nibble(buf[14], nib_lo);
    uint8_t provided = static_cast<uint8_t>((nib_hi << 4) | nib_lo);
    if (expected != provided)
        return false;

    return true;
}

/* ============================================================
 * 回复报文构造（private）
 *
 * 回复格式（12字符）："*bbbbbbbbcc^"
 *   索引  0: '*'
 *   索引  1~8: 8位十六进制值
 *   索引  9~10: "cc" 校验码
 *   索引 11: '^'
 *
 * 校验范围：索引 1~8（仅 "bbbbbbbb"），模128
 * ============================================================ */

void UtSimulator::build_response(RegValue value)
{
    std::string resp;
    resp.reserve(MSG_REPLY_LEN);

    resp += MSG_START;

    // bbbbbbbb
    char hex_val[9];
    for (int i = 7; i >= 0; --i) {
        hex_val[i] = nibble_to_hex(static_cast<uint8_t>(value & 0xF));
        value >>= 4;
    }
    hex_val[8] = '\0';
    resp.append(hex_val, 8);

    // 先用占位符填 cc，再计算校验和（基于索引 1~8）
    resp += '0';
    resp += '0';
    uint8_t cs = calc_checksum(resp.c_str(), 1, 8);

    // 替换 cc 为真实校验码
    resp[9]  = nibble_to_hex(static_cast<uint8_t>((cs >> 4) & 0xF));
    resp[10] = nibble_to_hex(static_cast<uint8_t>(cs & 0xF));
    resp += MSG_END;

    tx_buf_ = std::move(resp);
}

/* ============================================================
 * Sim_Send - 统一入口
 *
 * 接收上位机报文 → 解析 → 读/写寄存器 → 回复
 * ============================================================ */

bool UtSimulator::send(const std::string &msg)
{
    if (msg.size() >= 1024)
        return false;
    rx_buf_ = msg;

    uint8_t cmd;
    RegValue value;
    if (!parse_message(cmd, value)) {
        build_response(0);
        return false;
    }

    int8_t w_idx = cmd_to_reg_write_[cmd];
    int8_t r_idx = cmd_to_reg_read_[cmd];

    RegValue ret_value = 0;

    if (w_idx >= 0 && REG_DEF_TABLE[static_cast<size_t>(w_idx)].writable) {
        regs_[static_cast<size_t>(w_idx)] = value;
        ret_value = value;
    } else if (r_idx >= 0 && REG_DEF_TABLE[static_cast<size_t>(r_idx)].readable) {
        ret_value = regs_[static_cast<size_t>(r_idx)];
    } else {
        ret_value = 0;
    }

    build_response(ret_value);
    return true;
}
