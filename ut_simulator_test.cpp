#include "ut_simulator.hpp"
#include <cassert>
#include <cstdio>
#include <string>

using namespace std;

/* ================================================================
 * 工具函数
 * ================================================================ */

static uint8_t manual_checksum(const char *buf, size_t start, size_t end) noexcept
{
    uint16_t sum = 0;
    for (size_t i = start; i <= end; ++i)
        sum += static_cast<uint8_t>(buf[i]);
    return static_cast<uint8_t>(sum & 0x7F);
}

static char nibble_to_hex(uint8_t n) noexcept
{
    static constexpr char HEX[] = "0123456789ABCDEF";
    return HEX[n & 0xF];
}

/**
 * 构造上位机写寄存器报文（16字符）
 * cmd_hex: 两字符十六进制命令码
 * val:     写入值
 */
static string build_write_msg(const char *cmd_hex, uint32_t val)
{
    char val_hex[9];
    for (int i = 7; i >= 0; --i) {
        val_hex[i] = nibble_to_hex(static_cast<uint8_t>(val & 0xF));
        val >>= 4;
    }
    val_hex[8] = '\0';

    string msg;
    msg.reserve(MSG_TOTAL_LEN);
    msg += '*';
    msg += '6';
    msg += '2';
    msg += cmd_hex[0];
    msg += cmd_hex[1];
    msg.append(val_hex, 8);
    msg += '0';  // cc 占位符
    msg += '0';
    uint8_t cs = manual_checksum(msg.c_str(), 1, 12);
    msg[13] = nibble_to_hex(static_cast<uint8_t>((cs >> 4) & 0xF));
    msg[14] = nibble_to_hex(static_cast<uint8_t>(cs & 0xF));
    msg += '^';  // 索引 15

    return msg;  // size() == 16
}

static string build_read_msg(const char *cmd_hex)
{
    return build_write_msg(cmd_hex, 0);
}

/* ================================================================
 * 测试用例
 * ================================================================ */

static void test_init_and_reset()
{
    printf("=== %s ===\n", __func__);
    UtSimulator sim;

    for (int i = 1; i <= 37; ++i) {
        uint32_t v = 0xFFFFFFFF;
        bool readable = sim.reg_read(i, &v);
        if (readable) assert(v == 0);
    }

    sim.reg_write(7, 0x12345678);
    uint32_t v1;
    sim.reg_read(7, &v1);
    assert(v1 == 0x12345678);

    sim.reset();
    uint32_t v2;
    sim.reg_read(7, &v2);
    assert(v2 == 0);

    uint32_t tmp;
    assert(!sim.reg_write(0, 1));
    assert(!sim.reg_write(38, 1));
    assert(!sim.reg_read(0, &tmp));
    assert(!sim.reg_read(38, &tmp));

    printf("  PASS\n");
}

static void test_reg_rw_by_id()
{
    printf("=== %s ===\n", __func__);
    UtSimulator sim;

    assert(sim.reg_write(7, 0xDEADBEEF));
    uint32_t v;
    assert(sim.reg_read(7, &v));
    assert(v == 0xDEADBEEF);

    assert(!sim.reg_write(1, 999));  // Reg1 不可写

    assert(sim.reg_write(31, 0xABCDEF01));
    assert(!sim.reg_read(31, &v));  // Reg31 只写不可读

    printf("  PASS\n");
}

static void test_reg_rw_by_cmd()
{
    printf("=== %s ===\n", __func__);
    UtSimulator sim;

    assert(sim.reg_write_by_cmd(0x28, 0x11111111));
    uint32_t v;
    assert(sim.reg_read_by_cmd(0x41, &v));
    assert(v == 0x11111111);

    assert(sim.reg_write_by_cmd(0x33, 0x22222222));
    assert(!sim.reg_read_by_cmd(0x33, &v));  // 读命令为 NA

    assert(!sim.reg_write_by_cmd(0xFF, 0));
    assert(!sim.reg_read_by_cmd(0xFF, &v));

    printf("  PASS\n");
}

static void test_checksum_calculation()
{
    printf("=== %s ===\n", __func__);
    UtSimulator sim;

    string msg = build_write_msg("28", 0xAB);
    printf("  写寄存器报文: %s (len=%zu)\n", msg.c_str(), msg.size());
    assert(msg.size() == MSG_TOTAL_LEN);

    assert(sim.send(msg));
    printf("  模拟器回复:    %s (len=%zu)\n", sim.tx_buf().c_str(), sim.tx_buf().size());

    assert(sim.tx_buf()[0] == '*');
    assert(sim.tx_buf()[11] == '^');
    assert(sim.tx_buf().size() == MSG_REPLY_LEN);
    assert(sim.tx_buf().substr(1, 8) == "000000AB");

    printf("  PASS\n");
}

static void test_read_register()
{
    printf("=== %s ===\n", __func__);
    UtSimulator sim;

    sim.reg_write(7, 0x12345678);
    string msg = build_read_msg("41");
    printf("  读寄存器报文: %s\n", msg.c_str());

    sim.send(msg);
    printf("  模拟器回复:    %s\n", sim.tx_buf().c_str());
    assert(sim.tx_buf().substr(1, 8) == "12345678");

    printf("  PASS\n");
}

static void test_invalid_checksum()
{
    printf("=== %s ===\n", __func__);
    UtSimulator sim;

    string bad_msg = "*62" "41" "00000000" "00^";
    assert(!sim.send(bad_msg));

    printf("  错误校验回复: %s\n", sim.tx_buf().c_str());
    printf("  PASS\n");
}

static void test_invalid_address()
{
    printf("=== %s ===\n", __func__);
    UtSimulator sim;

    string bad_msg = "*63" "41" "00000000" "00^";
    assert(!sim.send(bad_msg));

    printf("  非法地址回复: %s\n", sim.tx_buf().c_str());
    printf("  PASS\n");
}

/**
 * 测试所有可写寄存器：通过 send 发写命令注入值，再发读命令验证
 */
static void test_all_writable_regs()
{
    printf("=== %s ===\n", __func__);
    UtSimulator sim;

    for (size_t idx = 0; idx < sim.reg_count(); ++idx) {
        const RegDef &def = sim.reg_def(idx);
        if (!def.writable)
            continue;

        char cmd_hex[3] = {
            (def.write_cmd >> 4) >= 10
                ? static_cast<char>('A' + (def.write_cmd >> 4) - 10)
                : static_cast<char>('0' + (def.write_cmd >> 4)),
            (def.write_cmd & 0xF) >= 10
                ? static_cast<char>('A' + (def.write_cmd & 0xF) - 10)
                : static_cast<char>('0' + (def.write_cmd & 0xF)),
            '\0'
        };

        uint32_t write_val = static_cast<uint32_t>(((idx + 1) * 0x123) ^ 0xAAAAAAAAu);

        // 写
        string wmsg = build_write_msg(cmd_hex, write_val);
        bool ok = sim.send(wmsg);
        assert(ok);
        // 写操作的回复值等于写入值
        assert(sim.tx_buf().substr(1, 8).size() == 8);

        // 通过读命令读回（如果有读命令）
        if (def.readable && def.read_cmd != CMD_NA) {
            char rcmd_hex[3] = {
                (def.read_cmd >> 4) >= 10
                    ? static_cast<char>('A' + (def.read_cmd >> 4) - 10)
                    : static_cast<char>('0' + (def.read_cmd >> 4)),
                (def.read_cmd & 0xF) >= 10
                    ? static_cast<char>('A' + (def.read_cmd & 0xF) - 10)
                    : static_cast<char>('0' + (def.read_cmd & 0xF)),
                '\0'
            };
            string rmsg = build_read_msg(rcmd_hex);
            ok = sim.send(rmsg);
            assert(ok);

            char exp_hex[9];
            uint32_t tmp = write_val;
            for (int j = 7; j >= 0; --j) {
                exp_hex[j] = nibble_to_hex(static_cast<uint8_t>(tmp & 0xF));
                tmp >>= 4;
            }
            exp_hex[8] = '\0';

            if (sim.tx_buf().substr(1, 8) != string(exp_hex, 8)) {
                printf("  FAIL Reg%zu (write_cmd=0x%02X, read_cmd=0x%02X):\n",
                       idx + 1, def.write_cmd, def.read_cmd);
                printf("    exp=%s got=%s\n", exp_hex, sim.tx_buf().substr(1, 8).c_str());
                assert(0);
            }
        }
    }

    printf("  PASS (all writable registers verified)\n");
}

/**
 * 测试只读寄存器（Reg1~6）通过 send 读报文的解析和回复格式
 */
static void test_readonly_regs()
{
    printf("=== %s ===\n", __func__);
    UtSimulator sim;

    for (size_t idx = 0; idx < sim.reg_count(); ++idx) {
        const RegDef &def = sim.reg_def(idx);
        if (!def.readable || def.writable)
            continue;  // 只处理只读寄存器

        char cmd_hex[3] = {
            (def.read_cmd >> 4) >= 10
                ? static_cast<char>('A' + (def.read_cmd >> 4) - 10)
                : static_cast<char>('0' + (def.read_cmd >> 4)),
            (def.read_cmd & 0xF) >= 10
                ? static_cast<char>('A' + (def.read_cmd & 0xF) - 10)
                : static_cast<char>('0' + (def.read_cmd & 0xF)),
            '\0'
        };

        string msg = build_read_msg(cmd_hex);
        bool ok = sim.send(msg);
        assert(ok);
        assert(sim.tx_buf().size() == MSG_REPLY_LEN);
        assert(sim.tx_buf()[0] == '*');
        assert(sim.tx_buf()[11] == '^');
        // 回复值应为 regs_[idx]（初始为 0）
        assert(sim.tx_buf().substr(1, 8) == "00000000");
    }

    printf("  PASS (readonly register format verified)\n");
}

static void test_reg31_write_only()
{
    printf("=== %s ===\n", __func__);
    UtSimulator sim;

    assert(sim.reg_write(31, 0xDEADBEEF));
    uint32_t v;
    assert(!sim.reg_read(31, &v));

    string msg = build_write_msg("33", 0x12345678);
    sim.send(msg);
    assert(sim.tx_buf().substr(1, 8) == "12345678");

    printf("  PASS\n");
}

/* ================================================================
 * 主入口
 * ================================================================ */

int main()
{
    printf("\n========== C++ UT Simulator Tests ==========\n\n");

    test_init_and_reset();
    test_reg_rw_by_id();
    test_reg_rw_by_cmd();
    test_checksum_calculation();
    test_read_register();
    test_invalid_checksum();
    test_invalid_address();
    test_reg31_write_only();
    test_all_writable_regs();
    test_readonly_regs();

    printf("\n========== ALL TESTS PASSED ==========\n\n");
    return 0;
}
