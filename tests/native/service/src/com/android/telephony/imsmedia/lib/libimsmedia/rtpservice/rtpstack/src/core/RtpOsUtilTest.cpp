/*
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

#include <RtpOsUtil.h>
#include <gtest/gtest.h>

TEST(RtpOsUtilTest, TestGetNtpTime)
{
    tRTP_NTP_TIME stCurNtpTimestamp;

    RtpOsUtil::GetNtpTime(stCurNtpTimestamp);
    struct timeval stAndrodTp;

    if (gettimeofday(&stAndrodTp, RTP_NULL) != -1)
    {
        EXPECT_EQ(stCurNtpTimestamp.m_uiNtpHigh32Bits, stAndrodTp.tv_sec + 2208988800UL);
        EXPECT_EQ(stCurNtpTimestamp.m_uiNtpLow32Bits, (RtpDt_UInt32)(stAndrodTp.tv_usec * 4294UL));
    }
}

TEST(RtpOsUtilTest, TestRand)
{
    RtpDt_UInt32 uiRand1 = RtpOsUtil::Rand();
    RtpDt_UInt32 uiRand2 = RtpOsUtil::Rand();

    EXPECT_NE(uiRand1, uiRand2);
}

TEST(RtpOsUtilTest, TestNtohl)
{
    uint8_t uiNetlong[] = {0x80, 0x01, 0xAA, 0xCC};
    uint8_t uiHostlong[] = {0xCC, 0xAA, 0x01, 0x80};

    EXPECT_EQ(RtpOsUtil::Ntohl(*(RtpDt_UInt32*)uiNetlong), *(RtpDt_UInt32*)uiHostlong);
}

TEST(RtpOsUtilTest, TestRRand)
{
    RtpDt_Double ulRRand1 = RtpOsUtil::RRand();
    RtpDt_Double ulRRand2 = RtpOsUtil::RRand();

    EXPECT_NE(ulRRand1, ulRRand2);
}