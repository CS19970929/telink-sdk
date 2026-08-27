#include "bms_param_store.h"
#include "hs_d011_board.h"
#include "drivers.h"
#include <string.h>

#define BMS_PARAM_STORE_MAGIC           0x42504D53u /* 'BPMS' */
#define BMS_PARAM_STORE_FORMAT_VERSION  0x0001u
#define BMS_PARAM_STORE_MAX_VALUES      65u
#define BMS_PARAM_STORE_HEADER_SIZE     16u
#define BMS_PARAM_STORE_CRC_SIZE        4u
#define BMS_PARAM_STORE_MAX_SIZE        (BMS_PARAM_STORE_HEADER_SIZE + BMS_PARAM_STORE_MAX_VALUES * 4u + BMS_PARAM_STORE_CRC_SIZE)
#define BMS_PARAM_STORE_SLOT_NONE       0xFFu

static u8 s_record[BMS_PARAM_STORE_MAX_SIZE];
static u8 s_verify[BMS_PARAM_STORE_MAX_SIZE];
static u32 s_sequence;
static u8 s_active_slot = BMS_PARAM_STORE_SLOT_NONE;
static u16 s_status;

static void put_u16_le(u8 *p, u16 v)
{
    p[0] = (u8)v;
    p[1] = (u8)(v >> 8);
}

static u16 get_u16_le(const u8 *p)
{
    return (u16)((u16)p[0] | ((u16)p[1] << 8));
}

static void put_u32_le(u8 *p, u32 v)
{
    p[0] = (u8)v;
    p[1] = (u8)(v >> 8);
    p[2] = (u8)(v >> 16);
    p[3] = (u8)(v >> 24);
}

static u32 get_u32_le(const u8 *p)
{
    return (u32)p[0] |
           ((u32)p[1] << 8) |
           ((u32)p[2] << 16) |
           ((u32)p[3] << 24);
}

static u32 crc32_ieee(const u8 *data, u16 len)
{
    u32 crc = 0xFFFFFFFFu;
    u8 bit;

    while (len--) {
        crc ^= *data++;
        for (bit = 0; bit < 8u; ++bit)
            crc = (crc & 1u) ? ((crc >> 1) ^ 0xEDB88320u) : (crc >> 1);
    }
    return crc ^ 0xFFFFFFFFu;
}

static u16 record_size(u16 count)
{
    return (u16)(BMS_PARAM_STORE_HEADER_SIZE + count * 4u + BMS_PARAM_STORE_CRC_SIZE);
}

static u16 crc_offset(u16 count)
{
    return (u16)(BMS_PARAM_STORE_HEADER_SIZE + count * 4u);
}

static int flash_capacity_supported(void)
{
    u32 mid = (u32)flash_read_mid();
    return (u8)((mid >> 16) & 0xFFu) == (u8)BMS_PARAM_FLASH_REQUIRED_SIZE;
}

static int validate_record(const u8 *buf, u16 count, u16 schema_version, u32 *sequence)
{
    u16 crc_off;
    u32 stored_crc;

    if (!buf || count == 0u || count > BMS_PARAM_STORE_MAX_VALUES) return 0;
    if (get_u32_le(&buf[0]) != BMS_PARAM_STORE_MAGIC) return 0;
    if (get_u16_le(&buf[4]) != BMS_PARAM_STORE_FORMAT_VERSION) return 0;
    if (get_u16_le(&buf[6]) != schema_version) return 0;
    if (get_u16_le(&buf[8]) != count) return 0;

    crc_off = crc_offset(count);
    stored_crc = get_u32_le(&buf[crc_off]);
    if (stored_crc != crc32_ieee(buf, crc_off)) return 0;

    if (sequence) *sequence = get_u32_le(&buf[12]);
    return 1;
}

static int sequence_is_newer(u32 a, u32 b)
{
    return (s32)(a - b) > 0;
}

int bms_param_store_load(s32 *values, u16 count, u16 schema_version)
{
    u16 size;
    u16 i;
    u32 seq_a = 0u;
    u32 seq_b = 0u;
    int valid_a;
    int valid_b;
    const u8 *chosen;

    s_status = 0u;
    s_sequence = 0u;
    s_active_slot = BMS_PARAM_STORE_SLOT_NONE;

    if (!values || count == 0u || count > BMS_PARAM_STORE_MAX_VALUES)
        return 0;
    if (!flash_capacity_supported()) {
        s_status |= BMS_PARAM_STORE_ST_ERROR;
        return 0;
    }

    s_status |= BMS_PARAM_STORE_ST_SUPPORTED;
    size = record_size(count);

    flash_read_page(BMS_PARAM_FLASH_SLOT_A, size, s_record);
    flash_read_page(BMS_PARAM_FLASH_SLOT_B, size, s_verify);
    valid_a = validate_record(s_record, count, schema_version, &seq_a);
    valid_b = validate_record(s_verify, count, schema_version, &seq_b);

    if (!valid_a && !valid_b)
        return 0; /* Normal first-boot state: defaults will be used. */

    if (valid_b && (!valid_a || sequence_is_newer(seq_b, seq_a))) {
        chosen = s_verify;
        s_sequence = seq_b;
        s_active_slot = 1u;
        s_status |= BMS_PARAM_STORE_ST_ACTIVE_SLOT_B;
    } else {
        chosen = s_record;
        s_sequence = seq_a;
        s_active_slot = 0u;
    }

    for (i = 0; i < count; ++i)
        values[i] = (s32)get_u32_le(&chosen[BMS_PARAM_STORE_HEADER_SIZE + i * 4u]);

    s_status |= BMS_PARAM_STORE_ST_VALID_RECORD;
    return 1;
}

int bms_param_store_save(const s32 *values, u16 count, u16 schema_version)
{
    u16 size;
    u16 crc_off;
    u16 first_len;
    u16 second_len;
    u16 i;
    u32 next_sequence;
    u32 target;
    u8 target_slot;

    s_status &= (u16)~(BMS_PARAM_STORE_ST_LAST_SAVE_OK | BMS_PARAM_STORE_ST_ERROR);

    if (!values || count == 0u || count > BMS_PARAM_STORE_MAX_VALUES) {
        s_status |= BMS_PARAM_STORE_ST_ERROR;
        return 0;
    }
    if (!flash_capacity_supported()) {
        s_status &= (u16)~BMS_PARAM_STORE_ST_SUPPORTED;
        s_status |= BMS_PARAM_STORE_ST_ERROR;
        return 0;
    }
    s_status |= BMS_PARAM_STORE_ST_SUPPORTED;

    size = record_size(count);
    crc_off = crc_offset(count);
    memset(s_record, 0xFF, size);

    put_u32_le(&s_record[0], BMS_PARAM_STORE_MAGIC);
    put_u16_le(&s_record[4], BMS_PARAM_STORE_FORMAT_VERSION);
    put_u16_le(&s_record[6], schema_version);
    put_u16_le(&s_record[8], count);
    put_u16_le(&s_record[10], 0u);

    next_sequence = s_sequence + 1u;
    put_u32_le(&s_record[12], next_sequence);
    for (i = 0; i < count; ++i)
        put_u32_le(&s_record[BMS_PARAM_STORE_HEADER_SIZE + i * 4u], (u32)values[i]);
    put_u32_le(&s_record[crc_off], crc32_ieee(s_record, crc_off));

    target_slot = (s_active_slot == 0u) ? 1u : 0u;
    target = target_slot ? BMS_PARAM_FLASH_SLOT_B : BMS_PARAM_FLASH_SLOT_A;

    flash_erase_sector(target);

    first_len = size > PAGE_SIZE ? PAGE_SIZE : size;
    flash_write_page(target, first_len, s_record);
    second_len = (u16)(size - first_len);
    if (second_len)
        flash_write_page(target + first_len, second_len, &s_record[first_len]);

    flash_read_page(target, size, s_verify);
    if (memcmp(s_record, s_verify, size) != 0 ||
        !validate_record(s_verify, count, schema_version, 0)) {
        s_status |= BMS_PARAM_STORE_ST_ERROR;
        return 0;
    }

    s_sequence = next_sequence;
    s_active_slot = target_slot;
    s_status |= BMS_PARAM_STORE_ST_VALID_RECORD | BMS_PARAM_STORE_ST_LAST_SAVE_OK;
    if (target_slot)
        s_status |= BMS_PARAM_STORE_ST_ACTIVE_SLOT_B;
    else
        s_status &= (u16)~BMS_PARAM_STORE_ST_ACTIVE_SLOT_B;
    return 1;
}

u16 bms_param_store_status_word(void)
{
    return s_status;
}
