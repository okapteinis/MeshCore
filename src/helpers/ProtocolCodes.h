#pragma once

/* ------------------------ Serial Protocol Commands -------------------- */
// Binary protocol commands for app connection (minimal subset for repeater)
#define CMD_APP_START                 1
#define CMD_GET_CONTACTS              4
#define CMD_SET_ADVERT_NAME           8
#define CMD_SYNC_NEXT_MESSAGE         10
#define CMD_SET_RADIO_PARAMS          11
#define CMD_SET_RADIO_TX_POWER        12
#define CMD_SET_ADVERT_LATLON         14
#define CMD_DEVICE_QEURY              22

#define RESP_CODE_OK                  0
#define RESP_CODE_ERR                 1
#define RESP_CODE_CONTACTS_START      2
#define RESP_CODE_END_OF_CONTACTS     4
#define RESP_CODE_SELF_INFO           5
#define RESP_CODE_NO_MORE_MESSAGES    10
#define RESP_CODE_DEVICE_INFO         13

// Error codes (1-10)
#define ERR_CODE_UNSUPPORTED_CMD      1
#define ERR_CODE_FILE_IO_ERROR        5
#define ERR_CODE_ILLEGAL_ARG          6

#define FIRMWARE_VER_CODE             8   // Protocol version
#define ADV_TYPE_REPEATER             6   // Repeater node type
