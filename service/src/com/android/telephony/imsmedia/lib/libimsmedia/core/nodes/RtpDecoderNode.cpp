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

#include <RtpDecoderNode.h>
#include <ImsMediaTrace.h>
#include <ImsMediaCondition.h>
#include <AudioConfig.h>
#include <VideoConfig.h>
#include <TextConfig.h>

#if defined(DEBUG_JITTER_GEN_SIMULATION_DELAY) || defined(DEBUG_JITTER_GEN_SIMULATION_REORDER) || \
        defined(DEBUG_JITTER_GEN_SIMULATION_LOSS)
#include <ImsMediaTimer.h>
#endif
#ifdef DEBUG_JITTER_GEN_SIMULATION_DELAY
#define DEBUG_JITTER_MAX_PACKET_INTERVAL 15  // milliseconds
#endif
#ifdef DEBUG_JITTER_GEN_SIMULATION_REORDER
#include <ImsMediaDataQueue.h>
#define DEBUG_JITTER_REORDER_MAX 4
#define DEBUG_JITTER_REORDER_MIN 4
#define DEBUG_JITTER_NORMAL      2
#endif
#ifdef DEBUG_JITTER_GEN_SIMULATION_LOSS
#define DEBUG_JITTER_LOSS_PACKET_INTERVAL 20
#endif
#ifdef DEBUG_JITTER_GEN_SIMULATION_DUPLICATE
#define DEBUG_JITTER_DUPLICATE_PACKET_INTERVAL 30
#endif

RtpDecoderNode::RtpDecoderNode(BaseSessionCallback* callback) :
        BaseNode(callback)
{
    mRtpSession = nullptr;
    mReceivingSSRC = 0;
    mInactivityTime = 0;
    mNoRtpTime = 0;
    mRtpPayloadTx = 0;
    mRtpPayloadRx = 0;
    mRtpTxDtmfPayload = 0;
    mRtpRxDtmfPayload = 0;
    mDtmfSamplingRate = 0;
    mCvoValue = CVO_DEFINE_NONE;
    mRedundantPayload = 0;
    mArrivalTime = 0;
    mSubtype = MEDIASUBTYPE_UNDEFINED;
#if defined(DEBUG_JITTER_GEN_SIMULATION_LOSS) || defined(DEBUG_JITTER_GEN_SIMULATION_DUPLICATE)
    mPacketCounter = 1;
#endif
#ifdef DEBUG_JITTER_GEN_SIMULATION_DELAY
    mNextTime = 0;
#endif
#ifdef DEBUG_JITTER_GEN_SIMULATION_REORDER
    mReorderDataCount = 0;
#endif
}

RtpDecoderNode::~RtpDecoderNode()
{
    // remove IRtpSession here to avoid shared instance in other node from unable to use
    if (mRtpSession)
    {
        mRtpSession->StopRtp();
        mRtpSession->SetRtpDecoderListener(nullptr);
        IRtpSession::ReleaseInstance(mRtpSession);
        mRtpSession = nullptr;
    }
}

kBaseNodeId RtpDecoderNode::GetNodeId()
{
    return kNodeIdRtpDecoder;
}

ImsMediaResult RtpDecoderNode::Start()
{
    IMLOGD1("[Start] type[%d]", mMediaType);

    if (mRtpPayloadTx == 0 || mRtpPayloadRx == 0)
    {
        IMLOGE0("[Start] invalid payload number");
        return RESULT_INVALID_PARAM;
    }

    if (mRtpSession == nullptr)
    {
        mRtpSession = IRtpSession::GetInstance(mMediaType, mLocalAddress, mPeerAddress);

        if (mRtpSession == nullptr)
        {
            IMLOGE0("[Start] - Can't create rtp session");
            return RESULT_NOT_READY;
        }
    }

    if (mMediaType == IMS_MEDIA_AUDIO)
    {
        mRtpSession->SetRtpPayloadParam(mRtpPayloadTx, mRtpPayloadRx, mSamplingRate * 1000,
                mRtpTxDtmfPayload, mRtpRxDtmfPayload, mDtmfSamplingRate * 1000);
    }
    else if (mMediaType == IMS_MEDIA_VIDEO)
    {
        mRtpSession->SetRtpPayloadParam(mRtpPayloadTx, mRtpPayloadRx, mSamplingRate * 1000);
    }
    else if (mMediaType == IMS_MEDIA_TEXT)
    {
        if (mRedundantPayload > 0)
        {
            mRtpSession->SetRtpPayloadParam(mRtpPayloadTx, mRtpPayloadRx, mSamplingRate * 1000,
                    mRedundantPayload, mSamplingRate * 1000);
        }
        else
        {
            mRtpSession->SetRtpPayloadParam(mRtpPayloadTx, mRtpPayloadRx, mSamplingRate * 1000);
        }
    }

    mRtpSession->SetRtpDecoderListener(this);
    mRtpSession->StartRtp();
    mReceivingSSRC = 0;
    mNoRtpTime = 0;
    mSubtype = MEDIASUBTYPE_UNDEFINED;
    mNodeState = kNodeStateRunning;
#if defined(DEBUG_JITTER_GEN_SIMULATION_LOSS) || defined(DEBUG_JITTER_GEN_SIMULATION_DUPLICATE)
    mPacketCounter = 1;
#endif
    return RESULT_SUCCESS;
}

void RtpDecoderNode::Stop()
{
    IMLOGD1("[Stop] type[%d]", mMediaType);

    mReceivingSSRC = 0;

    if (mRtpSession)
    {
        mRtpSession->StopRtp();
    }

    mNodeState = kNodeStateStopped;
}

void RtpDecoderNode::OnDataFromFrontNode(ImsMediaSubType subtype, uint8_t* data, uint32_t datasize,
        uint32_t timestamp, bool mark, uint32_t seq, ImsMediaSubType nDataType,
        uint32_t arrivalTime)
{
    IMLOGD_PACKET8(IM_PACKET_LOG_RTP,
            "[OnDataFromFrontNode] media[%d], subtype[%d] Size[%d], TS[%d], Mark[%d], Seq[%d], "
            "datatype[%d], arrivalTime[%u]",
            mMediaType, subtype, datasize, timestamp, mark, seq, nDataType, arrivalTime);

    mArrivalTime = arrivalTime;
#ifdef DEBUG_JITTER_GEN_SIMULATION_DELAY
    {
        ImsMediaCondition condition;
        uint32_t delay = ImsMediaTimer::GenerateRandom(DEBUG_JITTER_MAX_PACKET_INTERVAL);
        mArrivalTime += delay;
        condition.wait_timeout(delay);
    }
#endif

#if defined(DEBUG_JITTER_GEN_SIMULATION_LOSS) || defined(DEBUG_JITTER_GEN_SIMULATION_DUPLICATE)
    bool flag = false;
#ifdef DEBUG_JITTER_GEN_SIMULATION_LOSS
    uint32_t seed = ImsMediaTimer::GenerateRandom(5);
    if (mPacketCounter % DEBUG_JITTER_LOSS_PACKET_INTERVAL == 0 || seed % 5 == 0)
    {
        flag = true;
    }
#endif
#ifdef DEBUG_JITTER_GEN_SIMULATION_DUPLICATE
    if ((mPacketCounter % DEBUG_JITTER_DUPLICATE_PACKET_INTERVAL) == 0)
    {
        flag = true;
    }
#endif
    mPacketCounter++;
#endif
#ifdef DEBUG_JITTER_GEN_SIMULATION_REORDER
    {
        // add data to jitter gen buffer
        DataEntry entry;
        entry.subtype = MEDIASUBTYPE_RTPPACKET;
        entry.pbBuffer = data;
        entry.nBufferSize = datasize;
        entry.nTimestamp = 0;
        entry.bMark = 0;
        entry.nSeqNum = 0;
        entry.arrivalTime = arrivalTime;

        if (mReorderDataCount < DEBUG_JITTER_NORMAL)
        {
            jitterData.Add(&entry);
        }
        else if (mReorderDataCount < DEBUG_JITTER_NORMAL + DEBUG_JITTER_REORDER_MAX)
        {
            int32_t nCurrReorderSize;
            int32_t nInsertPos;
            uint32_t nCurrJitterBufferSize;
            nCurrJitterBufferSize = jitterData.GetCount();

            if (DEBUG_JITTER_REORDER_MAX > DEBUG_JITTER_REORDER_MIN)
            {
                nCurrReorderSize = mReorderDataCount - DEBUG_JITTER_NORMAL + 1 -
                        ImsMediaTimer::GenerateRandom(
                                DEBUG_JITTER_REORDER_MAX - DEBUG_JITTER_REORDER_MIN + 1);
            }
            else
            {
                nCurrReorderSize = mReorderDataCount - DEBUG_JITTER_NORMAL + 1;
            }

            if (nCurrReorderSize > 0)
            {
                nCurrReorderSize = ImsMediaTimer::GenerateRandom(nCurrReorderSize + 1);
            }

            nInsertPos = nCurrJitterBufferSize - nCurrReorderSize;

            if (nInsertPos < 0)
            {
                nInsertPos = 0;
            }

            jitterData.InsertAt(nInsertPos, &entry);
        }

        mReorderDataCount++;

        if (mReorderDataCount >= DEBUG_JITTER_NORMAL + DEBUG_JITTER_REORDER_MAX)
        {
            mReorderDataCount = 0;
        }

        // send
        while (jitterData.GetCount() >= DEBUG_JITTER_REORDER_MAX)
        {
            DataEntry* pEntry;

            if (jitterData.Get(&pEntry))
            {
#ifdef DEBUG_JITTER_GEN_SIMULATION_LOSS
                if (flag == false)
                {
                    mRtpSession->ProcRtpPacket(pEntry->pbBuffer, pEntry->nBufferSize);
                }
#else
#ifdef DEBUG_JITTER_GEN_SIMULATION_DUPLICATE
                if (flag == true)
                {
                    mRtpSession->ProcRtpPacket(pEntry->pbBuffer, pEntry->nBufferSize);
                }
#endif
                mRtpSession->ProcRtpPacket(pEntry->pbBuffer, pEntry->nBufferSize);
#endif
                jitterData.Delete();
            }
        }
    }
#else
#ifdef DEBUG_JITTER_GEN_SIMULATION_LOSS
    if (flag == false)
    {
        mRtpSession->ProcRtpPacket(data, datasize);
    }
#else
#ifdef DEBUG_JITTER_GEN_SIMULATION_DUPLICATE
    if (flag == true)
    {
        mRtpSession->ProcRtpPacket(data, datasize);
    }
#endif
    mRtpSession->ProcRtpPacket(data, datasize);
#endif
#endif
}

bool RtpDecoderNode::IsRunTime()
{
    return true;
}

bool RtpDecoderNode::IsSourceNode()
{
    return false;
}

void RtpDecoderNode::SetConfig(void* config)
{
    IMLOGD1("[SetConfig] type[%d]", mMediaType);

    if (config == nullptr)
    {
        return;
    }

    if (mMediaType == IMS_MEDIA_AUDIO)
    {
        AudioConfig* pConfig = reinterpret_cast<AudioConfig*>(config);
        mPeerAddress = RtpAddress(pConfig->getRemoteAddress().c_str(), pConfig->getRemotePort());
        mSamplingRate = pConfig->getSamplingRateKHz();
        mRtpPayloadTx = pConfig->getTxPayloadTypeNumber();
        mRtpPayloadRx = pConfig->getRxPayloadTypeNumber();
        mRtpTxDtmfPayload = pConfig->getTxDtmfPayloadTypeNumber();
        mRtpRxDtmfPayload = pConfig->getRxDtmfPayloadTypeNumber();
        mDtmfSamplingRate = pConfig->getDtmfsamplingRateKHz();
    }
    else if (mMediaType == IMS_MEDIA_VIDEO)
    {
        VideoConfig* pConfig = reinterpret_cast<VideoConfig*>(config);
        mPeerAddress = RtpAddress(pConfig->getRemoteAddress().c_str(), pConfig->getRemotePort());
        mSamplingRate = pConfig->getSamplingRateKHz();
        mRtpPayloadTx = pConfig->getTxPayloadTypeNumber();
        mRtpPayloadRx = pConfig->getRxPayloadTypeNumber();
        mCvoValue = pConfig->getCvoValue();
    }
    else if (mMediaType == IMS_MEDIA_TEXT)
    {
        TextConfig* pConfig = reinterpret_cast<TextConfig*>(config);
        mPeerAddress = RtpAddress(pConfig->getRemoteAddress().c_str(), pConfig->getRemotePort());
        mSamplingRate = pConfig->getSamplingRateKHz();
        mRtpPayloadTx = pConfig->getTxPayloadTypeNumber();
        mRtpPayloadRx = pConfig->getRxPayloadTypeNumber();
        mRedundantPayload = pConfig->getRedundantPayload();
    }

    IMLOGD2("[SetConfig] peer Ip[%s], port[%d]", mPeerAddress.ipAddress, mPeerAddress.port);
}

bool RtpDecoderNode::IsSameConfig(void* config)
{
    if (config == nullptr)
    {
        return true;
    }

    if (mMediaType == IMS_MEDIA_AUDIO)
    {
        AudioConfig* pConfig = reinterpret_cast<AudioConfig*>(config);
        return (mPeerAddress ==
                        RtpAddress(pConfig->getRemoteAddress().c_str(), pConfig->getRemotePort()) &&
                mSamplingRate == pConfig->getSamplingRateKHz() &&
                mRtpPayloadTx == pConfig->getTxPayloadTypeNumber() &&
                mRtpPayloadRx == pConfig->getRxPayloadTypeNumber() &&
                mRtpTxDtmfPayload == pConfig->getTxDtmfPayloadTypeNumber() &&
                mRtpRxDtmfPayload == pConfig->getRxDtmfPayloadTypeNumber() &&
                mDtmfSamplingRate == pConfig->getDtmfsamplingRateKHz());
    }
    else if (mMediaType == IMS_MEDIA_VIDEO)
    {
        VideoConfig* pConfig = reinterpret_cast<VideoConfig*>(config);
        return (mPeerAddress ==
                        RtpAddress(pConfig->getRemoteAddress().c_str(), pConfig->getRemotePort()) &&
                mSamplingRate == pConfig->getSamplingRateKHz() &&
                mRtpPayloadTx == pConfig->getTxPayloadTypeNumber() &&
                mRtpPayloadRx == pConfig->getRxPayloadTypeNumber() &&
                mCvoValue == pConfig->getCvoValue());
    }
    else if (mMediaType == IMS_MEDIA_TEXT)
    {
        TextConfig* pConfig = reinterpret_cast<TextConfig*>(config);
        return (mPeerAddress ==
                        RtpAddress(pConfig->getRemoteAddress().c_str(), pConfig->getRemotePort()) &&
                mSamplingRate == pConfig->getSamplingRateKHz() &&
                mRtpPayloadTx == pConfig->getTxPayloadTypeNumber() &&
                mRtpPayloadRx == pConfig->getRxPayloadTypeNumber() &&
                mRedundantPayload == pConfig->getRedundantPayload());
    }

    return false;
}

void RtpDecoderNode::OnMediaDataInd(unsigned char* data, uint32_t datasize, uint32_t timestamp,
        bool mark, uint16_t seq, uint32_t payloadType, uint32_t ssrc, bool extension,
        uint16_t nExtensionData)
{
    IMLOGD_PACKET8(IM_PACKET_LOG_RTP,
            "[OnMediaDataInd] media[%d] size[%d], TS[%d], mark[%d], seq[%d], payloadType[%d] "
            "sampling[%d], ext[%d]",
            mMediaType, datasize, timestamp, mark, seq, payloadType, mSamplingRate, extension);

    if (mMediaType == IMS_MEDIA_AUDIO && mRtpPayloadRx != payloadType &&
            mRtpPayloadTx != payloadType)
    {
        IMLOGE1("[OnMediaDataInd] media[%d] invalid frame", mMediaType);
        return;
    }

    // no need to change to timestamp to milliseconds unit in audio or text packet
    if (mMediaType != IMS_MEDIA_VIDEO && mSamplingRate != 0)
    {
        timestamp = timestamp / (mSamplingRate);
    }

    if (mReceivingSSRC != ssrc)
    {
        IMLOGD3("[OnMediaDataInd] media[%d] SSRC changed, [%x] -> [%x]", mMediaType, mReceivingSSRC,
                ssrc);
        mReceivingSSRC = ssrc;
        SendDataToRearNode(MEDIASUBTYPE_REFRESHED, nullptr, mReceivingSSRC, 0, 0, 0);
    }

    /** TODO : add checking receiving dtmf by the payload type number */
    if (extension && mMediaType == IMS_MEDIA_VIDEO && mCvoValue != CVO_DEFINE_NONE)
    {
        uint16_t nExtensionID;
        uint16_t nCamID;
        uint16_t nRotation;
        nExtensionID = nExtensionData;
        nExtensionID = nExtensionID >> 12;

        nCamID = nExtensionData;  // 0: Front-facing camera, 1: Back-facing camera
        nCamID = nCamID << 12;
        nCamID = nCamID >> 15;

        nRotation = nExtensionData;
        nRotation = nRotation << 13;
        nRotation = nRotation >> 13;

        switch (nRotation)
        {
            case 0:  // No rotation (Rotated 0CW/CCW = To rotate 0CW/CCW)
            case 4:  // + Horizontal Flip, but it's treated as same as above
                mSubtype = MEDIASUBTYPE_ROT0;
                break;
            case 1:  // Rotated 270CW(90CCW) = To rotate 90CW(270CCW)
            case 5:  // + Horizontal Flip, but it's treated as same as above
                mSubtype = MEDIASUBTYPE_ROT90;
                break;
            case 2:  // Rotated 180CW = To rotate 180CW
            case 6:  // + Horizontal Flip, but it's treated as same as above
                mSubtype = MEDIASUBTYPE_ROT180;
                break;
            case 3:  // Rotated 90CW(270CCW) = To rotate 270CW(90CCW)
            case 7:  // + Horizontal Flip, but it's treated as same as above
                mSubtype = MEDIASUBTYPE_ROT270;
                break;
            default:
                break;
        }

        IMLOGD4("[OnMediaDataInd] extensionId[%d], camId[%d], rot[%d], subtype[%d]", nExtensionID,
                nCamID, nRotation, mSubtype);
    }

    if (mMediaType == IMS_MEDIA_TEXT)
    {
        if (payloadType == mRtpPayloadTx)
        {
            if (mRedundantPayload == 0)
            {
                mSubtype = MEDIASUBTYPE_BITSTREAM_T140;
            }
            else
            {
                mSubtype = MEDIASUBTYPE_BITSTREAM_T140_RED;
            }
        }
        else if (payloadType == mRedundantPayload)
        {
            mSubtype = MEDIASUBTYPE_BITSTREAM_T140;
        }
        else
        {
            IMLOGI2("[OnMediaDataInd] media[%d] INVALID payload[%d] is received", mMediaType,
                    payloadType);
        }
    }

    SendDataToRearNode(
            mSubtype, data, datasize, timestamp, mark, seq, MEDIASUBTYPE_UNDEFINED, mArrivalTime);
}

void RtpDecoderNode::OnNumReceivedPacket(uint32_t nNumRtpPacket)
{
    IMLOGD_PACKET2(IM_PACKET_LOG_RTP, "[OnNumReceivedPacket] InactivityTime[%d], numRtp[%d]",
            mInactivityTime, nNumRtpPacket);

    if (nNumRtpPacket == 0)
    {
        mNoRtpTime++;
    }
    else
    {
        mNoRtpTime = 0;
    }

    if (mInactivityTime != 0 && mNoRtpTime == mInactivityTime)
    {
        if (mCallback != nullptr)
        {
            mCallback->SendEvent(kImsMediaEventMediaInactivity, kProtocolRtp, mInactivityTime);
        }
    }
}

void RtpDecoderNode::SetLocalAddress(const RtpAddress& address)
{
    mLocalAddress = address;
}

void RtpDecoderNode::SetPeerAddress(const RtpAddress& address)
{
    mPeerAddress = address;
}

void RtpDecoderNode::SetInactivityTimerSec(const uint32_t time)
{
    IMLOGD2("[SetInactivityTimerSec] media[%d], time[%d] reset", mMediaType, time);
    mInactivityTime = time;
    mNoRtpTime = 0;
}
