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

#ifndef  _COSA_LGI_WIFI_STATS_DML_H_
#define  _COSA_LGI_WIFI_STATS_DML_H_

#define ULL_STR_MAX_LEN 20
typedef  struct
_COSA_DML_CHANNEL_STATS
{
    BOOL                                  ChannelInPool;
    BOOL                                  ChannelRadarNoise;
    INT                                   ChannelNumber;
    INT                                   ChannelNoise;
    INT                                   ChannelMax80211Rssi;
    INT                                   ChannelNon80211Noise;
    INT                                   ChannelUtilization;
    UCHAR                                 UtilizationTotal[ULL_STR_MAX_LEN];
    UCHAR                                 UtilizationBusy[ULL_STR_MAX_LEN];
    UCHAR                                 UtilizationTxBusy[ULL_STR_MAX_LEN];
    UCHAR                                 UtilizationRxBusy[ULL_STR_MAX_LEN];
    UCHAR                                 UtilizationBusySelf[ULL_STR_MAX_LEN];
    UCHAR                                 UtilizationBusyExt[ULL_STR_MAX_LEN];
}
COSA_DML_CHANNEL_STATS, *PCOSA_DML_CHANNEL_STATS;

ULONG WifiRadioChannelStats_GetEntryCount(ANSC_HANDLE hInsContext);

ANSC_HANDLE WifiRadioChannelStats_GetEntry(ANSC_HANDLE hInsContext, ULONG nIndex, ULONG* pInsNumber);

ULONG WifiRadioChannelStats_Synchronize(ANSC_HANDLE hInsContext);

BOOL WifiRadioChannelStats_IsUpdated(ANSC_HANDLE hInsContext);

BOOL WifiRadioChannelStats_GetParamBoolValue(ANSC_HANDLE hInsContext, char* ParamName, BOOL* pBool);

BOOL WifiRadioChannelStats_GetParamIntValue(ANSC_HANDLE hInsContext, char* ParamName, INT* pInt);

ULONG WifiRadioChannelStats_GetParamStringValue(ANSC_HANDLE hInsContext, char* ParamName, char* pValue, ULONG* pulSize);

#endif
