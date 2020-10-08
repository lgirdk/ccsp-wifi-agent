/*********************************************************************
 * Copyright 2017-2019 LGI Enterprises, LLC.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 * http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 **********************************************************************/

#include "ansc_platform.h"
#include "cosa_wifi_internal.h"
#include "wifi_hal.h"

/***********************************************************************

 APIs for Object:

    X_LGI_COM_ChannelStats.

    *  WifiRadioChannelStats_GetEntryCount
    *  WifiRadioChannelStats_GetEntry
    *  WifiRadioChannelStats_IsUpdated
    *  WifiRadioChannelStats_Synchronize
    *  WifiRadioChannelStats_GetParamBoolValue
    *  WifiRadioChannelStats_GetParamIntValue
    *  WifiRadioChannelStats_GetParamStringValue

***********************************************************************/
ULONG
WifiRadioChannelStats_GetEntryCount
    (
        ANSC_HANDLE                 hInsContext
    )
{
    PCOSA_DML_WIFI_RADIO       pWifiRadio      = hInsContext;
    PCOSA_DML_WIFI_RADIO_STATS pWifiRadioStats = &pWifiRadio->Stats;

    return pWifiRadioStats->ChannelCount;
}

ANSC_HANDLE
WifiRadioChannelStats_GetEntry
    (
        ANSC_HANDLE                 hInsContext,
        ULONG                       nIndex,
        ULONG*                      pInsNumber
    )
{
    PCOSA_DML_WIFI_RADIO       pWifiRadio      = hInsContext;
    PCOSA_DML_WIFI_RADIO_STATS pWifiRadioStats = &pWifiRadio->Stats;
    PCOSA_DML_CHANNEL_STATS    pChannelStats   = pWifiRadioStats->ChannelStats;

    if (nIndex >= pWifiRadioStats->ChannelCount)
    {
        return NULL;
    }
    else
    {
        *pInsNumber  = nIndex + 1;
        return pChannelStats+nIndex;
    }

    return NULL; /* return the handle */
}

void copyChannelStats(PCOSA_DML_CHANNEL_STATS tr181ChannelStatsEntry, wifi_channelStats_t *wifihalChannelStatsEntry)
{
    if (tr181ChannelStatsEntry && wifihalChannelStatsEntry)
    {
        memset(tr181ChannelStatsEntry, 0, sizeof(COSA_DML_CHANNEL_STATS));
        tr181ChannelStatsEntry->ChannelNumber = wifihalChannelStatsEntry->ch_number;
        tr181ChannelStatsEntry->ChannelInPool = wifihalChannelStatsEntry->ch_in_pool;
        tr181ChannelStatsEntry->ChannelNoise = wifihalChannelStatsEntry->ch_noise;
        tr181ChannelStatsEntry->ChannelRadarNoise = wifihalChannelStatsEntry->ch_radar_noise;
        tr181ChannelStatsEntry->ChannelMax80211Rssi = wifihalChannelStatsEntry->ch_max_80211_rssi;
        tr181ChannelStatsEntry->ChannelNon80211Noise = wifihalChannelStatsEntry->ch_non_80211_noise;
        tr181ChannelStatsEntry->ChannelUtilization = wifihalChannelStatsEntry->ch_utilization;
        snprintf((char *)tr181ChannelStatsEntry->UtilizationTotal, 19, "%llu", wifihalChannelStatsEntry->ch_utilization_total);
        snprintf((char *)tr181ChannelStatsEntry->UtilizationBusy, 19, "%llu", wifihalChannelStatsEntry->ch_utilization_busy);
        snprintf((char *)tr181ChannelStatsEntry->UtilizationTxBusy, 19, "%llu", wifihalChannelStatsEntry->ch_utilization_busy_tx);
        snprintf((char *)tr181ChannelStatsEntry->UtilizationRxBusy, 19, "%llu", wifihalChannelStatsEntry->ch_utilization_busy_rx);
        snprintf((char *)tr181ChannelStatsEntry->UtilizationBusySelf, 19, "%llu", wifihalChannelStatsEntry->ch_utilization_busy_self);
        snprintf((char *)tr181ChannelStatsEntry->UtilizationBusyExt, 19, "%llu", wifihalChannelStatsEntry->ch_utilization_busy_ext);
    }
    return;
}

ULONG
WifiRadioChannelStats_Synchronize
    (
        ANSC_HANDLE                 hInsContext
    )
{
    PCOSA_DML_WIFI_RADIO            pWifiRadio      = hInsContext;
    PCOSA_DML_WIFI_RADIO_STATS      pWifiRadioStats = &pWifiRadio->Stats;
    PCOSA_DML_WIFI_RADIO_FULL       pWifiRadioFull = &pWifiRadio->Radio;
    PCOSA_DML_WIFI_RADIO_CFG        pWifiRadioCfg  = &pWifiRadioFull->Cfg;

    int i = 0;
    int ret = 0;
    unsigned int channelCount = 0;
    wifi_channelStats_t *pChannelStats = NULL;
    char channelsInUse[512] = {0};
    PANSC_TOKEN_CHAIN pTokenChain = (PANSC_TOKEN_CHAIN)NULL;
    PANSC_STRING_TOKEN pToken = (PANSC_STRING_TOKEN)NULL;

    if (NULL == pWifiRadioStats)
    {
        return -1;
    }

    // Free previously allocated entries
    if (pWifiRadioStats->ChannelStats)
    {
        AnscFreeMemory(pWifiRadioStats->ChannelStats);
        pWifiRadioStats->ChannelStats = NULL;
    }
    pWifiRadioStats->ChannelCount = 0;

    ret = wifi_getRadioChannelsInUse(pWifiRadioCfg->InstanceNumber - 1, channelsInUse);
    if (ret == 0)
    {
        pTokenChain = (PANSC_TOKEN_CHAIN)AnscTcAllocate(channelsInUse, ",");
        if (pTokenChain)
        {
            channelCount = AnscTcGetTokenCount(pTokenChain);
            pChannelStats = (wifi_channelStats_t *)AnscAllocateMemory(sizeof(wifi_channelStats_t)*channelCount);
            if(pChannelStats == NULL) {
               AnscTcFree((ANSC_HANDLE)pTokenChain);
               return 0;
            }
            if (pChannelStats)
            {
                for (i = 0; i < channelCount; i++)
                {
                    pChannelStats[i].ch_in_pool = false;
                    pToken = AnscTcUnlinkToken(pTokenChain);
                    if (pToken)
                    {
                        pChannelStats[i].ch_number = atoi(pToken->Name);
                        pChannelStats[i].ch_in_pool = true;
                        AnscFreeMemory(pToken);
                        pToken = NULL;
                    }
                }
            }
            AnscTcFree((ANSC_HANDLE)pTokenChain);
        }
        ret = wifi_getRadioChannelStats(pWifiRadioCfg->InstanceNumber - 1, pChannelStats, channelCount);
        if (ret != 0 && pChannelStats != NULL)
        {
            AnscFreeMemory(pChannelStats);
            pChannelStats = NULL;
            channelCount = 0;
        }
    }

    if ((ret == 0) && channelCount)
    {
        pWifiRadioStats->ChannelStats = (PCOSA_DML_CHANNEL_STATS)AnscAllocateMemory(sizeof(COSA_DML_CHANNEL_STATS)*(channelCount));

        if ( pWifiRadioStats->ChannelStats )
        {
            for (i = 0; i < channelCount; i++)
            {
                copyChannelStats(&pWifiRadioStats->ChannelStats[i], &pChannelStats[i]);
            }
            pWifiRadioStats->ChannelCount = channelCount;
        }
        else
        {
            fprintf(stderr, "WifiRadioChannelStats_Synchronize: memory allocation failed\n");
        }
    }
    else
    {
        fprintf(stderr, "WifiRadioChannelStats_Synchronize: wifi_getRadioChannelStats failed\n");
    }
    if (pChannelStats)
    {
        AnscFreeMemory(pChannelStats);
        pChannelStats = NULL;
    }

    return 0;
}

#define REFRESH_INTERVAL 10
BOOL
WifiRadioChannelStats_IsUpdated
    (
        ANSC_HANDLE                 hInsContext
    )
{
    PCOSA_DML_WIFI_RADIO            pWifiRadio      = hInsContext;
    PCOSA_DML_WIFI_RADIO_STATS      pWifiRadioStats = &pWifiRadio->Stats;

    if ( NULL == pWifiRadioStats )
    {
        return FALSE;
    }

    if ( !pWifiRadioStats->ChannelStatsUpdateTime )
    {
        pWifiRadioStats->ChannelStatsUpdateTime = AnscGetTickInSeconds();
        return TRUE;
    }

    if ( ( AnscGetTickInSeconds() - pWifiRadioStats->ChannelStatsUpdateTime ) < REFRESH_INTERVAL )
    {
        return FALSE;
    }
    else
    {
        pWifiRadioStats->ChannelStatsUpdateTime =  AnscGetTickInSeconds();
        return TRUE;
    }
    return TRUE;
}

BOOL
WifiRadioChannelStats_GetParamBoolValue
    (
        ANSC_HANDLE                 hInsContext,
        char*                       ParamName,
        BOOL                        *pBool
    )
{
    PCOSA_DML_CHANNEL_STATS pChannelStats = hInsContext;

    /* check the parameter name and return the corresponding value */
    if (strcmp(ParamName, "ChannelInPool") == 0)
    {
        *pBool = pChannelStats->ChannelInPool;
        return TRUE;
    }
    if (strcmp(ParamName, "ChannelRadarNoise") == 0)
    {
        *pBool = pChannelStats->ChannelRadarNoise;
        return TRUE;
    }
    return FALSE;
}

BOOL
WifiRadioChannelStats_GetParamIntValue
    (
        ANSC_HANDLE                 hInsContext,
        char*                       ParamName,
        INT*                        pInt
    )
{
    PCOSA_DML_CHANNEL_STATS pChannelStats = hInsContext;

    if ((NULL == ParamName) || (NULL == pInt))
    {
        return FALSE;
    }

    /* check the parameter name and return the corresponding value */
    if (strcmp(ParamName, "ChannelNumber") == 0)
    {
        *pInt = pChannelStats->ChannelNumber;
        return TRUE;
    }
    if (strcmp(ParamName, "ChannelNoise") == 0)
    {
        *pInt = pChannelStats->ChannelNoise;
        return TRUE;
    }
    if (strcmp(ParamName, "ChannelMax80211Rssi") == 0)
    {
        *pInt = pChannelStats->ChannelMax80211Rssi;
        return TRUE;
    }
    if (strcmp(ParamName, "ChannelNon80211Noise") == 0)
    {
        *pInt = pChannelStats->ChannelNon80211Noise;
        return TRUE;
    }
    if (strcmp(ParamName, "ChannelUtilization") == 0)
    {
        *pInt = pChannelStats->ChannelUtilization;
        return TRUE;
    }

    return FALSE;
}

ULONG
WifiRadioChannelStats_GetParamStringValue
    (
        ANSC_HANDLE                 hInsContext,
        char*                       ParamName,
        char*                       pValue,
        ULONG*                      pulSize
    )
{
    PCOSA_DML_CHANNEL_STATS pChannelStats = hInsContext;

    if (strcmp(ParamName, "UtilizationTotal") == 0)
    {
        size_t len = AnscSizeOfString((const char *)pChannelStats->UtilizationTotal) + 1;
        if (*pulSize < len)
        {
            *pulSize = len;
            return 1;
        }

        AnscCopyString(pValue, (char *) pChannelStats->UtilizationTotal);
        return 0;
    }
    if (strcmp(ParamName, "UtilizationBusy") == 0)
    {
        size_t len = AnscSizeOfString((const char *)pChannelStats->UtilizationBusy) + 1;
        if (*pulSize < len)
        {
            *pulSize = len;
            return 1;
        }

        AnscCopyString(pValue, (char *) pChannelStats->UtilizationBusy);
        return 0;
    }
    if (strcmp(ParamName, "UtilizationTxBusy") == 0)
    {
        size_t len = AnscSizeOfString((const char *)pChannelStats->UtilizationTxBusy) + 1;
        if (*pulSize < len)
        {
            *pulSize = len;
            return 1;
        }

        AnscCopyString(pValue, (char *) pChannelStats->UtilizationTxBusy);
        return 0;
    }
    if (strcmp(ParamName, "UtilizationRxBusy") == 0)
    {
        size_t len = AnscSizeOfString((const char *)pChannelStats->UtilizationRxBusy) + 1;
        if (*pulSize < len)
        {
            *pulSize = len;
            return 1;
        }

        AnscCopyString(pValue, (char *) pChannelStats->UtilizationRxBusy);
        return 0;
    }
    if (strcmp(ParamName, "UtilizationBusySelf") == 0)
    {
        size_t len = AnscSizeOfString((const char *)pChannelStats->UtilizationBusySelf) + 1;
        if (*pulSize < len)
        {
            *pulSize = len;
            return 1;
        }

        AnscCopyString(pValue, (char *) pChannelStats->UtilizationBusySelf);
        return 0;
    }
    if (strcmp(ParamName, "UtilizationBusyExt") == 0)
    {
        size_t len = AnscSizeOfString((const char *)pChannelStats->UtilizationBusyExt) + 1;
        if (*pulSize < len)
        {
            *pulSize = len;
            return 1;
        }

        AnscCopyString(pValue, (char *) pChannelStats->UtilizationBusyExt);
        return 0;
    }

    return -1;
}

