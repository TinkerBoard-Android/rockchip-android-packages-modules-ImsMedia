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

#include <AudioSession.h>
#include <ImsMediaTrace.h>
#include <ImsMediaEventHandler.h>
#include <AudioConfig.h>
#include <string>

AudioSession::AudioSession()
{
    IMLOGD0("[AudioSession]");
}

AudioSession::~AudioSession()
{
    IMLOGD0("[~AudioSession]");
    while (mListGraphRtpTx.size() > 0)
    {
        AudioStreamGraphRtpTx* graph = mListGraphRtpTx.front();
        if (graph->getState() == kStreamStateRunning)
        {
            graph->stop();
        }
        mListGraphRtpTx.pop_front();
        delete graph;
    }
    while (mListGraphRtpRx.size() > 0)
    {
        AudioStreamGraphRtpRx* graph = mListGraphRtpRx.front();
        if (graph->getState() == kStreamStateRunning)
        {
            graph->stop();
        }
        mListGraphRtpRx.pop_front();
        delete graph;
    }
    while (mListGraphRtcp.size() > 0)
    {
        AudioStreamGraphRtcp* graph = mListGraphRtcp.front();
        if (graph->getState() == kStreamStateRunning)
        {
            graph->stop();
        }
        mListGraphRtcp.pop_front();
        delete graph;
    }
}

SessionState AudioSession::getState()
{
    SessionState state = kSessionStateOpened;

    for (auto& graph : mListGraphRtpTx)
    {
        if (graph != NULL && graph->getState() == kStreamStateRunning)
        {
            return kSessionStateActive;
        }
    }

    for (auto& graph : mListGraphRtpRx)
    {
        if (graph != NULL && graph->getState() == kStreamStateRunning)
        {
            return kSessionStateActive;
        }
    }

    for (auto& graph : mListGraphRtcp)
    {
        if (graph != NULL && graph->getState() == kStreamStateRunning)
        {
            return kSessionStateSuspended;
        }
    }

    return state;
}

ImsMediaResult AudioSession::startGraph(RtpConfig* config)
{
    IMLOGD0("[startGraph]");

    if (config == NULL)
    {
        return RESULT_INVALID_PARAM;
    }

    AudioConfig* pConfig = reinterpret_cast<AudioConfig*>(config);

    if (std::strcmp(pConfig->getRemoteAddress().c_str(), "") == 0)
    {
        return RESULT_INVALID_PARAM;
    }

    ImsMediaResult ret = RESULT_NOT_READY;
    IMLOGD1("[startGraph] mListGraphRtpTx size[%d]", mListGraphRtpTx.size());

    if (mListGraphRtpTx.size() != 0)
    {
        ret = mListGraphRtpTx.front()->update(config);
        if (ret != RESULT_SUCCESS)
        {
            IMLOGE1("[startGraph] update error[%d]", ret);
            return ret;
        }
    }
    else
    {
        mListGraphRtpTx.push_back(new AudioStreamGraphRtpTx(this, mRtpFd));
        ret = mListGraphRtpTx.back()->create(config);

        if (ret == RESULT_SUCCESS &&
                (pConfig->getMediaDirection() == RtpConfig::MEDIA_DIRECTION_SEND_ONLY ||
                        pConfig->getMediaDirection() == RtpConfig::MEDIA_DIRECTION_SEND_RECEIVE))
        {
            ret = mListGraphRtpTx.back()->start();
            if (ret != RESULT_SUCCESS)
            {
                IMLOGE1("[startGraph] start error[%d]", ret);
                return ret;
            }
        }
    }

    IMLOGD1("[startGraph] mListGraphRtpRx size[%d]", mListGraphRtpRx.size());

    if (mListGraphRtpRx.size() != 0)
    {
        mListGraphRtpRx.front()->setMediaQualityThreshold(&mThreshold);
        ret = mListGraphRtpRx.front()->update(config);
        if (ret != RESULT_SUCCESS)
        {
            IMLOGE1("[startGraph] update error[%d]", ret);
            return ret;
        }
    }
    else
    {
        mListGraphRtpRx.push_back(new AudioStreamGraphRtpRx(this, mRtpFd));
        ret = mListGraphRtpRx.back()->create(config);

        if (ret == RESULT_SUCCESS &&
                (pConfig->getMediaDirection() == RtpConfig::MEDIA_DIRECTION_RECEIVE_ONLY ||
                        pConfig->getMediaDirection() == RtpConfig::MEDIA_DIRECTION_SEND_RECEIVE))
        {
            mListGraphRtpRx.back()->setMediaQualityThreshold(&mThreshold);
            ret = mListGraphRtpRx.back()->start();
            if (ret != RESULT_SUCCESS)
            {
                IMLOGE1("[startGraph] start error[%d]", ret);
                return ret;
            }
        }
    }

    IMLOGD1("[startGraph] mListGraphRtcp size[%d]", mListGraphRtcp.size());

    if (mListGraphRtcp.size() != 0)
    {
        mListGraphRtcp.front()->setMediaQualityThreshold(&mThreshold);
        ret = mListGraphRtcp.front()->update(config);
        if (ret != RESULT_SUCCESS)
        {
            IMLOGE1("[startGraph] update error[%d]", ret);
            return ret;
        }
    }
    else
    {
        mListGraphRtcp.push_back(new AudioStreamGraphRtcp(this, mRtcpFd));
        ret = mListGraphRtcp.back()->create(config);
        if (ret == RESULT_SUCCESS)
        {
            mListGraphRtcp.back()->setMediaQualityThreshold(&mThreshold);
            ret = mListGraphRtcp.back()->start();
            if (ret != RESULT_SUCCESS)
            {
                IMLOGE1("[startGraph] start error[%d]", ret);
                return ret;
            }
        }
    }

    return ret;
}

ImsMediaResult AudioSession::addGraph(RtpConfig* config)
{
    if (config == NULL || std::strcmp(config->getRemoteAddress().c_str(), "") == 0)
    {
        return RESULT_INVALID_PARAM;
    }

    for (auto& graph : mListGraphRtpTx)
    {
        if (graph != NULL && graph->isSameGraph(config))
        {
            IMLOGE0("[addGraph] same config is exist");
            return RESULT_INVALID_PARAM;
        }
    }

    for (auto& graph : mListGraphRtpTx)
    {
        if (graph != NULL)
        {
            graph->stop();
        }
    }

    for (auto& graph : mListGraphRtpRx)
    {
        if (graph != NULL)
        {
            graph->stop();
        }
    }

    for (auto& graph : mListGraphRtcp)
    {
        if (graph != NULL && graph->getState() != kStreamStateRunning)
        {
            graph->start();
        }
    }

    ImsMediaResult ret = RESULT_NOT_READY;

    mListGraphRtpTx.push_back(new AudioStreamGraphRtpTx(this, mRtpFd));
    ret = mListGraphRtpTx.back()->create(config);
    if (ret == RESULT_SUCCESS)
    {
        ret = mListGraphRtpTx.back()->start();
        if (ret != RESULT_SUCCESS)
        {
            IMLOGE1("[addGraph] start error[%d]", ret);
            return ret;
        }
    }

    IMLOGD1("[addGraph] mListGraphTx size[%d]", mListGraphRtpTx.size());

    mListGraphRtpRx.push_back(new AudioStreamGraphRtpRx(this, mRtpFd));
    ret = mListGraphRtpRx.back()->create(config);
    if (ret == RESULT_SUCCESS)
    {
        mListGraphRtpRx.back()->setMediaQualityThreshold(&mThreshold);
        ret = mListGraphRtpRx.back()->start();
        if (ret != RESULT_SUCCESS)
        {
            IMLOGE1("[addGraph] start error[%d]", ret);
            return ret;
        }
    }

    IMLOGD1("[addGraph] mListGraphRx size[%d]", mListGraphRtpRx.size());

    mListGraphRtcp.push_back(new AudioStreamGraphRtcp(this, mRtcpFd));
    ret = mListGraphRtcp.back()->create(config);
    if (ret == RESULT_SUCCESS)
    {
        mListGraphRtcp.back()->setMediaQualityThreshold(&mThreshold);
        ret = mListGraphRtcp.back()->start();
        if (ret != RESULT_SUCCESS)
        {
            IMLOGE1("[addGraph] start error[%d]", ret);
            return ret;
        }
    }

    IMLOGD1("[addGraph] mListGraphRtcp size[%d]", mListGraphRtcp.size());

    return RESULT_SUCCESS;
}

ImsMediaResult AudioSession::confirmGraph(RtpConfig* config)
{
    if (config == NULL || std::strcmp(config->getRemoteAddress().c_str(), "") == 0)
    {
        return RESULT_INVALID_PARAM;
    }

    ImsMediaResult ret = RESULT_NOT_READY;

    /** Stop unmatched running instances of StreamGraph. */
    for (auto& graph : mListGraphRtpTx)
    {
        if (graph != NULL && !graph->isSameGraph(config))
        {
            graph->stop();
        }
    }

    for (auto& graph : mListGraphRtpRx)
    {
        if (graph != NULL && !graph->isSameGraph(config))
        {
            graph->stop();
        }
    }

    for (auto& graph : mListGraphRtcp)
    {
        if (graph != NULL && !graph->isSameGraph(config))
        {
            graph->stop();
        }
    }

    bool bFound = false;
    for (std::list<AudioStreamGraphRtpTx*>::iterator iter = mListGraphRtpTx.begin();
            iter != mListGraphRtpTx.end();)
    {
        AudioStreamGraphRtpTx* graph = *iter;

        if (graph == NULL)
        {
            continue;
        }

        if (!graph->isSameGraph(config))
        {
            iter = mListGraphRtpTx.erase(iter);
            delete graph;
        }
        else
        {
            if (graph->getState() != kStreamStateRunning)
            {
                ret = graph->start();
                if (ret != RESULT_SUCCESS)
                {
                    IMLOGE1("[confirmGraph] start tx error[%d]", ret);
                    return ret;
                }
            }
            iter++;
            bFound = true;
        }
    }

    IMLOGD1("[confirmGraph] mListGraphTx size[%d]", mListGraphRtpTx.size());

    if (bFound == false)
    {
        IMLOGE0("[confirmGraph] no graph to confirm");
        return RESULT_INVALID_PARAM;
    }

    for (std::list<AudioStreamGraphRtpRx*>::iterator iter = mListGraphRtpRx.begin();
            iter != mListGraphRtpRx.end();)
    {
        AudioStreamGraphRtpRx* graph = *iter;

        if (graph == NULL)
        {
            continue;
        }

        if (!graph->isSameGraph(config))
        {
            iter = mListGraphRtpRx.erase(iter);
            delete graph;
        }
        else
        {
            if (graph->getState() != kStreamStateRunning)
            {
                ret = graph->start();
                if (ret != RESULT_SUCCESS)
                {
                    IMLOGE1("[confirmGraph] start rx error[%d]", ret);
                    return ret;
                }
            }
            iter++;
        }
    }

    IMLOGD1("[confirmGraph] mListGraphRx size[%d]", mListGraphRtpRx.size());

    for (std::list<AudioStreamGraphRtcp*>::iterator iter = mListGraphRtcp.begin();
            iter != mListGraphRtcp.end();)
    {
        AudioStreamGraphRtcp* graph = *iter;

        if (graph == NULL)
        {
            continue;
        }

        if (!graph->isSameGraph(config))
        {
            iter = mListGraphRtcp.erase(iter);
            delete graph;
        }
        else
        {
            if (graph->getState() != kStreamStateRunning)
            {
                ret = graph->start();
                if (ret != RESULT_SUCCESS)
                {
                    IMLOGE1("[confirmGraph] start rtcp error[%d]", ret);
                    return ret;
                }
            }
            iter++;
        }
    }

    IMLOGD1("[confirmGraph] mListGraphRtcp size[%d]", mListGraphRtcp.size());

    return RESULT_SUCCESS;
}

ImsMediaResult AudioSession::deleteGraph(RtpConfig* config)
{
    IMLOGD0("[deleteGraph]");
    bool bFound = false;
    for (std::list<AudioStreamGraphRtpTx*>::iterator iter = mListGraphRtpTx.begin();
            iter != mListGraphRtpTx.end();)
    {
        AudioStreamGraphRtpTx* graph = *iter;

        if (graph == NULL)
        {
            continue;
        }

        if (graph->isSameGraph(config))
        {
            if (graph->getState() == kStreamStateRunning)
            {
                graph->stop();
            }
            iter = mListGraphRtpTx.erase(iter);
            delete graph;
            bFound = true;
            break;
        }
        else
        {
            iter++;
        }
    }

    if (bFound == false)
    {
        return RESULT_INVALID_PARAM;
    }

    IMLOGD1("[deleteGraph] mListGraphRtpTx size[%d]", mListGraphRtpTx.size());

    for (std::list<AudioStreamGraphRtpRx*>::iterator iter = mListGraphRtpRx.begin();
            iter != mListGraphRtpRx.end();)
    {
        AudioStreamGraphRtpRx* graph = *iter;

        if (graph == NULL)
        {
            continue;
        }

        if (graph->isSameGraph(config))
        {
            if (graph->getState() == kStreamStateRunning)
            {
                graph->stop();
            }
            iter = mListGraphRtpRx.erase(iter);
            delete graph;
            break;
        }
        else
        {
            iter++;
        }
    }

    IMLOGD1("[deleteGraph] mListGraphRtpRx size[%d]", mListGraphRtpRx.size());

    for (std::list<AudioStreamGraphRtcp*>::iterator iter = mListGraphRtcp.begin();
            iter != mListGraphRtcp.end();)
    {
        AudioStreamGraphRtcp* graph = *iter;

        if (graph == NULL)
        {
            continue;
        }

        if (graph->isSameGraph(config))
        {
            if (graph->getState() == kStreamStateRunning)
            {
                graph->stop();
            }
            iter = mListGraphRtcp.erase(iter);
            delete graph;
            break;
        }
        else
        {
            iter++;
        }
    }

    IMLOGD1("[deleteGraph] mListGraphRtcp size[%d]", mListGraphRtcp.size());
    return RESULT_SUCCESS;
}

void AudioSession::onEvent(int32_t type, uint64_t param1, uint64_t param2)
{
    IMLOGD3("[onEvent] type[%d], param1[%d], param2[%d]", type, param1, param2);

    switch (type)
    {
        case kImsMediaEventStateChanged:
            if (mState != getState())
            {
                mState = getState();
                IMLOGD1("[onEvent] session state changed - state[%d]", mState);
                ImsMediaEventHandler::SendEvent(
                        "AUDIO_RESPONSE_EVENT", kAudioSessionChangedInd, mSessionId, mState);
            }
            break;
        case kImsMediaEventNotifyError:
            break;
        case kImsMediaEventFirstPacketReceived:
            ImsMediaEventHandler::SendEvent(
                    "AUDIO_RESPONSE_EVENT", kAudioFirstMediaPacketInd, 0, 0);
            break;
        case kImsMediaEventHeaderExtensionReceived:
            ImsMediaEventHandler::SendEvent(
                    "AUDIO_RESPONSE_EVENT", kAudioRtpHeaderExtensionInd, 0, 0);
            break;
        case kImsMediaEventMediaInactivity:
            ImsMediaEventHandler::SendEvent(
                    "AUDIO_RESPONSE_EVENT", kAudioMediaInactivityInd, mSessionId, param1, param2);
            break;
        case kImsMediaEventPacketLoss:
            ImsMediaEventHandler::SendEvent("AUDIO_RESPONSE_EVENT", kAudioPacketLossInd, param1, 0);
            break;
        case kImsMediaEventNotifyJitter:
            ImsMediaEventHandler::SendEvent(
                    "AUDIO_RESPONSE_EVENT", kAudioJitterInd, param1, param2);
            break;
        case kRequestAudioCmr:
            ImsMediaEventHandler::SendEvent(
                    "AUDIO_REQUEST_EVENT", kRequestAudioCmr, mSessionId, param1, param2);
            break;
        default:
            break;
    }
}

void AudioSession::sendDtmf(char digit, int duration)
{
    for (std::list<AudioStreamGraphRtpTx*>::iterator iter = mListGraphRtpTx.begin();
            iter != mListGraphRtpTx.end(); iter++)
    {
        AudioStreamGraphRtpTx* graph = *iter;

        if (graph != NULL && graph->getState() == kStreamStateRunning)
        {
            graph->sendDtmf(digit, duration);
        }
    }
}

void AudioSession::SendInternalEvent(int32_t type, uint64_t param1, uint64_t param2)
{
    (void)param2;

    switch (type)
    {
        case kRequestAudioCmr:
            for (std::list<AudioStreamGraphRtpTx*>::iterator iter = mListGraphRtpTx.begin();
                    iter != mListGraphRtpTx.end(); iter++)
            {
                AudioStreamGraphRtpTx* graph = *iter;

                if (graph != NULL && graph->getState() == kStreamStateRunning)
                {
                    graph->processCmr(static_cast<uint32_t>(param1));
                }
            }
            break;
        default:
            break;
    }
}