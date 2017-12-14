/*
* Author: Christian Huitema
* Copyright (c) 2017, Private Octopus, Inc.
* All rights reserved.
*
* Permission to use, copy, modify, and distribute this software for any
* purpose with or without fee is hereby granted, provided that the above
* copyright notice and this permission notice appear in all copies.
*
* THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
* ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
* WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
* DISCLAIMED. IN NO EVENT SHALL Private Octopus, Inc. BE LIABLE FOR ANY
* DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
* (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
* LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
* ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
* (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
* SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
*/

#include <stdlib.h>
#include "../picoquic/picoquic_internal.h"

static char const * test_file_name = "ticket_store_test.bin";
static char const * test_sni[] = { "example.com", "example.net", "test.example.com" };
static char const * test_alpn[] = { "hq05", "hq07", "hq09" };
static const size_t nb_test_sni = sizeof(test_sni) / sizeof(char const *);
static const size_t nb_test_alpn = sizeof(test_alpn) / sizeof(char const *);

static int create_test_ticket(uint64_t current_time, uint32_t ttl, uint8_t * buf, uint16_t len)
{
    int ret = 0;
    if (len < 35)
    {
        ret = -1;
    }
    else
    {
        uint16_t t_length = len - 31;
        picoformat_64(buf, current_time);
        buf[8] = 0;
        buf[9] = 1;
        buf[10] = 0;
        buf[11] = (uint8_t)(t_length >>8);
        buf[12] = (uint8_t)(t_length & 0xFF);
        picoformat_32(buf + 13, ttl);
        memset(buf + 17, 0xcc, len - 17);
        buf[len - 18] = 0;
        buf[len - 17] = 16;
    }

    return ret;
}

static int ticket_store_compare(picoquic_stored_ticket_t * s1, picoquic_stored_ticket_t *s2)
{
    int ret = 0;
    picoquic_stored_ticket_t * c1 = s1;
    picoquic_stored_ticket_t * c2 = s2;

    while (ret == 0 && c1 != 0)
    {
        if (c2 == 0)
        {
            ret = -1;
        }
        else
        {
            if (c1->time_valid_until != c2->time_valid_until ||
                c1->sni_length != c2->sni_length ||
                c1->alpn_length != c2->alpn_length ||
                c1->ticket_length != c2->ticket_length ||
                memcmp(c1->sni, c2->sni, c1->sni_length) != 0 ||
                memcmp(c1->alpn, c2->alpn, c1->alpn_length) != 0 ||
                memcmp(c1->ticket, c2->ticket, c1->ticket_length) != 0)
            {
                ret = -1;
            }
            else
            {
                c1 = c1->next_ticket;
                c2 = c2->next_ticket;
            }
        }
    }

    if (ret == 0 && c1 == NULL && c2 != NULL)
    {
        ret = -1;
    }

    return ret;
}

int ticket_store_test()
{
    int ret = 0;
    picoquic_stored_ticket_t * p_first_ticket = NULL;
    picoquic_stored_ticket_t * p_first_ticket_bis = NULL;
    picoquic_stored_ticket_t * p_first_ticket_ter = NULL;
    
    uint64_t ticket_time = 40000000000ull;
    uint64_t current_time = 50000000000ull;
    uint64_t retrieve_time = 60000000000ull;
    uint64_t too_late_time = 150000000000ull;
    uint64_t create_time = 10000000ull;
    uint32_t ttl = 100000;
    uint8_t ticket[128];

    /* Generate a set of tickets */
    for (size_t i = 0; ret == 0 && i < nb_test_sni; i++)
    {
        for (size_t j = 0;  ret == 0 && j < nb_test_alpn; j++)
        {
            uint16_t ticket_length = (uint16_t)(64 + j*nb_test_sni + i);
            ret = create_test_ticket((ticket_time/1000) + 1000*((i*nb_test_alpn) + j), ttl, ticket, ticket_length);
            if (ret != 0)
            {
                break;
            }
            ret = picoquic_store_ticket(&p_first_ticket, current_time, 
                test_sni[i], (uint16_t) strlen(test_sni[i]),
                test_alpn[j], (uint16_t) strlen(test_alpn[j]),
                ticket, ticket_length);
            if (ret != 0)
            {
                break;
            }
        }
    }

    /* Verify that they can be retrieved */
    for (size_t i = 0; ret == 0 && i < nb_test_sni; i++)
    {
        for (size_t j = 0; ret == 0 && j < nb_test_alpn; j++)
        {
            uint16_t ticket_length = 0;
            uint16_t expected_length = (uint16_t)(64 + j*nb_test_sni + i);
            uint8_t * ticket = NULL;
            ret = picoquic_get_ticket(p_first_ticket, current_time,
                test_sni[i], (uint16_t) strlen(test_sni[i]),
                test_alpn[j], (uint16_t) strlen(test_alpn[j]),
                &ticket, &ticket_length);
            if (ret != 0)
            {
                break;
            }
            if (ticket_length != expected_length)
            {
                ret = -1;
                break;
            }
        }
    }
    /* Store them on a file */
    if (ret == 0)
    {
        ret = picoquic_save_tickets(p_first_ticket, current_time, test_file_name);
    }
    /* Load the file again */
    if (ret == 0)
    {
        ret = picoquic_load_tickets(&p_first_ticket_bis, retrieve_time, test_file_name);
    }

    /* Verify that the two contents match */
    if (ret == 0)
    {
        ret = ticket_store_compare(p_first_ticket, p_first_ticket_bis);
    }

    /* Reload after a long time */
    if (ret == 0)
    {
        ret = picoquic_load_tickets(&p_first_ticket_ter, too_late_time, test_file_name);

        if (ret == 0 && p_first_ticket_ter != NULL)
        {
            ret = -1;
        }
    }
    /* Free what needs be */
    picoquic_free_tickets(&p_first_ticket);
    picoquic_free_tickets(&p_first_ticket_bis);
    picoquic_free_tickets(&p_first_ticket_ter);

    return ret;
}