#pragma once

// Firmware Version
#define FIRMWARE_VER_CODE             8   // Protocol version

// BLE Protocol Command Codes
#define CMD_APP_START              1
#define CMD_GET_CONTACTS           4
#define CMD_SET_ADVERT_NAME        8
#define CMD_SYNC_NEXT_MESSAGE     10
#define CMD_SET_RADIO_PARAMS      11
#define CMD_SET_RADIO_TX_POWER    12
#define CMD_SET_ADVERT_LATLON     13
#define CMD_DEVICE_QUERY          22  // Fixed typo from QEURY

// Response Codes
#define RESP_CODE_OK                    0
#define RESP_CODE_ERR                   1
#define RESP_CODE_SELF_INFO             2
#define RESP_CODE_CONTACTS_START        6
#define RESP_CODE_CONTACT_ENTRY         7
#define RESP_CODE_END_OF_CONTACTS       8
#define RESP_CODE_NO_MORE_MESSAGES     10
#define RESP_CODE_DEVICE_INFO          13

// Error Codes
#define ERR_CODE_UNSUPPORTED_CMD       1
#define ERR_CODE_ILLEGAL_ARG           6

// Radio Parameter Limits
#define MIN_FREQ_HZ          300000
#define MAX_FREQ_HZ         2500000
#define MIN_SF                    5
#define MAX_SF                   12
#define MIN_CR                    5
#define MAX_CR                    8
#define MIN_BW_HZ              7000
#define MAX_BW_HZ            500000
#define MAX_LORA_TX_POWER_SX1262 22

// Coordinate Limits (microdegrees)
#define MAX_LAT              90
#define MIN_LAT             -90
#define MAX_LON             180
#define MIN_LON            -180
#define COORDS_MULTIPLIER  1000000

// Buffer Sizes
#define REPLY_BUFFER_SIZE  160
