/*
 * ut_simulator_test.c
 * 单元测试入口
 *
 * 编译：
 *   gcc -std=c11 -Wall -Wextra -I. ut_simulator.c ut_simulator_test.c -o ut_simulator_test
 *
 * 运行：
 *   ./ut_simulator_test
 */

#include <stdio.h>
#include <string.h>
#include <assert.h>
#include "ut_simulator.h"

/* ------------------------------------------------------------
 * 工具：手动计算报文的校验码（用于验证模拟器结果）
 * 报文 "*62aabbbbbbbbcc^" 中 cc 的计算规则：
 *   将索引 1~12（不含首尾的 * 和 ^）的每个字符 ASCII 值相加，结果 % 128
 * ------------------------------------------------------------ */
static uint8_t manual_checksum(const char *msg, int start, int end)
{
    uint16_t sum = 0;
    for (int i = start; i <= end; i++) {
        sum += (uint8_t)msg[i];
    }
    return (uint8_t)(sum & 0x7F);
}

/*
 * 辅助：构造一条上位机写寄存器报文
 * cmd_hex: 两字符十六进制命令码字符串，如 "28"
 * val:     要写入的值
 * out:     输出报文缓冲区（至少 15 字节）
 */
/*
 * 报文格式（16字节打印字符 + 1 null = 17字节）：
 *
 * 索引:  0  1  2  3  4  5-12    13 14  15
 *       [*][6][2][a][a][bbbbbbbb][c][c][^]
 *
 * 校验范围（不含 cc 本身）：
 *   请求 "62aabbbbbbbbcc" → 索引 1~12，共 12 字节
 *   回复 "bbbbbbbbcc"     → 索引 1~10，共 10 字节
 */
static void build_write_msg(char *out, const char *cmd_hex, RegValue val)
{
    static const char hex_chars[] = "0123456789ABCDEF";
    char val_hex[9];
    for (int i = 7; i >= 0; i--) {
        val_hex[i] = hex_chars[val & 0xF];
        val >>= 4;
    }
    val_hex[8] = '\0';

    /* 组装消息骨架（cc 暂时用占位符 '0'） */
    char msg[17];
    msg[0] = '*';
    msg[1] = '6';
    msg[2] = '2';
    msg[3] = cmd_hex[0];
    msg[4] = cmd_hex[1];
    memcpy(&msg[5], val_hex, 8);
    msg[13] = '0';   /* cc 占位 */
    msg[14] = '0';
    msg[15] = '^';

    /* 校验范围：索引 1~12（不含 cc），模 128 */
    uint8_t cs = manual_checksum(msg, 1, 12);
    msg[13] = hex_chars[(cs >> 4) & 0xF];
    msg[14] = hex_chars[cs & 0xF];

    /* 复制 16 字节（0~15）+ null */
    memcpy(out, msg, 16);
    out[16] = '\0';
}

/*
 * 辅助：构造一条上位机读寄存器报文（bbbbbbbb 全 0）
 */
static void build_read_msg(char *out, const char *cmd_hex)
{
    build_write_msg(out, cmd_hex, 0);
}

/* ------------------------------------------------------------
 * 测试用例
 * ------------------------------------------------------------ */

static void test_init_and_reset(void)
{
    printf("=== %s ===\n", __func__);

    UtSimulator sim;
    Sim_Init(&sim);

    /* 验证所有寄存器初始为 0 */
    for (int i = 1; i <= REGISTER_COUNT; i++) {
        RegValue v;
        bool readable = sim.reg_def[i - 1].readable;
        bool ok = Reg_Read(&sim, i, &v);
        assert(ok == readable);  /* 可读性应与定义一致 */
        if (readable) {
            assert(v == 0);       /* 可读寄存器初始值应为 0 */
        }
    }

    /* 验证寄存器编号超出范围 */
    RegValue v;
    assert(!Reg_Read(&sim, 0, &v));
    assert(!Reg_Read(&sim, 38, &v));
    assert(!Reg_Write(&sim, 0, 100));
    assert(!Reg_Write(&sim, 38, 100));

    /* 复位后值应为 0 */
    Reg_Write(&sim, 7, 0x12345678);
    RegValue v1;
    Reg_Read(&sim, 7, &v1);
    assert(v1 == 0x12345678);

    Sim_Reset(&sim);
    RegValue v2;
    Reg_Read(&sim, 7, &v2);
    assert(v2 == 0);

    printf("  PASS\n");
}

static void test_reg_rw_by_id(void)
{
    printf("=== %s ===\n", __func__);

    UtSimulator sim;
    Sim_Init(&sim);

    /* Reg 7 (write=0x28, read=0x41) 可读写 */
    assert(Reg_Write(&sim, 7, 0xDEADBEEF));
    RegValue v;
    assert(Reg_Read(&sim, 7, &v));
    assert(v == 0xDEADBEEF);

    /* Reg 1 不可写 */
    assert(!Reg_Write(&sim, 1, 999));

    /* Reg 31 不可读（read_cmd=NA） */
    assert(Reg_Write(&sim, 31, 0xABCDEF01));
    assert(!Reg_Read(&sim, 31, &v));

    printf("  PASS\n");
}

static void test_reg_rw_by_cmd(void)
{
    printf("=== %s ===\n", __func__);

    UtSimulator sim;
    Sim_Init(&sim);

    /* 通过写命令码 0x28 写入 Reg7 */
    assert(Reg_WriteByCmd(&sim, 0x28, 0x11111111));
    RegValue v;
    assert(Reg_ReadByCmd(&sim, 0x41, &v));  /* 通过读命令码 0x41 读 Reg7 */
    assert(v == 0x11111111);

    /* 通过写命令码 0x33 写入 Reg31（只写不可读） */
    assert(Reg_WriteByCmd(&sim, 0x33, 0x22222222));
    assert(!Reg_ReadByCmd(&sim, 0x33, &v));  /* 读命令为 NA，应失败 */

    /* 无效命令码 */
    assert(!Reg_WriteByCmd(&sim, 0xFF, 0));
    assert(!Reg_ReadByCmd(&sim, 0xFF, &v));

    printf("  PASS\n");
}

static void test_checksum_calculation(void)
{
    printf("=== %s ===\n", __func__);

    /* 验证 *62 + cmd(2字节) + 8hex(8字节) = 1+2+8 = 11 字符参与校验 */
    /* "*62aabbbbbbbbcc^" 中校验范围是 buf[1]~buf[12]，即 "62aabbbbbbbb" */
    UtSimulator sim;
    Sim_Init(&sim);

    /* 构造报文：写 Reg7，命令 0x28，写入值 0x000000AB */
    char msg[17];
    build_write_msg(msg, "28", 0xAB);

    printf("  写寄存器报文: %s\n", msg);

    /* 复制到模拟器 */
    strcpy(sim.rx_buf, msg);
    uint8_t cmd;
    RegValue val;
    bool ok = Parse_Message(&sim, &cmd, &val);
    assert(ok);
    assert(cmd == 0x28);
    assert(val == 0xAB);

    /* 触发写操作并获取回复 */
    bool send_ok = Sim_Send(&sim, msg);
    assert(send_ok);
    printf("  模拟器回复:    %s\n", sim.tx_buf);

    /* 验证回复报文格式 "*bbbbbbbbcc^"（12字符，^ 在索引11） */
    assert(sim.tx_buf[0] == '*');
    assert(sim.tx_buf[11] == '^');
    /* 验证回复值等于写入值 */
    char reply_val_hex[9];
    memcpy(reply_val_hex, &sim.tx_buf[1], 8);
    reply_val_hex[8] = '\0';
    printf("  回复值 hex:    %s\n", reply_val_hex);
    assert(memcmp(reply_val_hex, "000000AB", 8) == 0);

    printf("  PASS\n");
}

static void test_read_register(void)
{
    printf("=== %s ===\n", __func__);

    UtSimulator sim;
    Sim_Init(&sim);

    /* 先写入 Reg7 一个值 */
    Reg_Write(&sim, 7, 0x12345678);

    /* 构造读报文：读 Reg7，读命令码 0x41 */
    char msg[17];
    build_read_msg(msg, "41");
    printf("  读寄存器报文: %s\n", msg);

    Sim_Send(&sim, msg);
    printf("  模拟器回复:    %s\n", sim.tx_buf);

    /* 回复值应为之前写入的 0x12345678 */
    char reply_val_hex[9];
    memcpy(reply_val_hex, &sim.tx_buf[1], 8);
    reply_val_hex[8] = '\0';
    assert(memcmp(reply_val_hex, "12345678", 8) == 0);

    printf("  PASS\n");
}

static void test_invalid_checksum(void)
{
    printf("=== %s ===\n", __func__);

    UtSimulator sim;
    Sim_Init(&sim);

    /* 伪造一个校验码错误的报文 */
    char bad_msg[17] = "*62" "41" "00000000" "00^";
    bool ok = Sim_Send(&sim, bad_msg);
    assert(!ok);  /* 校验失败应返回 false */
    /* 模拟器仍会构造一个回复（全0） */
    printf("  错误校验测试回复: %s\n", sim.tx_buf);

    printf("  PASS\n");
}

static void test_invalid_address(void)
{
    printf("=== %s ===\n", __func__);

    UtSimulator sim;
    Sim_Init(&sim);

    /* 设备地址不是 62 的报文 */
    char bad_msg[17] = "*63" "41" "00000000" "00^";
    bool ok = Sim_Send(&sim, bad_msg);
    assert(!ok);

    printf("  非法地址测试回复: %s\n", sim.tx_buf);
    printf("  PASS\n");
}

static void test_all_readable_regs(void)
{
    printf("=== %s ===\n", __func__);

    UtSimulator sim;
    Sim_Init(&sim);

    /* 遍历所有可读寄存器，注入值后读回验证 */
    for (int i = 1; i <= REGISTER_COUNT; i++) {
        if (!sim.reg_def[i - 1].readable)
            continue;

        uint8_t rcmd = sim.reg_def[i - 1].read_cmd;
        char cmd_hex[3] = {
            (rcmd >> 4) >= 10 ? ('A' + (rcmd >> 4) - 10) : ('0' + (rcmd >> 4)),
            (rcmd & 0xF) >= 10 ? ('A' + (rcmd & 0xF) - 10) : ('0' + (rcmd & 0xF)),
            '\0'
        };

        /* 向寄存器注入测试值（可直接写入，无需关心 writable 标志） */
        RegValue write_val = (RegValue)((i * 0x111) & 0xFFFFFFFF);
        sim.reg[i - 1] = write_val;

        /* 构造读报文 */
        char msg[17];
        build_read_msg(msg, cmd_hex);

        Sim_Send(&sim, msg);

        char reply_val_hex[9];
        memcpy(reply_val_hex, &sim.tx_buf[1], 8);
        reply_val_hex[8] = '\0';

        /* 回复值应为写入值 */
        char exp_hex[9];
        static const char h2c[] = "0123456789ABCDEF";
        for (int j = 7; j >= 0; j--) {
            exp_hex[j] = h2c[write_val & 0xF];
            write_val >>= 4;
        }
        exp_hex[8] = '\0';

        if (memcmp(reply_val_hex, exp_hex, 8) != 0) {
            printf("  FAIL at Reg%d (read_cmd=0x%02X): expected %s, got %s\n",
                   i, rcmd, exp_hex, reply_val_hex);
            assert(0);
        }
    }

    printf("  PASS (all readable registers verified)\n");
}

static void test_all_writable_regs(void)
{
    printf("=== %s ===\n", __func__);

    UtSimulator sim;
    Sim_Init(&sim);

    /* 遍历所有可写寄存器，通过写命令码写入后读回 */
    for (int i = 1; i <= REGISTER_COUNT; i++) {
        if (!sim.reg_def[i - 1].writable)
            continue;

        uint8_t wcmd = sim.reg_def[i - 1].write_cmd;
        char cmd_hex[3];
        cmd_hex[0] = (wcmd >> 4) >= 10 ? ('A' + (wcmd >> 4) - 10) : ('0' + (wcmd >> 4));
        cmd_hex[1] = (wcmd & 0xF) >= 10 ? ('A' + (wcmd & 0xF) - 10) : ('0' + (wcmd & 0xF));
        cmd_hex[2] = '\0';

        RegValue write_val = (RegValue)((i * 0x123) ^ 0xAAAAAAAA);

        /* 构造写报文 */
        char msg[17];
        build_write_msg(msg, cmd_hex, write_val);
        Sim_Send(&sim, msg);

        /* 通过读命令码读回（如果可读） */
        if (!sim.reg_def[i - 1].readable)
            continue;

        uint8_t rcmd = sim.reg_def[i - 1].read_cmd;
        char rcmd_hex[3];
        rcmd_hex[0] = (rcmd >> 4) >= 10 ? ('A' + (rcmd >> 4) - 10) : ('0' + (rcmd >> 4));
        rcmd_hex[1] = (rcmd & 0xF) >= 10 ? ('A' + (rcmd & 0xF) - 10) : ('0' + (rcmd & 0xF));
        rcmd_hex[2] = '\0';

        char read_msg[17];
        build_read_msg(read_msg, rcmd_hex);
        Sim_Send(&sim, read_msg);

        char reply_val_hex[9];
        memcpy(reply_val_hex, &sim.tx_buf[1], 8);
        reply_val_hex[8] = '\0';

        /* 用 Reg_Read 验证 */
        RegValue read_val;
        Reg_Read(&sim, i, &read_val);
        assert(read_val == write_val);
    }

    printf("  PASS (all writable registers verified)\n");
}

static void test_reg31_write_only(void)
{
    printf("=== %s ===\n", __func__);

    UtSimulator sim;
    Sim_Init(&sim);

    /* Reg31: write_cmd=0x33, read_cmd=NA */
    /* 写入后读接口应返回 false */
    assert(Reg_Write(&sim, 31, 0xDEADBEEF));
    RegValue v;
    assert(!Reg_Read(&sim, 31, &v));

    /* 通过报文写入 */
    char msg[17];
    build_write_msg(msg, "33", 0x12345678);
    Sim_Send(&sim, msg);
    /* 写成功回复值应为写入值 */
    assert(memcmp(&sim.tx_buf[1], "12345678", 8) == 0);

    printf("  PASS\n");
}

/* ------------------------------------------------------------
 * 主入口
 * ------------------------------------------------------------ */
int main(void)
{
    printf("\n========== UT Simulator Tests ==========\n\n");

    test_init_and_reset();
    test_reg_rw_by_id();
    test_reg_rw_by_cmd();
    test_checksum_calculation();
    test_read_register();
    test_invalid_checksum();
    test_invalid_address();
    test_reg31_write_only();
    test_all_readable_regs();
    test_all_writable_regs();

    printf("\n========== ALL TESTS PASSED ==========\n\n");
    return 0;
}
