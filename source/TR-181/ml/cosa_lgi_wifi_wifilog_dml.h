/*********************************************************************
 * Copyright 2017-2019 ARRIS Enterprises, LLC.
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

#ifndef  _COSA_LGI_WIFI_WIFILOG_DML_H_
#define  _COSA_LGI_WIFI_WIFILOG_DML_H_
#define TIME_NO_NEGATIVE(x) ((long)(x) < 0 ? 0 : (x))
#define LGI_WIFI_EVENT_LOG_TIME_LEN 64
#define LGI_WIFI_EVENT_LOG_INFO_LEN 512
#define LGI_WIFI_EVENT_LOG_CHANNEL_LEN 10

typedef  struct
_COSA_DML_WIFILOG_FULL
{
    ULONG                           Index;
    ULONG                           EventID;
    ULONG                           EventLevel;
    CHAR                            Time[LGI_WIFI_EVENT_LOG_TIME_LEN];
    CHAR                            Description[LGI_WIFI_EVENT_LOG_INFO_LEN];
}
COSA_DML_WIFILOG_FULL, *PCOSA_DML_WIFILOG_FULL;

typedef  struct
_COSA_DML_WIFI_CH_CHANGE_LOG_FULL
{
    ULONG                           Index;
    ULONG                           Channel;
    CHAR                            Time[LGI_WIFI_EVENT_LOG_TIME_LEN];
    CHAR                            Reason[LGI_WIFI_EVENT_LOG_INFO_LEN];
}
COSA_DML_WIFI_CH_CHANGE_LOG_FULL, *PCOSA_DML_WIFI_CH_CHANGE_LOG_FULL;

BOOL
X_LGI_COM_WiFiLog_GetParamUlongValue
    (
        ANSC_HANDLE                 hInsContext,
        char*                       ParamName,
        ULONG*                      puLong
    );
BOOL
X_LGI_COM_WiFiLog_SetParamUlongValue
    (
        ANSC_HANDLE                 hInsContext,
        char*                       ParamName,
        ULONG                       uValue
    );
ULONG
WiFiLog_GetEntryCount
    (
        ANSC_HANDLE                 hInsContext
    );
ANSC_HANDLE
WiFiLog_GetEntry
    (
        ANSC_HANDLE                 hInsContext,
        ULONG                       nIndex,
        ULONG*                      pInsNumber
    );
BOOL
WiFiLog_IsUpdated
    (
        ANSC_HANDLE                 hInsContext
    );
ULONG
WiFiLog_Synchronize
    (
        ANSC_HANDLE                 hInsContext
    );
BOOL
WiFiLog_GetParamUlongValue
    (
        ANSC_HANDLE                 hInsContext,
        char*                       ParamName,
        ULONG*                      puLong
    );
ULONG
WiFiLog_GetParamStringValue
    (
        ANSC_HANDLE                 hInsContext,
        char*                       ParamName,
        char*                       pValue,
        ULONG*                      pUlSize
    );

ULONG
WiFiChannelChangeLog_GetEntryCount
    (
        ANSC_HANDLE                 hInsContext
    );
ANSC_HANDLE
WiFiChannelChangeLog_GetEntry
    (
        ANSC_HANDLE                 hInsContext,
        ULONG                       nIndex,
        ULONG*                      pInsNumber
    );
BOOL
WiFiChannelChangeLog_IsUpdated
    (
        ANSC_HANDLE                 hInsContext
    );
ULONG
WiFiChannelChangeLog_Synchronize
    (
        ANSC_HANDLE                 hInsContext
    );
BOOL
WiFiChannelChangeLog_GetParamUlongValue
    (
        ANSC_HANDLE                 hInsContext,
        char*                       ParamName,
        ULONG*                      puLong
    );
ULONG
WiFiChannelChangeLog_GetParamStringValue
    (
        ANSC_HANDLE                 hInsContext,
        char*                       ParamName,
        char*                       pValue,
        ULONG*                      pUlSize
    );
#endif

