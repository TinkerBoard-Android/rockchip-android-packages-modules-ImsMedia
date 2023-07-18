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

#include <IAudioPlayerNode.h>
#include <ImsMediaAudioPlayer.h>
#include <ImsMediaDefine.h>
#include <ImsMediaTrace.h>
#include <ImsMediaTimer.h>
#include <ImsMediaAudioUtil.h>
#include <AudioConfig.h>
#include <RtpConfig.h>
#include <string.h>

#define MAX_CODEC_EVS_AMR_IO_MODE 9

IAudioPlayerNode::IAudioPlayerNode(BaseSessionCallback* callback) :
        JitterBufferControlNode(callback, IMS_MEDIA_AUDIO)
{
    std::unique_ptr<ImsMediaAudioPlayer> track(new ImsMediaAudioPlayer());
    mAudioPlayer = std::move(track);
    mConfig = nullptr;
    mIsOctetAligned = false;
    mIsDtxEnabled = false;
}

IAudioPlayerNode::~IAudioPlayerNode()
{
    if (mConfig != nullptr)
    {
        delete mConfig;
    }
}

kBaseNodeId IAudioPlayerNode::GetNodeId()
{
    return kNodeIdAudioPlayer;
}

ImsMediaResult IAudioPlayerNode::ProcessStart()
{
    IMLOGD2("[ProcessStart] codec[%d], mode[%d]", mCodecType, mMode);
    if (mJitterBuffer)
    {
        mJitterBuffer->SetCodecType(mCodecType);
    }

    // reset the jitter
    Reset();

    if (mAudioPlayer)
    {
        mAudioPlayer->SetCodec(mCodecType);
        mAudioPlayer->SetSamplingRate(mSamplingRate * 1000);
        mAudioPlayer->SetDtxEnabled(mIsDtxEnabled);
        mAudioPlayer->SetOctetAligned(mIsOctetAligned);
        int mode = (mCodecType == kAudioCodecEvs) ? ImsMediaAudioUtil::GetMaximumEvsMode(mMode)
                                                  : ImsMediaAudioUtil::GetMaximumAmrMode(mMode);

        if (mCodecType == kAudioCodecEvs)
        {
            mAudioPlayer->SetEvsBandwidth((int32_t)mEvsBandwidth);
            mAudioPlayer->SetEvsPayloadHeaderMode(mEvsPayloadHeaderMode);
            mAudioPlayer->SetEvsChAwOffset(mEvsChannelAwOffset);
            mRunningCodecMode = ImsMediaAudioUtil::GetMaximumEvsMode(mMode);
            mAudioPlayer->SetEvsBitRate(
                    ImsMediaAudioUtil::ConvertEVSModeToBitRate(mRunningCodecMode));
            mAudioPlayer->SetCodecMode(mRunningCodecMode);
        }
        mAudioPlayer->SetCodecMode(mode);

        mAudioPlayer->Start();
    }
    else
    {
        IMLOGE0("[IAudioPlayer] Not able to start AudioPlayer");
    }

    mNodeState = kNodeStateRunning;
    StartThread();
    return RESULT_SUCCESS;
}

void IAudioPlayerNode::Stop()
{
    IMLOGD0("[Stop]");
    StopThread();
    mCondition.reset();
    mCondition.wait_timeout(AUDIO_STOP_TIMEOUT);

    if (mAudioPlayer)
    {
        mAudioPlayer->Stop();
    }

    mNodeState = kNodeStateStopped;
}

bool IAudioPlayerNode::IsRunTime()
{
    return true;
}

bool IAudioPlayerNode::IsRunTimeStart()
{
    return false;
}

bool IAudioPlayerNode::IsSourceNode()
{
    return false;
}

void IAudioPlayerNode::SetConfig(void* config)
{
    if (config == nullptr)
    {
        return;
    }

    if (mConfig != nullptr)
    {
        delete mConfig;
        mConfig = nullptr;
    }

    mConfig = new AudioConfig(*static_cast<AudioConfig*>(config));
    mCodecType = ImsMediaAudioUtil::ConvertCodecType(mConfig->getCodecType());

    if (mCodecType == kAudioCodecAmr || mCodecType == kAudioCodecAmrWb)
    {
        mMode = mConfig->getAmrParams().getAmrMode();
        mIsOctetAligned = mConfig->getAmrParams().getOctetAligned();
    }
    else if (mCodecType == kAudioCodecEvs)
    {
        mMode = mConfig->getEvsParams().getEvsMode();
        mEvsChannelAwOffset = mConfig->getEvsParams().getChannelAwareMode();
        mEvsBandwidth = ImsMediaAudioUtil::FindMaxEvsBandwidthFromRange(
                mConfig->getEvsParams().getEvsBandwidth());
        mEvsPayloadHeaderMode = mConfig->getEvsParams().getUseHeaderFullOnly();
    }

    mSamplingRate = mConfig->getSamplingRateKHz();
    mIsDtxEnabled = mConfig->getDtxEnabled();
    SetJitterBufferSize(3, 3, 9);
    SetJitterOptions(
            80, 1, (double)25 / 10, false /** TODO: when enable DTX, set this true on condition*/
    );
}

bool IAudioPlayerNode::IsSameConfig(void* config)
{
    if (config == nullptr)
    {
        return true;
    }

    AudioConfig* pConfig = reinterpret_cast<AudioConfig*>(config);

    if (mCodecType == ImsMediaAudioUtil::ConvertCodecType(pConfig->getCodecType()))
    {
        if (mCodecType == kAudioCodecAmr || mCodecType == kAudioCodecAmrWb)
        {
            return (mMode == pConfig->getAmrParams().getAmrMode() &&
                    mSamplingRate == pConfig->getSamplingRateKHz() &&
                    mIsDtxEnabled == pConfig->getDtxEnabled() &&
                    mIsOctetAligned == pConfig->getAmrParams().getOctetAligned());
        }
        else if (mCodecType == kAudioCodecEvs)
        {
            return (mMode == pConfig->getEvsParams().getEvsMode() &&
                    mEvsBandwidth ==
                            ImsMediaAudioUtil::FindMaxEvsBandwidthFromRange(
                                    pConfig->getEvsParams().getEvsBandwidth()) &&
                    mEvsChannelAwOffset == pConfig->getEvsParams().getChannelAwareMode() &&
                    mSamplingRate == pConfig->getSamplingRateKHz() &&
                    mEvsPayloadHeaderMode == pConfig->getEvsParams().getUseHeaderFullOnly() &&
                    mIsDtxEnabled == pConfig->getDtxEnabled());
        }
    }

    return false;
}

void IAudioPlayerNode::ProcessCmr(const uint32_t cmrType, const uint32_t cmrDefine)
{
    IMLOGD2("[ProcessCmr] cmr type[%d], define[%d]", cmrType, cmrDefine);

    if (mAudioPlayer == nullptr)
    {
        return;
    }

    if (mCodecType == kAudioCodecEvs)
    {
        if (cmrType == kEvsCmrCodeTypeNoReq || cmrDefine == kEvsCmrCodeDefineNoReq)
        {
            int32_t mode = ImsMediaAudioUtil::GetMaximumEvsMode(mMode);

            if (mRunningCodecMode != mode)
            {
                mAudioPlayer->ProcessCmr(mode);
                mRunningCodecMode = mode;
            }
        }
        else
        {
            int mode = MAX_CODEC_EVS_AMR_IO_MODE;
            switch (cmrType)
            {
                case kEvsCmrCodeTypeNb:
                    mEvsBandwidth = kEvsBandwidthNB;
                    mode += cmrDefine;
                    break;
                case kEvsCmrCodeTypeWb:
                    mEvsBandwidth = kEvsBandwidthWB;
                    mode += cmrDefine;
                    break;
                case kEvsCmrCodeTypeSwb:
                    mEvsBandwidth = kEvsBandwidthSWB;
                    mode += cmrDefine;
                    break;
                case kEvsCmrCodeTypeFb:
                    mEvsBandwidth = kEvsBandwidthFB;
                    mode += cmrDefine;
                    break;
                case kEvsCmrCodeTypeWbCha:
                    mEvsBandwidth = kEvsBandwidthWB;
                    mode = kImsAudioEvsPrimaryMode13200;
                    break;
                case kEvsCmrCodeTypeSwbCha:
                    mEvsBandwidth = kEvsBandwidthSWB;
                    mode = kImsAudioEvsPrimaryMode13200;
                    break;
                case kEvsCmrCodeTypeAmrIO:
                    mode = cmrDefine;
                    break;
                default:
                    break;
            }

            if (cmrType == kEvsCmrCodeTypeWbCha || cmrType == kEvsCmrCodeTypeSwbCha)
            {
                switch (cmrDefine)
                {
                    case kEvsCmrCodeDefineChaOffset2:
                    case kEvsCmrCodeDefineChaOffsetH2:
                        mEvsChannelAwOffset = 2;
                        break;
                    case kEvsCmrCodeDefineChaOffset3:
                    case kEvsCmrCodeDefineChaOffsetH3:
                        mEvsChannelAwOffset = 3;
                        break;
                    case kEvsCmrCodeDefineChaOffset5:
                    case kEvsCmrCodeDefineChaOffsetH5:
                        mEvsChannelAwOffset = 5;
                        break;
                    case kEvsCmrCodeDefineChaOffset7:
                    case kEvsCmrCodeDefineChaOffsetH7:
                        mEvsChannelAwOffset = 7;
                        break;
                    default:
                        mEvsChannelAwOffset = 3;
                        break;
                }
            }

            mAudioPlayer->SetEvsBandwidth((int32_t)mEvsBandwidth);
            mAudioPlayer->SetEvsChAwOffset(mEvsChannelAwOffset);

            if (mode != mRunningCodecMode)
            {
                mRunningCodecMode = mode;
                mAudioPlayer->SetEvsBitRate(
                        ImsMediaAudioUtil::ConvertEVSModeToBitRate(mRunningCodecMode));
                mAudioPlayer->SetCodecMode(mRunningCodecMode);
            }

            mAudioPlayer->ProcessCmr(mRunningCodecMode);
        }
    }
}

void* IAudioPlayerNode::run()
{
    IMLOGD0("[run] enter");
    SetAudioThreadPriority(gettid());
    ImsMediaSubType subtype = MEDIASUBTYPE_UNDEFINED;
    ImsMediaSubType datatype = MEDIASUBTYPE_UNDEFINED;
    uint8_t* pData = nullptr;
    uint32_t nDataSize = 0;
    uint32_t nTimestamp = 0;
    bool bMark = false;
    uint32_t nSeqNum = 0;
    uint32_t currentTime = 0;
    uint64_t nNextTime = ImsMediaTimer::GetTimeInMicroSeconds();
    bool isFirstFrameReceived = false;

    while (true)
    {
        if (IsThreadStopped())
        {
            IMLOGD0("[run] terminated");
            break;
        }

        if (GetData(&subtype, &pData, &nDataSize, &nTimestamp, &bMark, &nSeqNum, &datatype,
                    &currentTime))
        {
            IMLOGD_PACKET2(IM_PACKET_LOG_AUDIO, "[run] write buffer size[%d], TS[%u]", nDataSize,
                    nTimestamp);
            if (nDataSize != 0)
            {
                if (mAudioPlayer->onDataFrame(pData, nDataSize, datatype == MEDIASUBTYPE_AUDIO_SID))
                {
                    // send buffering complete message to client
                    if (!isFirstFrameReceived)
                    {
                        mCallback->SendEvent(kImsMediaEventFirstPacketReceived,
                                reinterpret_cast<uint64_t>(new AudioConfig(*mConfig)));
                        isFirstFrameReceived = true;
                    }
                }
            }
            DeleteData();
        }
        else if (isFirstFrameReceived)
        {
            IMLOGD_PACKET0(IM_PACKET_LOG_AUDIO, "[run] GetData returned 0 bytes");
            mAudioPlayer->onDataFrame(nullptr, 0, false);
        }

        nNextTime += 20000;
        uint64_t nCurrTime = ImsMediaTimer::GetTimeInMicroSeconds();
        int64_t nTime = nNextTime - nCurrTime;

        if (nTime < 0)
        {
            continue;
        }

        ImsMediaTimer::USleep(nTime);
    }
    mCondition.signal();
    return nullptr;
}
