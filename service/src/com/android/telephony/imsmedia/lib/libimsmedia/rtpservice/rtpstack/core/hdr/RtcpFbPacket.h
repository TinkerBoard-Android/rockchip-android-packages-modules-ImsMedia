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

#ifndef __RTCP_FB_PACKET_H__
#define __RTCP_FB_PACKET_H__

#include <rtp_global.h>
#include <RtpBuffer.h>
#include <RtcpHeader.h>
#include <RtpList.h>


typedef enum eRCTFBType
{
    FEEDBACK_NONE = 0,

    // RTP FB
    RTPFBF_NACK = 1,
    RTPFBF_TMMBR = 3,
    RTPFBF_TMMBN = 4,

    // Payload specific FB
    PSFB_BOUNDARY = 10,

    PSFB_PLI = 11,
    PSFB_SLI = 12,
    PSFB_RPSI = 13,
    PSFB_FIR = 14,
    PSFB_TSTR = 15,
    PSFB_TSTN = 16,
    PSFB_VBCM = 17,

    FB_FEEDBACK_MAX

}eRCTFBType_t;

/**
* @class    RtcpFbPacket
* @brief    It holds RTCP Feedback Message information
*
*/
class RtcpFbPacket
{
    private:
        // RTCP Fb header information
        RtcpHeader m_objRtcpHdr;

        // Fb payload type
        eRTCP_TYPE m_ePayloadType;

        // Media SSRC
        RtpDt_UInt32 m_uiMediaSsrc;

        // Feedback Control Information
        RtpBuffer *m_pFCI;

        /**
         * Performs the encoding of the RTCP RTP FB packet.
         * This function does not allocate memory required for encoding.
         * @param[out] pobjRtcpPktBuf Memory for the buffer is pre-allocated by caller
         * @return RTP_SUCCESS on successful encoding
         */
        eRTP_STATUS_CODE formRtcpRtpFbPacket(OUT RtpBuffer* pobjRtcpPktBuf);

        /**
         * Performs the encoding of the RTCP payload FB packet.
         * This function does not allocate memory required for encoding.
         * @param[out] pobjRtcpPktBuf Memory for the buffer is pre-allocated by caller
         * @return RTP_SUCCESS on successful encoding
         */
        eRTP_STATUS_CODE formRtcpPayloadFbPacket(OUT RtpBuffer* pobjRtcpPktBuf);

    public:

        RtcpFbPacket();

        ~RtcpFbPacket();

        /**
         * get method for m_objRtcpHdr
         */
        RtcpHeader* getRtcpHdrInfo();

        /**
         * set method for SSRC
         */
        RtpDt_Void setSsrc(RtpDt_UInt32);

        /**
         * set method for Media SSRC
         */
        RtpDt_Void setMediaSsrc(RtpDt_UInt32);

        /**
         * get method for SSRC
         */
        RtpDt_UInt32 getSsrc();

        /**
         * get method for Media SSRC
         */
        RtpDt_UInt32 getMediaSsrc();

        /**
         * get method for FCI info from the RTCP packet
         */
        RtpBuffer* getFCI();

        /**
         * set method for  FCI info from the RTCP packet
         */
        RtpDt_Void setFCI(IN RtpBuffer* m_pFCI);

        /**
         * get method for Feedback payload type info from the RTCP packet
         */
        eRTCP_TYPE getPayloadType();

        /**
         * set method for  Feedback payload type info from the RTCP packet
         */
        RtpDt_Void setPayloadType(IN eRTCP_TYPE ePayloadType);

        /**
         * Decodes and stores the information of the RTCP FB packet
         * This function does not allocate memory required for decoding.
         *
         * @param pucByeBuf received RTCP FB packet
         * @param usByeLen length of the RTCP FB packet
         */
        eRTP_STATUS_CODE decodeRtcpFbPacket(IN RtpDt_UChar* ,
                                     IN RtpDt_UInt16 , IN RtpDt_UChar ucPktType);

        /**
         * Performs the encoding of the RTCP FB packet.
         * This function does not allocate memory required for encoding.
         * @param[out] pobjRtcpPktBuf Memory for the buffer is pre-allocated by caller
         * @return RTP_SUCCESS on successful encoding
        */
        eRTP_STATUS_CODE formRtcpFbPacket(OUT RtpBuffer* pobjRtcpPktBuf);

}; // end of RtcpFbPacket


#endif    //__RTCP_FB_PACKET_H__


/** @}*/
