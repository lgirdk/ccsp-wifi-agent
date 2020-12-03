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

#ifndef  _COSA_WIFI_RADIUS_DML_H_
#define  _COSA_WIFI_RADIUS_DML_H_

typedef  struct
_COSA_DML_LG_WIFI_RADIUS
{
    ULONG                           TransportInterface;
}
COSA_DML_LG_WIFI_RADIUS,  *PCOSA_DML_LG_WIFI_RADIUS;

BOOL
LGI_Radius_GetParamUlongValue
    (
        ANSC_HANDLE                 hInsContext,
        char*                       ParamName,
        ULONG*                      pUlong
    );

BOOL
LGI_Radius_SetParamUlongValue
    (
        ANSC_HANDLE                 hInsContext,
        char*                       ParamName,
        ULONG                       uValuepUlong
    );

BOOL
LGI_Radius_Validate
    (
        ANSC_HANDLE                 hInsContext,
        char*                       pReturnParamName,
        ULONG*                      puLength
  );


ULONG
LGI_Radius_Commit
    (
        ANSC_HANDLE                 hInsContext
    );

ULONG
LGI_Radius_Rollback
    (
        ANSC_HANDLE                 hInsContext
    );

#endif
