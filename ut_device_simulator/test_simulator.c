#include <stdio.h>
#include <string.h>
#include <assert.h>
#include "ut_device_simulator.h"

/*===========================================================================
 * 辅助函数
 *==========================================================================*/

static void print_buf(const char *tag, const char *buf, uint32_t len)
{
    printf("[%s] len=%u: ", tag, len);
    for (uint32_t i = 0; i < len; i++) {
        printf("%02X ", (uint8_t)buf[i]);
    }
    printf("  (ascii: ");
    for (uint32_t i = 0; i < len; i++) {
        char c = buf[i];
        printf("%c", (c >= 32 && c < 127) ? c : '.');
    }
    printf(")\n");
}

/* 计算报文校验码并构造完整下发报文 */
static void make_rx_msg(char *out, uint8_t cmd, uint32_t value)
{
    static const char hex_chars[] = "0123456789ABCDEF";
    /* 格式：*62aabbbbbbbbcc^ */
    out[0] = '*';
    out[1] = '6';
    out[2] = '2';
    out[3] = hex_chars[(cmd >> 4) & 0x0F];
    out[4] = hex_chars[cmd & 0x0F];
    out[5] = hex_chars[(value >> 28) & 0x0F];
    out[6] = hex_chars[(value >> 24) & 0x0F];
    out[7] = hex_chars[(value >> 20) & 0x0F];
    out[8] = hex_chars[(value >> 16) & 0x0F];
    out[9] = hex_chars[(value >> 12) & 0x0F];
    out[10] = hex_chars[(value >>  8) & 0x0F];
    out[11] = hex_chars[(value >>  4) & 0x0F];
    out[12] = hex_chars[(value >>  0) & 0x0F];

    /* 校验码：data[1..12]之和模128 */
    uint32_t sum = 0;
    for (int i = 1; i <= 12; i++) {
        sum += (uint8_t)out[i];
    }
    uint8_t cc = (uint8_t)(sum & 0x7F);

    out[13] = hex_chars[(cc >> 4) & 0x0F];
    out[14] = hex_chars[cc & 0x0F];
    out[15] = '^';
    out[16] = '\0';
}

/*===========================================================================
 * 测试用例
 *==========================================================================*/

void test_init_and_checksum(void)
{
    printf("\n=== test_init_and_checksum ===\n");

    Simulator_Init();

    /* 校验码计算测试
     * 报文 "*62410000000000^" 的data[1..12] = "624100000000"
     * ASCII: '6'=0x36,'2'=0x32,'4'=0x34,'1'=0x31,'0'=0x30 x 8
     * sum = 0x36+0x32+0x34+0x31+0x30*8 = 0x24D
     * cs = 0x24D & 0x7F = 0x4D
     */
    char test_data[] = "624100000000";
    uint8_t cs = Calc_Checksum(test_data, 12);
    printf("checksum('624100000000') = 0x%02X (expected 0x4D)\n", cs);
    assert(cs == 0x4D);

    printf("PASS\n");
}

void test_reg_rw_via_api(void)
{
    printf("\n=== test_reg_rw_via_api ===\n");

    Simulator_Init();

    /* 通过API设置寄存器7的值（写命令0x28） */
    int ret = Simulator_SetReg(7, 0x12345678);
    assert(ret == 0);

    uint32_t val = 0xFFFFFFFF;
    ret = Simulator_GetReg(7, &val);
    assert(ret == 0);
    assert(val == 0x12345678);

    /* 寄存器1是NA-01，只读不可写
     * 读应该成功，写应该失败
     */
    ret = Simulator_GetReg(1, &val);  /* 只读寄存器，读成功 */
    assert(ret == 0);

    ret = Simulator_SetReg(1, 100);  /* 只读寄存器，写失败 */
    assert(ret == -2);

    /* 无效寄存器 */
    ret = Simulator_GetReg(0, &val);
    assert(ret == -1);
    ret = Simulator_SetReg(100, 0);
    assert(ret == -1);

    printf("PASS\n");
}

void test_read_reg7_via_msg(void)
{
    printf("\n=== test_read_reg7_via_msg ===\n");

    Simulator_Init();

    /* 先通过API设置寄存器7的值 */
    Simulator_SetReg(7, 0xDEADBEEF);

    /* 构造读寄存器7的报文：读命令=0x41 */
    char rx_msg[32];
    make_rx_msg(rx_msg, 0x41, 0x00000000); /* 读，值填0 */

    printf("TX msg: %s\n", rx_msg);
    print_buf("RX", rx_msg, 16);

    Simulator_SetRx(rx_msg, strlen(rx_msg));

    int ret = Simulator_ParseRx();
    assert(ret == 0);

    uint32_t tx_len = 0;
    const char *tx = Simulator_GetTx(&tx_len);
    printf("RX msg: %s (len=%u)\n", tx, tx_len);
    print_buf("TX", tx, tx_len);

    /* 回复应该是 *DEADBEEFxx^ (xx为校验码)
     * 格式: [0]='*', [1..8]=值, [9..10]=校验码, [11]='^'
     */
    assert(tx[0] == '*');
    assert(tx[11] == '^');
    assert(strncmp(&tx[1], "DEADBEEF", 8) == 0);

    printf("PASS\n");
}

void test_write_reg7_via_msg(void)
{
    printf("\n=== test_write_reg7_via_msg ===\n");

    Simulator_Init();

    /* 构造写寄存器7的报文：写命令=0x28 */
    char rx_msg[32];
    make_rx_msg(rx_msg, 0x28, 0xCAFEBABE);

    printf("TX msg: %s\n", rx_msg);
    print_buf("RX", rx_msg, 16);

    Simulator_SetRx(rx_msg, strlen(rx_msg));

    int ret = Simulator_ParseRx();
    assert(ret == 0);

    uint32_t tx_len = 0;
    const char *tx = Simulator_GetTx(&tx_len);
    printf("RX msg: %s (len=%u)\n", tx, tx_len);
    print_buf("TX", tx, tx_len);

    /* 回复应该回显写入值 */
    assert(tx[0] == '*');
    assert(strncmp(&tx[1], "CAFEBABE", 8) == 0);

    /* 验证寄存器值已更新 */
    uint32_t val = 0;
    Simulator_GetReg(7, &val);
    assert(val == 0xCAFEBABE);

    printf("PASS\n");
}

void test_read_only_reg(void)
{
    printf("\n=== test_read_only_reg ===\n");

    Simulator_Init();

    /* 寄存器1只读（NA-01），写命令0xFF，读命令0x01 */
    /* 尝试用写命令写只读寄存器应该失败 */
    char rx_msg[32];
    make_rx_msg(rx_msg, 0xFF, 0x12345678); /* 无效命令 */
    Simulator_SetRx(rx_msg, strlen(rx_msg));
    int ret = Simulator_ParseRx();
    printf("write to read-only reg with cmd=0xFF: ret=%d (expected -3 unknown cmd)\n", ret);
    assert(ret == -3);

    /* 用读命令读只读寄存器 */
    make_rx_msg(rx_msg, 0x01, 0x00000000);
    Simulator_SetRx(rx_msg, strlen(rx_msg));
    ret = Simulator_ParseRx();
    printf("read read-only reg with cmd=0x01: ret=%d\n", ret);
    assert(ret == 0);

    printf("PASS\n");
}

void test_write_only_reg(void)
{
    printf("\n=== test_write_only_reg ===\n");

    Simulator_Init();

    /* 寄存器31只写（33-NA），写命令=0x33，读命令=0xFF(不可读) */
    /* 用写命令写 */
    char rx_msg[32];
    make_rx_msg(rx_msg, 0x33, 0x12345678);
    Simulator_SetRx(rx_msg, strlen(rx_msg));
    int ret = Simulator_ParseRx();
    printf("write write-only reg31 with cmd=0x33: ret=%d\n", ret);
    assert(ret == 0); /* 写成功，回显值 */

    /* 用读命令读只写寄存器应该失败 */
    make_rx_msg(rx_msg, 0xFF, 0x00000000);
    Simulator_SetRx(rx_msg, strlen(rx_msg));
    ret = Simulator_ParseRx();
    printf("read write-only reg with cmd=0xFF: ret=%d (expected -3)\n", ret);
    assert(ret == -3);

    printf("PASS\n");
}

void test_full_rw_cycle(void)
{
    printf("\n=== test_full_rw_cycle ===\n");

    Simulator_Init();

    char rx_msg[32];
    int ret;

    /* 测试寄存器7的完整读写循环 */
    /* 1. 初始值 */
    Simulator_SetReg(7, 0x11111111);
    uint32_t val = 0;
    Simulator_GetReg(7, &val);
    assert(val == 0x11111111);

    /* 2. 写一个新值 */
    make_rx_msg(rx_msg, 0x28, 0x22222222);
    Simulator_SetRx(rx_msg, strlen(rx_msg));
    ret = Simulator_ParseRx();
    assert(ret == 0);

    /* 3. 读回来验证 */
    make_rx_msg(rx_msg, 0x41, 0x00000000);
    Simulator_SetRx(rx_msg, strlen(rx_msg));
    ret = Simulator_ParseRx();
    assert(ret == 0);

    const char *tx = Simulator_GetTx(NULL);
    assert(strncmp(&tx[1], "22222222", 8) == 0);

    printf("PASS\n");
}

/*===========================================================================
 * 主函数
 *==========================================================================*/

int main(void)
{
    printf("========================================\n");
    printf("  UT Device Simulator Unit Tests\n");
    printf("========================================\n");

    test_init_and_checksum();
    test_reg_rw_via_api();
    test_read_reg7_via_msg();
    test_write_reg7_via_msg();
    test_read_only_reg();
    test_write_only_reg();
    test_full_rw_cycle();

    printf("\n========================================\n");
    printf("  ALL TESTS PASSED\n");
    printf("========================================\n");

    return 0;
}
