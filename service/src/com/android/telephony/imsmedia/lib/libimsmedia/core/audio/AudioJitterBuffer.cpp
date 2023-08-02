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

#include <AudioJitterBuffer.h>
#include <ImsMediaDataQueue.h>
#include <ImsMediaTimer.h>
#include <ImsMediaTrace.h>
#include <numeric>

#define AUDIO_JITTER_BUFFER_MIN_SIZE   (3)
#define AUDIO_JITTER_BUFFER_MAX_SIZE   (9)
#define AUDIO_JITTER_BUFFER_START_SIZE (4)
#define GET_SEQ_GAP(a, b)              ((uint16_t)(a) - (uint16_t)(b))
#define JITTER_BUFFER_UPDATE_INTERVAL  (100)    // ms unit
#define FRAME_INTERVAL                 (20)     // ms unit
#define ALLOWABLE_ERROR                (10)     // ms unit
#define RESET_THRESHOLD                (10000)  // ms unit
#define TS_ROUND_QUARD                 (3000)   // ms unit
#define SEQ_OUTLIER_THRESHOLD          (3000)
#define USHORT_TS_ROUND_COMPARE(a, b)                                                   \
    ((((a) >= (b)) && (((b) >= TS_ROUND_QUARD) || ((a) <= 0xffff - TS_ROUND_QUARD))) || \
            (((a) <= TS_ROUND_QUARD) && ((b) >= 0xffff - TS_ROUND_QUARD)))

#define MAX_STORED_BUFFER_SIZE (50 * 60 * 60)  // 1 hour in frame interval unit
#define MAX_QUEUE_SIZE         (150)           // 3 sec

AudioJitterBuffer::AudioJitterBuffer()
{
    mInitJitterBufferSize = AUDIO_JITTER_BUFFER_START_SIZE;
    mMinJitterBufferSize = AUDIO_JITTER_BUFFER_MIN_SIZE;
    mMaxJitterBufferSize = AUDIO_JITTER_BUFFER_MAX_SIZE;
    mBufferIgnoreSIDPacket = false;
    mJitterAnalyzer.Reset();
    mJitterAnalyzer.SetMinMaxJitterBufferSize(mMinJitterBufferSize, mMaxJitterBufferSize);
    mListJitterBufferSize.clear();
    AudioJitterBuffer::Reset();
}

AudioJitterBuffer::~AudioJitterBuffer()
{
    AudioJitterBuffer::ClearBuffer();
}

void AudioJitterBuffer::Reset()
{
    IMLOGD0("[Reset]");
    mLastPlayedSeqNum = 0;
    mLastPlayedTimestamp = 0;
    mFirstFrameReceived = false;
    mNextJitterBufferSize = mCurrJitterBufferSize;
    mDeleteCount = 0;
    mDtxPlayed = false;
    mWaiting = true;
    mEnforceUpdate = false;
    mUpdatedDelay = 0;
    mCannotGetCount = 0;
    mCheckUpdateJitterPacketCnt = 0;
    mPreservedDtx = nullptr;
}

void AudioJitterBuffer::ClearBuffer()
{
    IMLOGD0("[ClearBuffer]");
    std::lock_guard<std::mutex> guard(mMutex);
    DataEntry* entry = nullptr;

    while (mDataQueue.Get(&entry))
    {
        if (entry->eDataType != MEDIASUBTYPE_AUDIO_SID)
        {
            CollectRxRtpStatus(entry->nSeqNum, kRtpStatusDiscarded);
        }

        mDataQueue.Delete();
    }
}

void AudioJitterBuffer::SetJitterBufferSize(uint32_t nInit, uint32_t nMin, uint32_t nMax)
{
    IMLOGD3("[SetJitterBufferSize] %d, %d, %d", nInit, nMin, nMax);

    if (nMin > 0)
    {
        mMinJitterBufferSize = nMin;
    }

    if (nMax > 0)
    {
        mMaxJitterBufferSize = nMax;
    }

    if (nInit > 0)
    {
        if (nInit < mMinJitterBufferSize)
        {
            nInit = mMinJitterBufferSize;
        }

        if (nInit > mMaxJitterBufferSize)
        {
            nInit = mMaxJitterBufferSize;
        }

        mInitJitterBufferSize = nInit;
        mCurrJitterBufferSize = mInitJitterBufferSize;
        mNextJitterBufferSize = mInitJitterBufferSize;
    }

    mJitterAnalyzer.SetMinMaxJitterBufferSize(mMinJitterBufferSize, mMaxJitterBufferSize);
}

void AudioJitterBuffer::SetJitterOptions(
        uint32_t incThreshold, uint32_t decThreshold, uint32_t stepSize, double zValue)
{
    mJitterAnalyzer.SetJitterOptions(incThreshold, decThreshold, stepSize, zValue);
}

void AudioJitterBuffer::Add(ImsMediaSubType subtype, uint8_t* pbBuffer, uint32_t nBufferSize,
        uint32_t nTimestamp, bool bMark, uint32_t nSeqNum, ImsMediaSubType nDataType,
        uint32_t arrivalTime)
{
    DataEntry currEntry = DataEntry();
    currEntry.subtype = subtype;
    currEntry.pbBuffer = pbBuffer;
    currEntry.nBufferSize = nBufferSize;
    currEntry.nTimestamp = nTimestamp;
    currEntry.bMark = bMark;
    currEntry.nSeqNum = nSeqNum;
    currEntry.bHeader = true;
    currEntry.bValid = true;
    currEntry.arrivalTime = arrivalTime;
    currEntry.eDataType = nDataType;

    if (subtype == MEDIASUBTYPE_REFRESHED)
    {
        // nBufferSize is ssrc value
        mSsrc = nBufferSize;
        mTimeStarted = ImsMediaTimer::GetTimeInMilliSeconds();
        mJitterAnalyzer.Reset();
        mDataQueue.Add(&currEntry);

        IMLOGI2("[Add] ssrc[%u], startTime[%d]", mSsrc, mTimeStarted);
        return;
    }

    if (mCannotGetCount > mMaxJitterBufferSize)
    {
        ClearBuffer();
        Reset();
        // reset the timer for waiting
        mTimeStarted = ImsMediaTimer::GetTimeInMilliSeconds();
    }

    int32_t jitter = 0;

    if (!mBufferIgnoreSIDPacket ||
            (mBufferIgnoreSIDPacket && currEntry.eDataType != MEDIASUBTYPE_AUDIO_SID))
    {
        jitter = mJitterAnalyzer.CalculateTransitTimeDifference(nTimestamp, arrivalTime);
    }

    RtpPacket* packet = new RtpPacket();

    if (nBufferSize == 0)
    {
        packet->rtpDataType = kRtpDataTypeNoData;
    }
    else
    {
        (currEntry.eDataType == MEDIASUBTYPE_AUDIO_SID) ? packet->rtpDataType = kRtpDataTypeSid
                                                        : packet->rtpDataType = kRtpDataTypeNormal;
    }

    packet->ssrc = mSsrc;
    packet->seqNum = nSeqNum;
    packet->jitter = jitter;
    packet->arrival = arrivalTime;
    mCallback->SendEvent(kCollectPacketInfo, kStreamRtpRx, reinterpret_cast<uint64_t>(packet));

    if (nBufferSize == 0)
    {
        return;
    }

    std::lock_guard<std::mutex> guard(mMutex);

    IMLOGD_PACKET8(IM_PACKET_LOG_JITTER,
            "[Add] seq[%d], mark[%d], TS[%d], size[%d], jitter[%d], queue[%d], playingDiff[%d], "
            "arrival[%d]",
            nSeqNum, bMark, nTimestamp, nBufferSize, jitter, mDataQueue.GetCount() + 1,
            mCurrPlayingTS - nTimestamp, arrivalTime);

    if (mDataQueue.GetCount() == 0)
    {  // jitter buffer is empty
        mDataQueue.Add(&currEntry);
    }
    else
    {
        DataEntry* pEntry;
        mDataQueue.GetLast(&pEntry);

        if (pEntry == nullptr)
        {
            return;
        }

        // current data is the latest data
        if (USHORT_SEQ_ROUND_COMPARE(nSeqNum, pEntry->nSeqNum))
        {
            mDataQueue.Add(&currEntry);
        }
        else
        {
            // find the position of current data and insert current data to the correct position
            mDataQueue.SetReadPosFirst();

            for (int32_t i = 0; mDataQueue.GetNext(&pEntry); i++)
            {
                // late arrival packet
                if (!USHORT_SEQ_ROUND_COMPARE(nSeqNum, pEntry->nSeqNum))
                {
                    mDataQueue.InsertAt(i, &currEntry);
                    break;
                }
            }
        }
    }
}

bool AudioJitterBuffer::Get(ImsMediaSubType* psubtype, uint8_t** ppData, uint32_t* pnDataSize,
        uint32_t* pnTimestamp, bool* pbMark, uint32_t* pnSeqNum, uint32_t currentTime,
        ImsMediaSubType* pDataType)
{
    std::lock_guard<std::mutex> guard(mMutex);

    DataEntry* pEntry = nullptr;
    bool bForceToPlay = false;
    mCheckUpdateJitterPacketCnt++;

    if (mDataQueue.Get(&pEntry) && pEntry->subtype == MEDIASUBTYPE_REFRESHED)  // ssrc changed
    {
        Reset();
        mDataQueue.Delete();  // delete indication frame of ssrc

        if (!mWaiting && mDataQueue.Get(&pEntry))  // get next frame
        {
            mCurrPlayingTS = pEntry->nTimestamp;  // play directly
            mWaiting = false;
        }
    }

    // update jitter buffer size
    if (!mWaiting && mUpdatedDelay == 0 &&
            ((mDtxPlayed && mDataQueue.Get(&pEntry) &&
                     pEntry->eDataType != MEDIASUBTYPE_AUDIO_SID) ||
                    mCheckUpdateJitterPacketCnt * FRAME_INTERVAL > JITTER_BUFFER_UPDATE_INTERVAL))
    {
        uint32_t nextJitterBufferSize =
                mJitterAnalyzer.GetNextJitterBufferSize(mCurrJitterBufferSize, currentTime);
        mCheckUpdateJitterPacketCnt = 0;
        mUpdatedDelay = nextJitterBufferSize - mCurrJitterBufferSize;
        mCurrJitterBufferSize = nextJitterBufferSize;
    }

    if (mListJitterBufferSize.size() > MAX_STORED_BUFFER_SIZE)  // 1hour
    {
        mListJitterBufferSize.pop_front();
    }

    mListJitterBufferSize.push_back(mCurrJitterBufferSize);

    // increase delay
    if (!mWaiting && mDtxPlayed && mUpdatedDelay > 0)
    {
        IMLOGD2("[Get] increase delay[%d], curTS[%d]", mUpdatedDelay, mCurrPlayingTS);
        mUpdatedDelay--;
        return false;
    }

    // decrease delay
    if (!mWaiting && mDataQueue.Get(&pEntry) && pEntry->eDataType == MEDIASUBTYPE_AUDIO_SID &&
            mUpdatedDelay < 0)
    {
        IMLOGD3("[Get] decrease delay[%d], curTS[%u], queue[%u]", mUpdatedDelay, mCurrPlayingTS,
                mDataQueue.GetCount());
        mUpdatedDelay++;
        mCurrPlayingTS += FRAME_INTERVAL;
    }

    // enforce update when breach the reset threshold
    if (mCannotGetCount * FRAME_INTERVAL > RESET_THRESHOLD)
    {
        IMLOGD0("[Get] enforce update");
        mEnforceUpdate = true;
        mWaiting = false;
        mCannotGetCount = 0;
    }

    if (mDataQueue.GetCount() == 0)
    {
        IMLOGD_PACKET1(IM_PACKET_LOG_JITTER, "[Get] fail - empty, curTS[%d]", mCurrPlayingTS);

        if (!mWaiting)
        {
            mCurrPlayingTS += FRAME_INTERVAL;
        }

        return false;
    }
    else if (mDataQueue.Get(&pEntry) && mWaiting)
    {
        if (currentTime - mTimeStarted < mCurrJitterBufferSize * FRAME_INTERVAL)
        {
            if (psubtype)
                *psubtype = MEDIASUBTYPE_UNDEFINED;
            if (ppData)
                *ppData = nullptr;
            if (pnDataSize)
                *pnDataSize = 0;
            if (pnTimestamp)
                *pnTimestamp = 0;
            if (pbMark)
                *pbMark = false;
            if (pnSeqNum)
                *pnSeqNum = 0;
            if (pDataType)
                *pDataType = MEDIASUBTYPE_UNDEFINED;

            IMLOGD_PACKET4(IM_PACKET_LOG_JITTER,
                    "[Get] Wait - seq[%u], CurrJBSize[%u], delay[%u], QueueCount[%u]",
                    pEntry->nSeqNum, mCurrJitterBufferSize, currentTime - pEntry->arrivalTime,
                    GetCount());
            return false;
        }
        else
        {
            // resync when the audio frame stacked over the current jitter buffer size
            Resync(mCurrJitterBufferSize + 1);
            mWaiting = false;
        }
    }

    // discard duplicated packet
    if (mDataQueue.Get(&pEntry) && mFirstFrameReceived && pEntry->nSeqNum == mLastPlayedSeqNum)
    {
        IMLOGD6("[Get] duplicate - curTS[%u], seq[%d], mark[%d], TS[%u], size[%d], queue[%d]",
                mCurrPlayingTS, pEntry->nSeqNum, pEntry->bMark, pEntry->nTimestamp,
                pEntry->nBufferSize, mDataQueue.GetCount());
        CollectRxRtpStatus(pEntry->nSeqNum, kRtpStatusDuplicated);
        mDataQueue.Delete();
        mDeleteCount++;
    }

    if (currentTime - mTimeStarted < 3000)
    {
        // resync when the audio frame stacked over the max jitter buffer size
        Resync(mMaxJitterBufferSize);
    }
    else
    {
        Resync(MAX_QUEUE_SIZE);
    }

    // adjust the playing timestamp
    if (mDataQueue.Get(&pEntry) && pEntry->nTimestamp != mCurrPlayingTS &&
            ((mCurrPlayingTS - ALLOWABLE_ERROR) < pEntry->nTimestamp) &&
            (pEntry->nTimestamp < (mCurrPlayingTS + ALLOWABLE_ERROR)))
    {
        mCurrPlayingTS = pEntry->nTimestamp;
        IMLOGD2("[Get] sync playing curTS[%u], seq[%d]", mCurrPlayingTS, pEntry->nSeqNum);
    }

    // delete late arrival
    while (mDataQueue.Get(&pEntry) && !USHORT_TS_ROUND_COMPARE(pEntry->nTimestamp, mCurrPlayingTS))
    {
        mDtxPlayed = (pEntry->eDataType == MEDIASUBTYPE_AUDIO_SID);

        // discard case that latest packet is about to cut by the jitter then update the
        // sequence number to avoid incorrect lost counting
        if (pEntry->nSeqNum >= mLastPlayedSeqNum)
        {
            mLastPlayedSeqNum = pEntry->nSeqNum;
        }

        IMLOGD_PACKET3(IM_PACKET_LOG_JITTER,
                "[Get] delete late arrival seq[%d], curTS[%u], dtx[%d]", pEntry->nSeqNum,
                mCurrPlayingTS, mDtxPlayed);

        if (mPreservedDtx != nullptr)
        {
            delete mPreservedDtx;
        }

        mPreservedDtx = nullptr;

        if (mDtxPlayed)
        {
            mPreservedDtx = new DataEntry(*pEntry);
        }
        else
        {
            CollectRxRtpStatus(pEntry->nSeqNum, kRtpStatusLate);
            mDeleteCount++;
        }

        mJitterAnalyzer.SetLateArrivals(currentTime);
        mDataQueue.Delete();
    }

    // add condition in case of changing Seq# & TS
    if (mDataQueue.Get(&pEntry) && (pEntry->nTimestamp - mCurrPlayingTS) > TS_ROUND_QUARD)
    {
        IMLOGD4("[Get] TS changing case, enforce play [ %d / %u / %u / %d ]", pEntry->nSeqNum,
                pEntry->nTimestamp, mCurrPlayingTS, mDataQueue.GetCount());
        bForceToPlay = true;
    }

    if (mEnforceUpdate)
    {
        // removing delete packet in min JitterBuffer size
        if (mDataQueue.GetCount() > mCurrJitterBufferSize + 1)
        {
            if (mDataQueue.Get(&pEntry))
            {
                IMLOGD_PACKET5(IM_PACKET_LOG_JITTER,
                        "[Get] delete - seq[%d], mark[%d], TS[%u], curTS[%u], size[%d]",
                        pEntry->nSeqNum, pEntry->bMark, pEntry->nTimestamp, mCurrPlayingTS,
                        mDataQueue.GetCount());

                mDtxPlayed = (pEntry->eDataType == MEDIASUBTYPE_AUDIO_SID);

                if (pEntry->nSeqNum >= mLastPlayedSeqNum)
                {
                    mLastPlayedSeqNum = pEntry->nSeqNum;
                }

                if (pEntry->eDataType != MEDIASUBTYPE_AUDIO_SID)
                {
                    CollectRxRtpStatus(pEntry->nSeqNum, kRtpStatusDiscarded);
                }

                mDataQueue.Delete();
                bForceToPlay = true;
            }
        }

        mEnforceUpdate = false;

        if ((mDataQueue.GetCount() < 2) ||
                (mDataQueue.GetCount() < mCurrJitterBufferSize - mMinJitterBufferSize))
        {
            IMLOGD_PACKET0(IM_PACKET_LOG_JITTER, "[Get] wait stacking");
            return false;
        }
    }

    if (mDataQueue.Get(&pEntry) &&
            (pEntry->nTimestamp == mCurrPlayingTS || bForceToPlay ||
                    (pEntry->nTimestamp < TS_ROUND_QUARD && mCurrPlayingTS > 0xFFFF)))
    {
        if (psubtype)
            *psubtype = pEntry->subtype;
        if (ppData)
            *ppData = pEntry->pbBuffer;
        if (pnDataSize)
            *pnDataSize = pEntry->nBufferSize;
        if (pnTimestamp)
            *pnTimestamp = pEntry->nTimestamp;
        if (pbMark)
            *pbMark = pEntry->bMark;
        if (pnSeqNum)
            *pnSeqNum = pEntry->nSeqNum;
        if (pDataType)
            *pDataType = pEntry->eDataType;

        mDtxPlayed = (pEntry->eDataType == MEDIASUBTYPE_AUDIO_SID);

        if (mFirstFrameReceived)
        {
            /** Report the loss gap if the loss gap is over 0 */
            uint16_t lostGap = GET_SEQ_GAP(pEntry->nSeqNum, mLastPlayedSeqNum);

            if (lostGap > 1 && lostGap < SEQ_OUTLIER_THRESHOLD)
            {
                uint16_t lostSeq = mLastPlayedSeqNum + 1;
                IMLOGD_PACKET2(IM_PACKET_LOG_JITTER, "[Get] lost - seq[%u], num[%u]", lostSeq,
                        lostGap - 1);

                SessionCallbackParameter* param =
                        new SessionCallbackParameter(kReportPacketLossGap, lostSeq, lostGap - 1);
                mCallback->SendEvent(kCollectOptionalInfo, reinterpret_cast<uint64_t>(param), 0);
            }
        }

        IMLOGD_PACKET7(IM_PACKET_LOG_JITTER,
                "[Get] OK - dtx[%d], curTS[%u], seq[%u], TS[%u], size[%u], delay[%u], queue[%u]",
                mDtxPlayed, mCurrPlayingTS, pEntry->nSeqNum, pEntry->nTimestamp,
                pEntry->nBufferSize, currentTime - pEntry->arrivalTime, mDataQueue.GetCount());

        mCurrPlayingTS = pEntry->nTimestamp + FRAME_INTERVAL;
        mFirstFrameReceived = true;
        mLastPlayedSeqNum = pEntry->nSeqNum;
        mCannotGetCount = 0;
        CollectRxRtpStatus(pEntry->nSeqNum, kRtpStatusNormal);
        CollectJitterBufferStatus(
                mCurrJitterBufferSize * FRAME_INTERVAL, mMaxJitterBufferSize * FRAME_INTERVAL);
        return true;
    }
    else
    {
        // TODO: check EVS redundancy in channel aware mode

        if (!mDtxPlayed)
        {
            mCannotGetCount++;
        }

        if (mPreservedDtx != nullptr)
        {
            // push front the preserved dtx to the queue
            mDataQueue.InsertAt(0, mPreservedDtx);
            delete mPreservedDtx;
            mPreservedDtx = nullptr;

            mDataQueue.Get(&pEntry);

            if (psubtype)
                *psubtype = pEntry->subtype;
            if (ppData)
                *ppData = pEntry->pbBuffer;
            if (pnDataSize)
                *pnDataSize = pEntry->nBufferSize;
            if (pnTimestamp)
                *pnTimestamp = mCurrPlayingTS;
            if (pbMark)
                *pbMark = pEntry->bMark;
            if (pnSeqNum)
                *pnSeqNum = pEntry->nSeqNum;
            if (pDataType)
                *pDataType = pEntry->eDataType;

            IMLOGD_PACKET3(IM_PACKET_LOG_JITTER, "[Get] preserved dtx[%d], curTS[%d], current[%d]",
                    mDtxPlayed, mCurrPlayingTS, currentTime);

            mCurrPlayingTS += FRAME_INTERVAL;
            return true;
        }

        if (psubtype)
            *psubtype = MEDIASUBTYPE_UNDEFINED;
        if (ppData)
            *ppData = nullptr;
        if (pnDataSize)
            *pnDataSize = 0;
        if (pnTimestamp)
            *pnTimestamp = 0;
        if (pbMark)
            *pbMark = false;
        if (pnSeqNum)
            *pnSeqNum = 0;
        if (pDataType)
            *pDataType = MEDIASUBTYPE_UNDEFINED;

        IMLOGD_PACKET3(IM_PACKET_LOG_JITTER, "[Get] fail - dtx[%d], curTS[%d], current[%d]",
                mDtxPlayed, mCurrPlayingTS, currentTime);

        mCurrPlayingTS += FRAME_INTERVAL;
        return false;
    }

    return false;
}

double AudioJitterBuffer::GetMeanBufferSize()
{
    return std::accumulate(mListJitterBufferSize.begin(), mListJitterBufferSize.end(), 0.0f) /
            mListJitterBufferSize.size();
}

void AudioJitterBuffer::Resync(uint32_t spareFrames)
{
    bool isDeleted = false;
    DataEntry* entry = nullptr;

    while (mDataQueue.Get(&entry) && GetCount() > spareFrames)
    {
        IMLOGD6("[Resync] state[%d], seq[%d], TS[%d], dtx[%d], queue[%d], spareFrames[%d]",
                mWaiting, entry->nSeqNum, entry->nTimestamp,
                entry->eDataType == MEDIASUBTYPE_AUDIO_SID, GetCount(), spareFrames);

        if (entry->eDataType != MEDIASUBTYPE_AUDIO_SID)
        {
            CollectRxRtpStatus(entry->nSeqNum, kRtpStatusDiscarded);
        }

        if (!mWaiting)
        {
            mLastPlayedSeqNum = entry->nSeqNum;
        }

        mDataQueue.Delete();
        isDeleted = true;
    }

    if ((mWaiting || isDeleted) && mDataQueue.Get(&entry))
    {
        mCurrPlayingTS = entry->nTimestamp;
    }
}

void AudioJitterBuffer::CollectRxRtpStatus(int32_t seq, kRtpPacketStatus status)
{
    IMLOGD_PACKET2(IM_PACKET_LOG_JITTER, "[CollectRxRtpStatus] seq[%d], status[%d]", seq, status);

    if (mCallback != nullptr)
    {
        SessionCallbackParameter* param =
                new SessionCallbackParameter(seq, status, ImsMediaTimer::GetTimeInMilliSeconds());
        mCallback->SendEvent(kCollectRxRtpStatus, reinterpret_cast<uint64_t>(param));
    }
}
void AudioJitterBuffer::CollectJitterBufferStatus(int32_t currSize, int32_t maxSize)
{
    IMLOGD_PACKET2(IM_PACKET_LOG_JITTER, "[CollectJitterBufferStatus] currSize[%d], maxSize[%d]",
            currSize, maxSize);

    if (mCallback != nullptr)
    {
        mCallback->SendEvent(kCollectJitterBufferSize, currSize, maxSize);
    }
}
