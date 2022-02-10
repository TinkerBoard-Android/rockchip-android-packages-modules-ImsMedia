/**
 * Copyright (C) 2022 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

/** \addtogroup  RTP_Stack
 *  @{
 */

#ifndef __RTCP_XR_PACKET_H__
#define __RTCP_XR_PACKET_H__

#include <rtp_global.h>
#include <RtpBuffer.h>
#include <RtcpHeader.h>

/**
 * @class   RtcpXrPacket
 * @brief   It holds RTCP XR packet information
 */
class RtcpXrPacket
{
    private:
        // RTCP Xr header information
        RtcpHeader m_objRtcpHdr; // making use of RTCP header

        // round trip time delay
        RtpDt_UInt32 m_uiRTTD;

        //RTTD offset
        RtpDt_UInt16 m_uiRttdOffset;

        // Extended report block Information
        RtpBuffer *m_reportBlk;

    public:

        RtcpXrPacket();

        ~RtcpXrPacket();

        /**
         * get method for m_objRtcpHdr
         */
        RtcpHeader* getRtcpHdrInfo();

        /**
         * set RTTD method
         */
        RtpDt_Void setRTTD(RtpDt_UInt32 rttd);

        /**
         * get RTTD method
         */
        RtpDt_UInt32 getRTTD();

        RtpDt_Void setRttdOffset(RtpDt_UInt16 rttdoffset);

        RtpDt_UInt16 getRttdOffset();

        /**
         * get method for report block for the RTCP XR packet
         */
        RtpBuffer* getReportBlk();

        /**
         * set method for  report block for the RTCP XR packet
         */
        RtpDt_Void setReportBlk(IN RtpBuffer* m_reportBlk);

        /**
         * Decodes and stores the information of the RTCP XR packet
         * This function does not allocate memory required for decoding.
         * @param[in] pucByeBuf received RTCP XR packet
         * @param[in] usByeLen length of the RTCP XR packet
         * @return RTP_SUCCESS on successful decoding
         */
        eRTP_STATUS_CODE decodeRtcpXrPacket(IN RtpDt_UChar* ,
                                     IN RtpDt_UInt16 , IN RtpDt_UChar ucPktType);

        /**
         * Performs the encoding of the RTCP XR packet.
         * This function does not allocate memory required for encoding.
         * @param[out] pobjRtcpPktBuf Memory for the buffer is pre-allocated by caller
         * @return RTP_SUCCESS on successful encoding
         */
        eRTP_STATUS_CODE formRtcpXrPacket(OUT RtpBuffer* pobjRtcpPktBuf);

}; // end of RtcpXrPacket

#endif    //__RTCP_XR_PACKET_H__

/** @}*/
