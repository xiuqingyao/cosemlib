// Cosem library
#include "csm_config.h"
#include "csm_association.h"
#include "csm_definitions.h"

// OS/System definitions
#include "os.h"
#include "server_config.h"

// Ciphering library
#include "gcm.h"

// Standard libraries
#include <string.h>
#include <stdlib.h>

/*
the  leading  (i.e.  the  leftmost)  64  bits  (8  octets)  shall  hold  the  fixed  field.  It  shall  contain  the
system title, see 4.3.4;
•     the trailing (i.e. the rightmost) 32 bits shall hold the invocation field. The invocation field shall be
an integer counter.
*/

static uint8_t system_title[CSM_DEF_APP_TITLE_SIZE] = { 0x4DU, 0x4DU, 0x4DU, 0x00U, 0x00U, 0xBCU, 0x61U, 0x4EU }; // GreenBook server example

// FIXME:
// 1. Store the key in a configuration file as they can be updated on the field
// 2. Create a key-ring per association (SAP)

// Master key, common for all the associations, not changeable
static uint8_t key_kek[16] = { 0xFFU,0xFFU,0xFFU,0xFFU,0xFFU,0xFFU,0xFFU,0xFFU,0xFFU,0xFFU,0xFFU,0xFFU,0xFFU,0xFFU,0xFFU,0xFFU };

// One key per association
static uint8_t key_guek[16] = { 0x00U,0x01U,0x02U,0x03U,0x04U,0x05U,0x06U,0x07U,0x08U,0x09U,0x0AU,0x0BU,0x0CU,0x0DU,0x0EU,0x0FU };
static uint8_t key_gbek[16] = { 0xFFU,0xFFU,0xFFU,0xFFU,0xFFU,0xFFU,0xFFU,0xFFU,0xFFU,0xFFU,0xFFU,0xFFU,0xFFU,0xFFU,0xFFU,0xFFU };
static uint8_t key_gak[16] = { 0xD0U,0xD1U,0xD2U,0xD3U,0xD4U,0xD5U,0xD6U,0xD7U,0xD8U,0xD9U,0xDAU,0xDBU,0xDCU,0xDDU,0xDEU,0xDFU };

// Keep a context by channel to be thread safe
mbedtls_gcm_context chan_ctx[NUMBER_OF_CHANNELS];

void csm_sys_set_system_title(const uint8_t *buf)
{
    memcpy(system_title, buf, sizeof(system_title));
}

const uint8_t *csm_sys_get_system_title()
{
    return system_title;
}


uint8_t *csm_sys_get_key(uint8_t sap, csm_sec_key key_id)
{
    (void)sap; // FIXME: manage one key per SAP
    uint8_t *key = NULL;

    switch(key_id)
    {
    case CSM_SEC_KEK:
        key = key_kek;
        break;
    case CSM_SEC_GUEK:
        key = key_guek;
        break;
    case CSM_SEC_GBEK:
        key = key_gbek;
        break;
    default:
    case CSM_SEC_GAK:
        key = key_gak;
        break;
    }
    return key;
}


int csm_sys_gcm_init(uint8_t channel, uint8_t sap, csm_sec_key key_id, const uint8_t *iv, const uint8_t *aad, uint32_t aad_len)
{
    mbedtls_gcm_init(&chan_ctx[channel]);
    mbedtls_gcm_setkey(&chan_ctx[channel], MBEDTLS_CIPHER_ID_AES, csm_sys_get_key(sap, key_id), 128);
    int res = mbedtls_gcm_starts(&chan_ctx[channel], MBEDTLS_GCM_ENCRYPT, iv, 12, aad, aad_len);
    return (res == 0) ? TRUE : FALSE;
}

int csm_sys_gcm_update(uint8_t channel, const uint8_t *plain, uint32_t plain_len, uint8_t *crypt)
{
    mbedtls_gcm_update(&chan_ctx[channel], plain_len, plain, crypt);
    return TRUE;
}

// Sizes are total sizes of plain and AAD
int csm_sys_gcm_finish(uint8_t channel, uint8_t *tag)
{
    mbedtls_gcm_finish(&chan_ctx[channel], tag, 16);
    return TRUE;
}

struct cfg_cosem
{
    uint8_t sap; //!< Sap number of the association
    uint8_t lls_password[CSM_DEF_LLS_SIZE]; // Password.
    uint8_t mechanism_id;
};

// FIXME: in a real server, store these parameters into non-volatile memory (get/set access)
static const struct cfg_cosem cfg_cosem_passwords [] =
{
    { 16U, { 0x30U, 0x30U, 0x30U, 0x30U, 0x30U, 0x30U, 0x30U, 0x30U }, CSM_AUTH_LOWEST_LEVEL},  // Public
    { 1U, { 0x30U, 0x30U, 0x30U, 0x30U, 0x30U, 0x30U, 0x30U, 0x31U }, CSM_AUTH_LOW_LEVEL}, // Management
};

#define CFG_COSEM_NB_ASSOS  (sizeof(cfg_cosem_passwords)/sizeof(struct cfg_cosem))

int csm_sys_test_lls_password(uint8_t sap, uint8_t *buf, uint32_t size)
{
    int valid = FALSE;

    if (size == CSM_DEF_LLS_SIZE)
    {
        for (uint32_t i = 0U; i < CFG_COSEM_NB_ASSOS; i++)
        {
            if (sap == cfg_cosem_passwords[i].sap)
            {
                int ret = memcmp(buf, cfg_cosem_passwords[i].lls_password, size);
                valid = (ret == 0) ? TRUE : FALSE;
                break;
            }
        }
    }
    return valid;
}

uint8_t csm_sys_get_mechanism_id(uint8_t sap)
{
    uint8_t mechanism_id = CSM_AUTH_LOWEST_LEVEL;
    for (uint32_t i = 0U; i < CFG_COSEM_NB_ASSOS; i++)
    {
        if (sap == cfg_cosem_passwords[i].sap)
        {
            mechanism_id = cfg_cosem_passwords[i].mechanism_id;
            break;
        }
    }
    return mechanism_id;
}

uint8_t csm_sys_get_random_u8()
{
    return (uint8_t)(rand()%256);
}

