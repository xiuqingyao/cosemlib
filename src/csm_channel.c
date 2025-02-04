/**
 * A virtual channel of communication with the logical device
 *
 * Copyright (c) 2016, Anthony Rabine
 * All rights reserved.
 *
 * This software may be modified and distributed under the terms of the BSD license.
 * See LICENSE.txt for more details.
 *
 */

#include "csm_channel.h"
#include "csm_config.h"
#include "csm_services.h"
#include "csm_security.h"

// List of channels
static csm_channel *channel_list = NULL;
static uint8_t channel_list_size;

// List of association state and ocnfiguration
static csm_asso_state *asso_list = NULL;
static const csm_asso_config *asso_conf_list = NULL;
static uint8_t asso_list_size;

void csm_channel_init(csm_channel *channels, uint8_t chan_size, csm_asso_state *assos, const csm_asso_config *assos_config, uint8_t asso_size)
{
    // Save system channels and
    channel_list = channels;
    channel_list_size = chan_size;
    asso_list = assos;
    asso_conf_list = assos_config;
    asso_list_size = asso_size;

    for (uint32_t i = 0U; i < asso_size; i++)
    {
        csm_asso_init(&assos[i]);
    }

    for (uint32_t i = 0U; i < chan_size; i++)
    {
        channels[i].asso = NULL;
        channels[i].request.channel_id = INVALID_CHANNEL_ID;
    }
}

int csm_channel_execute(uint8_t channel, csm_array *packet)
{
    int ret = FALSE;

    if ((channel_list == NULL) ||
        (asso_list == NULL) ||
        (asso_conf_list == NULL))
    {
        CSM_ERR("[CHAN] Stack is not initialized. Call csm_channel_init() first.");
        return ret;
    }

    uint32_t i = 0U;

    // We have to find the association used by this request
    // Find the valid association.
    for (i = 0U; i < asso_list_size; i++)
    {
        if ((channel_list[CHAN].request.llc.ssap == asso_conf_list[i].llc.ssap) &&
            (channel_list[CHAN].request.llc.dsap == asso_conf_list[i].llc.dsap))
        {
            break;
        }
    }

    if (i < asso_list_size)
    {
        // Association found, use this one
        // Link the state with the configuration structure
        asso_list[i].config = &asso_conf_list[i];
        channel_list[CHAN].asso = &asso_list[i];

        uint8_t tag;
        if (csm_array_get(packet, 0U, &tag))
        {
            switch (tag)
            {
            case CSM_ASSO_AARE:
            case CSM_ASSO_AARQ:
            case CSM_ASSO_RLRE:
            case CSM_ASSO_RLRQ:
                ret = csm_asso_execute(&asso_list[i], packet);
                break;
            default:
                if (asso_list[i].state_cf == CF_ASSOCIATED)
                {
                    ret = csm_services_execute(&asso_list[i], &channel_list[CHAN].request, packet);
                }
                else if (asso_list[i].state_cf == CF_ASSOCIATION_PENDING)
                {
                    // In case of HLS, we have to access to one attribute
                    ret = csm_services_hls_execute(&asso_list[i], &channel_list[CHAN].request, packet);
                }
                else
                {
                    CSM_ERR("[CHAN] Association is not open");
                }
                break;
            }
        }
    }
    return ret;
}

int csm_channel_hls_pass3(csm_array *array, csm_request *request)
{
    csm_sec_control_byte sc;
    uint32_t ic;

    // Save SC and IC
    csm_array_read_u8(array, &sc.sh_byte);
    csm_array_read_u32(array, &ic);

    // Remaing data should be the TAG
    uint32_t unread = csm_array_unread(array);

    if (unread == 12U)
    {
        uint32_t offset = array->offset; // Save the original offset

        if (offset >= CSM_DEF_MAX_HLS_SIZE)
        {
            csm_asso_state *asso = channel_list[request->channel_id - 1U].asso;

            // Reserve memory & prepare packet
            array->offset = (offset + array->rd_index) - (5U + asso->handshake.stoc.size);
            array->rd_index = 0U;
            array->wr_index = 0U;

            // Build a new fake packet with: SC || IC || Information || Tag
            // Tag is left untouched, other data are appended just before
            csm_array_write_u8(array, sc.sh_byte);
            csm_array_write_u32(array, ic);
            csm_array_write_buff(array, &asso->handshake.stoc.value[0], asso->handshake.stoc.size);
            csm_array_writer_jump(array, 12U); // Add the tag (already in the buffer)

            csm_sec_result res = csm_sec_auth_decrypt(array, request, &asso->client_app_title[0]);

            if (res == CSM_SEC_OK)
            {
                CSM_LOG("[CHAN] HLS Pass 3 success");
                // Reply with the pass4, restore array parameters
                array->offset = offset;
                array->wr_index = 0U;
            }
            else
            {
                CSM_ERR("[CHAN] Bad tag");
            }
        }
        else
        {
            CSM_ERR("[CHAN] Array too small for HLS");
        }
    }
    else
    {
        CSM_ERR("[CHAN] Bad HLS Pass3 size");
    }

    return FALSE;
}

void csm_channel_disconnect(uint8_t channel)
{
    if (channel < channel_list_size)
    {
        channel_list[CHAN].request.channel_id = INVALID_CHANNEL_ID;
        if (channel_list[CHAN].asso != NULL)
        {
            channel_list[CHAN].asso->state_cf = CF_IDLE;
        }
    }
}

uint8_t csm_channel_new(void)
{
    uint8_t chan_id = INVALID_CHANNEL_ID;
    // search for a valid free channel
    // In case of CONN_NEW event, channel parameter is 0 (means invalid)
    for (uint32_t i = 0U; i < channel_list_size; i++)
    {
        if (channel_list[i].request.channel_id == INVALID_CHANNEL_ID)
        {
            chan_id = i + 1U; // generate a channel id
            channel_list[i].request.channel_id = chan_id;
            CSM_LOG("[CHAN] Grant connection to channel %d", chan_id);
            break;
        }
    }

    return chan_id;
}
