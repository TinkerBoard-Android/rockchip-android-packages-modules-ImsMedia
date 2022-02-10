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

#ifndef AUDIO_JITTER_BUFFER_H
#define AUDIO_JITTER_BUFFER_H

#include <BaseJitterBuffer.h>
#include <JitterNetworkAnalyser.h>

class AudioJitterBuffer : public BaseJitterBuffer {
public:
    AudioJitterBuffer();
    virtual ~AudioJitterBuffer();
    virtual void Reset();
    virtual void SetJitterBufferSize(uint32_t nInit, uint32_t nMin, uint32_t nMax);
    void SetJitterOptions(uint32_t nReduceTH, uint32_t nStepSize, double zValue,
        bool bIgnoreSID, bool bImprovement);
    virtual void Add(ImsMediaSubType subtype, uint8_t* pbBuffer, uint32_t nBufferSize,
        uint32_t nTimestamp, bool bMark, uint32_t nSeqNum,
        ImsMediaSubType nDataType = ImsMediaSubType::MEDIASUBTYPE_UNDEFINED);
    virtual bool Get(ImsMediaSubType* psubtype, uint8_t** ppData,
        uint32_t* pnDataSize, uint32_t* pnTimestamp, bool* pbMark, uint32_t*
        pnSeqNum, uint32_t* pnChecker = NULL);

private:
    bool CheckPartialRedundancyFrame(ImsMediaSubType* psubtype,
        uint8_t** ppData, uint32_t* pnDataSize, uint32_t* pnTimestamp, bool* pbMark,
        uint32_t* pnSeqNum, uint32_t* pnChecker = NULL);
    bool IsSID(uint8_t* pbBuffer, uint32_t nBufferSize);

    JitterNetworkAnalyser mJitterAnalyser;
    bool mDtxOn;
    bool mBufferImprovement;
    bool mBufferIgnoreSIDPacket;
    bool mNeedToUpdateBasePacket;
    bool mIsReceivedFirst;
    bool mWaiting;
    bool mFourceToUpdateJitterBuffer;
    uint32_t mCanNotGetCount;
    uint32_t mCurrPlayingTS;
    uint16_t mCurrPlayingSeq;
    uint32_t mBaseTS;
    uint32_t mBaseAT;
    uint32_t mNullDataCount;
    uint32_t mUpdateJitterBufferSize;
    uint32_t mCheckUpdateJitterPacketCnt;
    uint32_t mCurrJitterBufferSize;
    uint32_t mBufferUpdateDuration;
    uint32_t mSIDCount;
    uint32_t mDeleteCount;
    uint32_t mRedundancyOffSet;
    uint32_t mNextJitterBufferSize;

};

#endif