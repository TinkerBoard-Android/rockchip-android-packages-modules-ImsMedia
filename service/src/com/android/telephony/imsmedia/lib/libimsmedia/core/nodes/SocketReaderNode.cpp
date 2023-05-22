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

#include <SocketReaderNode.h>
#include <ImsMediaTrace.h>
#include <ImsMediaTimer.h>
#include <thread>

SocketReaderNode::SocketReaderNode(BaseSessionCallback* callback) :
        BaseNode(callback),
        mLocalFd(0)
{
    mSocket = nullptr;
    mReceiveTtl = false;
    mSocketOpened = false;
}

SocketReaderNode::~SocketReaderNode()
{
    IMLOGD1("[~SocketReaderNode] queue size[%d]", GetDataCount());
    CloseSocket();
}

kBaseNodeId SocketReaderNode::GetNodeId()
{
    return kNodeIdSocketReader;
}

ImsMediaResult SocketReaderNode::Start()
{
    IMLOGD2("[Start] media[%d], protocolType[%d]", mMediaType, mProtocolType);
    mSocket = ISocket::GetInstance(mLocalAddress.port, mPeerAddress.ipAddress, mPeerAddress.port);

    if (mSocket == nullptr)
    {
        IMLOGE0("[Start] can't create socket instance");
        return RESULT_NOT_READY;
    }

    ClearDataQueue();  // clear the old data stacked

    if (mSocketOpened)
    {
        IMLOGD0("[Start] opened already");
        mNodeState = kNodeStateRunning;
        return RESULT_SUCCESS;
    }

    // set socket local/peer address here
    mSocket->SetLocalEndpoint(mLocalAddress.ipAddress, mLocalAddress.port);
    mSocket->SetPeerEndpoint(mPeerAddress.ipAddress, mPeerAddress.port);

    if (!mSocketOpened && !mSocket->Open(mLocalFd))
    {
        IMLOGE0("[Start] can't open socket");
        mSocketOpened = false;
        return RESULT_PORT_UNAVAILABLE;
    }

    mReceiveTtl = false;

    if (mSocket->SetSocketOpt(kSocketOptionIpTtl, 1))
    {
        mReceiveTtl = true;
    }

    mSocket->Listen(this);
    mSocketOpened = true;
    mNodeState = kNodeStateRunning;
    return RESULT_SUCCESS;
}

void SocketReaderNode::Stop()
{
    IMLOGD2("[Stop] media[%d], protocolType[%d]", mMediaType, mProtocolType);

    if (mProtocolType != kProtocolRtp)
    {
        CloseSocket();
    }

    mNodeState = kNodeStateStopped;
}

void SocketReaderNode::ProcessData()
{
    uint8_t* data = nullptr;
    uint32_t dataSize = 0;
    uint32_t timeStamp = 0;
    bool bMark = false;
    uint32_t seqNum = 0;
    ImsMediaSubType subtype;
    ImsMediaSubType dataType;
    uint32_t arrivalTime;

    while (GetData(
            &subtype, &data, &dataSize, &timeStamp, &bMark, &seqNum, &dataType, &arrivalTime))
    {
        IMLOGD_PACKET3(IM_PACKET_LOG_SOCKET, "[ProcessData] media[%d], size[%d], arrivalTime[%u]",
                mMediaType, dataSize, arrivalTime);
        SendDataToRearNode(MEDIASUBTYPE_UNDEFINED, reinterpret_cast<uint8_t*>(data), dataSize,
                timeStamp, bMark, seqNum, dataType, arrivalTime);
        DeleteData();
    }
}

bool SocketReaderNode::IsRunTime()
{
    return false;
}

bool SocketReaderNode::IsSourceNode()
{
    return true;
}

void SocketReaderNode::SetConfig(void* config)
{
    if (config == nullptr)
    {
        return;
    }

    RtpConfig* pConfig = reinterpret_cast<RtpConfig*>(config);

    if (mProtocolType == kProtocolRtp)
    {
        mPeerAddress = RtpAddress(pConfig->getRemoteAddress().c_str(), pConfig->getRemotePort());
    }
    else if (mProtocolType == kProtocolRtcp)
    {
        mPeerAddress =
                RtpAddress(pConfig->getRemoteAddress().c_str(), pConfig->getRemotePort() + 1);
    }
}

bool SocketReaderNode::IsSameConfig(void* config)
{
    if (config == nullptr)
    {
        return true;
    }

    RtpConfig* pConfig = reinterpret_cast<RtpConfig*>(config);
    RtpAddress peerAddress;

    if (mProtocolType == kProtocolRtp)
    {
        peerAddress = RtpAddress(pConfig->getRemoteAddress().c_str(), pConfig->getRemotePort());
    }
    else if (mProtocolType == kProtocolRtcp)
    {
        peerAddress = RtpAddress(pConfig->getRemoteAddress().c_str(), pConfig->getRemotePort() + 1);
    }

    return (mPeerAddress == peerAddress);
}

ImsMediaResult SocketReaderNode::UpdateConfig(void* config)
{
    // check config items updates
    bool isUpdateNode = false;

    if (IsSameConfig(config))
    {
        IMLOGD0("[UpdateConfig] no update");
        return RESULT_SUCCESS;
    }
    else
    {
        isUpdateNode = true;
    }

    kBaseNodeState prevState = mNodeState;

    if (isUpdateNode && mNodeState == kNodeStateRunning)
    {
        Stop();

        if (mProtocolType == kProtocolRtp && mSocketOpened)
        {
            CloseSocket();
        }
    }

    // reset the parameters
    SetConfig(config);

    if (isUpdateNode && prevState == kNodeStateRunning)
    {
        return Start();
    }

    return RESULT_SUCCESS;
}

void SocketReaderNode::OnReadDataFromSocket()
{
    std::lock_guard<std::mutex> guard(mMutex);

    if (mSocket != nullptr)
    {
        int nLen = mSocket->ReceiveFrom(mBuffer, DEFAULT_MTU);

        if (nLen > 0)
        {
            IMLOGD_PACKET3(IM_PACKET_LOG_SOCKET,
                    "[OnReadDataFromSocket] media[%d], data size[%d], queue size[%d]", mMediaType,
                    nLen, GetDataCount());

            OnDataFromFrontNode(MEDIASUBTYPE_UNDEFINED, mBuffer, nLen, 0, 0, 0,
                    MEDIASUBTYPE_UNDEFINED, ImsMediaTimer::GetTimeInMilliSeconds());
        }
    }
}

void SocketReaderNode::SetLocalFd(int fd)
{
    mLocalFd = fd;
}

void SocketReaderNode::SetLocalAddress(const RtpAddress& address)
{
    mLocalAddress = address;
}

void SocketReaderNode::SetPeerAddress(const RtpAddress& address)
{
    mPeerAddress = address;
}

void SocketReaderNode::CloseSocket()
{
    std::lock_guard<std::mutex> guard(mMutex);

    if (mSocket != nullptr)
    {
        IMLOGD2("[CloseSocket] media[%d], protocolType[%d]", mMediaType, mProtocolType);

        if (mSocketOpened)
        {
            mSocket->Listen(nullptr);
            mSocket->Close();
            mSocketOpened = false;
        }

        ISocket::ReleaseInstance(mSocket);
        mSocket = nullptr;
    }
}