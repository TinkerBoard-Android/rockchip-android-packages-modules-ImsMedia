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

#include <AudioStreamGraphRtcp.h>
#include <RtcpEncoderNode.h>
#include <RtcpDecoderNode.h>
#include <SocketReaderNode.h>
#include <SocketWriterNode.h>
#include <ImsMediaNetworkUtil.h>
#include <ImsMediaTrace.h>

AudioStreamGraphRtcp::AudioStreamGraphRtcp(BaseSessionCallback* callback, int localFd) :
        AudioStreamGraph(callback, localFd)
{
    mConfig = NULL;
}

AudioStreamGraphRtcp::~AudioStreamGraphRtcp() {}

ImsMediaResult AudioStreamGraphRtcp::create(RtpConfig* config)
{
    IMLOGD1("[create] state[%d]", mGraphState);

    if (config == NULL)
    {
        return RESULT_INVALID_PARAM;
    }

    mConfig = new AudioConfig(reinterpret_cast<AudioConfig*>(config));
    BaseNode* pNodeRtcpEncoder = new RtcpEncoderNode(mCallback);
    pNodeRtcpEncoder->SetMediaType(IMS_MEDIA_AUDIO);
    char localIp[MAX_IP_LEN];
    uint32_t localPort = 0;
    ImsMediaNetworkUtil::getLocalIpPortFromSocket(mLocalFd, localIp, MAX_IP_LEN, localPort);
    RtpAddress localAddress(localIp, localPort - 1);
    ((RtcpEncoderNode*)pNodeRtcpEncoder)->SetLocalAddress(localAddress);
    pNodeRtcpEncoder->SetConfig(config);
    AddNode(pNodeRtcpEncoder);

    BaseNode* pNodeSocketWriter = new SocketWriterNode(mCallback);
    pNodeSocketWriter->SetMediaType(IMS_MEDIA_AUDIO);
    ((SocketWriterNode*)pNodeSocketWriter)->SetLocalFd(mLocalFd);
    ((SocketWriterNode*)pNodeSocketWriter)->SetLocalAddress(RtpAddress(localIp, localPort));
    ((SocketWriterNode*)pNodeSocketWriter)->SetProtocolType(kProtocolRtcp);
    pNodeSocketWriter->SetConfig(config);
    AddNode(pNodeSocketWriter);
    pNodeRtcpEncoder->ConnectRearNode(pNodeSocketWriter);
    setState(StreamState::kStreamStateCreated);

    BaseNode* pNodeSocketReader = new SocketReaderNode(mCallback);
    pNodeSocketReader->SetMediaType(IMS_MEDIA_AUDIO);
    ((SocketReaderNode*)pNodeSocketReader)->SetLocalFd(mLocalFd);
    ((SocketReaderNode*)pNodeSocketReader)->SetLocalAddress(RtpAddress(localIp, localPort));
    ((SocketReaderNode*)pNodeSocketReader)->SetProtocolType(kProtocolRtcp);
    pNodeSocketReader->SetConfig(config);
    AddNode(pNodeSocketReader);

    BaseNode* pNodeRtcpDecoder = new RtcpDecoderNode(mCallback);
    pNodeRtcpDecoder->SetMediaType(IMS_MEDIA_AUDIO);
    ((RtcpDecoderNode*)pNodeRtcpDecoder)->SetLocalAddress(localAddress);
    pNodeRtcpDecoder->SetConfig(config);
    AddNode(pNodeRtcpDecoder);
    pNodeSocketReader->ConnectRearNode(pNodeRtcpDecoder);
    return ImsMediaResult::RESULT_SUCCESS;
}

ImsMediaResult AudioStreamGraphRtcp::update(RtpConfig* config)
{
    IMLOGD1("[update] state[%d]", mGraphState);

    if (config == NULL)
    {
        return RESULT_INVALID_PARAM;
    }

    AudioConfig* pConfig = reinterpret_cast<AudioConfig*>(config);

    if (*reinterpret_cast<AudioConfig*>(mConfig) == *pConfig)
    {
        IMLOGD0("[update] no update");
        return RESULT_SUCCESS;
    }

    if (mConfig != NULL)
    {
        delete mConfig;
        mConfig = new AudioConfig(pConfig);
    }

    if (mConfig->getMediaDirection() == RtpConfig::MEDIA_DIRECTION_NO_FLOW)
    {
        IMLOGD0("[update] pause RTCP");
        return stop();
    }

    ImsMediaResult ret = ImsMediaResult::RESULT_NOT_READY;

    // update in running state
    if (mGraphState == kStreamStateRunning)
    {
        mScheduler->Stop();

        for (auto& node : mListNodeStarted)
        {
            IMLOGD1("[update] update node[%s]", node->GetNodeName());
            ret = node->UpdateConfig(pConfig);
            if (ret != RESULT_SUCCESS)
            {
                IMLOGE2("[update] error in update node[%s], ret[%d]", node->GetNodeName(), ret);
            }
        }

        mScheduler->Start();
    }

    if (mGraphState == kStreamStateCreated &&
            mConfig->getMediaDirection() != RtpConfig::MEDIA_DIRECTION_NO_FLOW)
    {
        IMLOGI0("[update] resume RTCP");
        return start();
    }

    return ret;
}

bool AudioStreamGraphRtcp::setMediaQualityThreshold(MediaQualityThreshold* threshold)
{
    if (threshold != NULL)
    {
        BaseNode* node = findNode(kNodeIdRtcpDecoder);

        if (node != NULL)
        {
            RtcpDecoderNode* decoder = reinterpret_cast<RtcpDecoderNode*>(node);
            decoder->SetInactivityTimerSec(threshold->getRtcpInactivityTimerMillis() / 1000);
            return true;
        }
    }

    return false;
}

bool AudioStreamGraphRtcp::OnEvent(int32_t type, uint64_t param1, uint64_t param2)
{
    IMLOGI3("[onEvent] type[%d], param1[%d], param2[%d]", type, param1, param2);

    switch (type)
    {
        case kRequestSendRtcpXrReport:
        {
            BaseNode* node = findNode(kNodeIdRtcpEncoder);

            if (node != NULL)
            {
                RtcpEncoderNode* encoder = reinterpret_cast<RtcpEncoderNode*>(node);
                encoder->SendRtcpXr(
                        reinterpret_cast<uint8_t*>(param1), static_cast<uint32_t>(param2));
                return true;
            }
        }
        break;
        default:
            break;
    }

    return false;
}