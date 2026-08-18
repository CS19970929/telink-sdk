#include "bms_lab_simulator.h"

#include "bms/bms_realtime.h"
#include "bms_firmware.h"

#define BMS_LAB_SAMPLE_INTERVAL_MS  (500u)
#define BMS_LAB_CELL_COUNT          (20u)
#define BMS_LAB_TEMPERATURE_COUNT   (4u)
#define BMS_LAB_WAVE_STEPS          (40u)

static uint32_t g_last_sample_ms;
static uint8_t g_sample_initialized;

void bms_lab_simulator_init(void)
{
    g_last_sample_ms = 0u;
    g_sample_initialized = 0u;
}

void bms_lab_simulator_process(uint32_t timestamp_ms)
{
    BmsMeasurement measurement;
    uint8_t phase;
    uint8_t cell_wave;
    uint32_t pack_voltage = 0u;
    uint8_t index;
    int32_t current_ma;

    if ((g_sample_initialized != 0u) &&
        ((uint32_t)(timestamp_ms - g_last_sample_ms) < BMS_LAB_SAMPLE_INTERVAL_MS)) {
        return;
    }
    g_last_sample_ms = timestamp_ms;
    g_sample_initialized = 1u;
    phase = (uint8_t)((timestamp_ms / BMS_LAB_SAMPLE_INTERVAL_MS) % BMS_LAB_WAVE_STEPS);
    cell_wave = (phase < (BMS_LAB_WAVE_STEPS / 2u)) ?
                phase : (uint8_t)(BMS_LAB_WAVE_STEPS - phase);
    current_ma = (phase < (BMS_LAB_WAVE_STEPS / 2u)) ?
                 (400 + (int32_t)phase * 80) :
                 (-400 - (int32_t)(phase - (BMS_LAB_WAVE_STEPS / 2u)) * 80);

    measurement.timestamp_ms = timestamp_ms;
    measurement.valid_flags = BMS_MEASUREMENT_VALID_CELLS |
                              BMS_MEASUREMENT_VALID_TEMPERATURES |
                              BMS_MEASUREMENT_VALID_PACK_VOLTAGE |
                              BMS_MEASUREMENT_VALID_CURRENT |
                              BMS_MEASUREMENT_VALID_CHARGER |
                              BMS_MEASUREMENT_VALID_LOAD;
    measurement.cell_count = BMS_LAB_CELL_COUNT;
    measurement.temperature_count = BMS_LAB_TEMPERATURE_COUNT;
    for (index = 0u; index < BMS_LAB_CELL_COUNT; ++index) {
        uint16_t voltage = (uint16_t)(3700u + index * 2u + cell_wave * 2u +
                                      ((index + phase) % 4u) * 3u);
        if (index == (BMS_LAB_CELL_COUNT - 1u)) {
            voltage = (uint16_t)(voltage + 12u);
        }
        measurement.cell_voltage_mv[index] = voltage;
        pack_voltage += voltage;
    }
    for (index = 0u; index < BMS_LAB_TEMPERATURE_COUNT; ++index) {
        measurement.temperature_decic[index] = (int16_t)(245 + index * 7 + cell_wave);
    }
    measurement.pack_voltage_mv = pack_voltage;
    measurement.current_ma = current_ma;
    measurement.charger_present = (current_ma < 0) ? 1u : 0u;
    measurement.load_present = (current_ma > 0) ? 1u : 0u;
    (void)bms_firmware_publish_measurement(&measurement);
}
