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

#ifndef  _COSA_LGI_WIFI_BANDSTEERING_DML_H_
#define  _COSA_LGI_WIFI_BANDSTEERING_DML_H_

typedef struct
_COSA_DML_BANDSTEERING_SSID_CAPABLE5G_ENTRY
{
    char    MacAddress[18];
    char    EntryTime[32];
}
COSA_DML_BANDSTEERING_SSID_CAPABLE5G_ENTRY, *PCOSA_DML_BANDSTEERING_SSID_CAPABLE5G_ENTRY;

typedef struct
_COSA_DML_BANDSTEERING_SSID_BLACKLIST24G_ENTRY
{
    char    MacAddress[18];
    char    EntryTime[32];
    UINT    TimeRemaining;
}
COSA_DML_BANDSTEERING_SSID_BLACKLIST24G_ENTRY, *PCOSA_DML_BANDSTEERING_SSID_BLACKLIST24G_ENTRY;


typedef  struct
_COSA_DML_BANDSTEERING_SSID
{
    INT     ifIndex;
    BOOL    Enable;
    BOOL    Active;
    BOOL    ClearCapable5G;
    BOOL    ClearBlacklist24G;
    INT     RSSIThreshold;
    UINT    DeltaThreshold;
    UINT    BlacklistTimeout;
    ULONG   pCapable5GEntryCount;
    PCOSA_DML_BANDSTEERING_SSID_CAPABLE5G_ENTRY pCapable5G;
    ULONG   pBlacklist24GEntryCount;
    PCOSA_DML_BANDSTEERING_SSID_BLACKLIST24G_ENTRY pBlacklist24G;
}
COSA_DML_BANDSTEERING_SSID,  *PCOSA_DML_BANDSTEERING_SSID;

#endif
