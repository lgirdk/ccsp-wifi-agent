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

#ifndef  _COSA_LGI_WIFI_SOFTBLOCK_DML_H_
#define  _COSA_LGI_WIFI_SOFTBLOCK_DML_H_

#define SOFTBLOCK_SSID_STR_LEN     17
#define SOFTBLOCK_WIFI_SSID_NUM    16
#define SOFTBLOCK_MAX_CLIENT_NUM   64

typedef  struct
_COSA_DML_SOFTBLOCKING_CLIENT
{
    char    MacAddress[18];
    char    LastAssocTime[32];
}
COSA_DML_SOFTBLOCKING_CLIENT,  *PCOSA_DML_SOFTBLOCKING_CLIENT;

#endif

