/**
 * Cosem security layer functions to (de)cypher and authenticate packets
 *
 * Copyright (c) 2016, Anthony Rabine
 * All rights reserved.
 *
 * This software may be modified and distributed under the terms of the BSD license.
 * See LICENSE.txt for more details.
 *
 */

#include "csm_security.h"
#include "os.h"
#include <string.h>

csm_sec_result csm_sec_auth_decrypt(csm_array *array, csm_request *request, uint8_t *system_title)
{
    csm_sec_result retcode = CSM_SEC_OK;
    csm_sec_control_byte sc;
    uint32_t ic;
    uint8_t *tag_read = NULL;
    uint32_t data_size = 0U;
    uint32_t aad_size = 0U;
    uint8_t IV[12];

    csm_array_read_u8(array, &sc.sh_byte);
    csm_array_read_u32(array, &ic);

    // Prepare IV
    memcpy(&IV[0], &system_title[0], CSM_DEF_APP_TITLE_SIZE);
    PUT_BE32(&IV[8], ic);

    uint8_t *data = csm_array_rd_data(array); // point to the information or tag

    uint32_t unread = csm_array_unread(array); // size of information + tag

    // We have saved the security header (SC + IC), now  override this header (and beyond) with the AAD
    // The AAD is composed with the SC || AK || information. Information can be null.

    uint8_t *aad = (data - 17U); // pointer to the begining of AAD
    aad[0] = sc.sh_byte;
    memcpy(&aad[1], csm_sys_get_key(request->llc.dsap, CSM_SEC_GAK), 16U);


    if (sc.sh_bit_field.encryption)
    {
        CSM_LOG("[SEC] Encryption enabled");

        data_size = unread;
        aad_size = 0U;

        if (sc.sh_bit_field.authentication)
        {
            CSM_LOG("[SEC] Authentication enabled");
            // E + A: size must be higher than the tag
            if (unread > 12U)
            {
                data_size -= 12U;
                aad_size += 17U;
            }
            else
            {
                CSM_ERR("[SEC] Bad packet size for decryption");
                retcode = CSM_SEC_ERROR;
            }
        }

    }
    else if (sc.sh_bit_field.authentication)
    {
        CSM_LOG("[SEC] Authentication only");
        if (unread >= 12U)
        {
            data_size = (unread - 12U);
            aad_size = 17U + data_size; // SC + AK size + information size
            tag_read = data + data_size;
        }
        else
        {
            CSM_ERR("[SEC] Bad packet size for auth");
            retcode = CSM_SEC_ERROR;
        }

        data_size = 0U; // No data to decrypt
    }
    else
    {
        // No any encryption/authentication
        data_size = 0U;
        aad_size = 0U;
    }

    csm_sys_gcm_init(request->channel_id, request->llc.dsap, CSM_SEC_GUEK, IV, aad, aad_size);

    // Decrypt in place
    csm_sys_gcm_update(request->channel_id, data, data_size, data);

    uint8_t tag[16U];
    csm_sys_gcm_finish(request->channel_id, tag);

    if ((tag_read != NULL) && (retcode == CSM_SEC_OK))
    {
        // Need to validate the tag
        if (memcmp(tag, tag_read, 12U) != 0)
        {
            retcode = CSM_SEC_AUTH_FAILURE;
        }
    }

    return retcode;
}
