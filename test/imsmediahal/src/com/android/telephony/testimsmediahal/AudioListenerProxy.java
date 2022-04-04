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

package com.android.telephony.testimsmediahal;

import android.os.Parcel;
import android.telephony.Rlog;
import android.telephony.imsmedia.AudioConfig;
import android.os.RemoteException;

import android.hardware.radio.ims.media.IImsMediaSessionListener;
import android.hardware.radio.ims.media.IImsMediaListener;
import android.hardware.radio.ims.media.IImsMediaSession;
import android.hardware.radio.ims.media.RtpConfig;
import android.hardware.radio.ims.media.RtpHeaderExtension;

import com.android.telephony.imsmedia.JNIImsMediaListener;
import com.android.telephony.imsmedia.AudioSession;
import com.android.telephony.imsmedia.Utils;

import java.util.List;
import java.util.ArrayList;

/**
 * Implementation of proxy Listener class used to ge response back
 * from {@link libimsmedia} and sent back to {@link ImsMediaController}.
 * Set appropriate listener for sending call back to {@link ImsMediaController}.
 */

class AudioListenerProxy implements JNIImsMediaListener {

    private static String TAG = "ImsMediaAudioListener";

    private IImsMediaSessionListener mMediaSessionListener;
    private IImsMediaListener mListener;
    private static AudioListenerProxy mInstance;
    private IImsMediaSession mMediaSession;
    private int mSessionId;

    public void setMediaSessionListener(IImsMediaSessionListener mediaSessionListener) {
        mMediaSessionListener = mediaSessionListener;
    }

    public void setImsMediaListener(IImsMediaListener listener) {
        mListener = listener;
    }

    public void setSessionId(int sessionId)
    {
        mSessionId = sessionId;
    }

    public static AudioListenerProxy getInstance() {
        if(mInstance == null)
        {
            mInstance = new AudioListenerProxy();
        }
        return mInstance;
    }


    @Override
    public void onMessage(Parcel parcel) {
        final int event = parcel.readInt();
        Rlog.d(TAG, "onMessage=" + event);
        switch (event) {
            case AudioSession.EVENT_OPEN_SESSION_SUCCESS:
                final int sessionId = parcel.readInt();

                mMediaSession = new IImsMediaSessionImpl(mSessionId);

                try {
                    mListener.onOpenSessionSuccess(sessionId,
                    mMediaSession);
                } catch(RemoteException e) {
                    Rlog.e(TAG, "Failed to notify openSuccess: " + e);
                }
                break;
            case AudioSession.EVENT_OPEN_SESSION_FAILURE:
                final int sessionId1 = parcel.readInt();
                final int result = parcel.readInt();
                try {
                    mListener.onOpenSessionFailure(sessionId1,
                    result);
                } catch(RemoteException e) {
                    Rlog.e(TAG, "Failed to notify openFailure: " + e);
                }
                break;
            case AudioSession.EVENT_MODIFY_SESSION_RESPONSE:
                final int result1 = parcel.readInt();
                final AudioConfig config = AudioConfig.CREATOR.createFromParcel(parcel);
                final RtpConfig rtpConfig = Utils.convertToRtpConfig(config);

                try {
                    mMediaSessionListener.onModifySessionResponse(rtpConfig, result1);
                } catch(RemoteException e) {
                    Rlog.e(TAG, "Failed to notify modify session: " + e);
                }
                break;
            case AudioSession.EVENT_ADD_CONFIG_RESPONSE:
                final int result2 = parcel.readInt();
                final AudioConfig config1 = AudioConfig.CREATOR.createFromParcel(parcel);
                final RtpConfig rtpConfig1 = Utils.convertToRtpConfig(config1);

                try {
                    mMediaSessionListener.onAddConfigResponse(rtpConfig1, result2);
                } catch(RemoteException e) {
                    Rlog.e(TAG, "Failed to notify add config: " + e);
                }
                break;
            case AudioSession.EVENT_CONFIRM_CONFIG_RESPONSE:
                final int result3 = parcel.readInt();
                final AudioConfig config2 = AudioConfig.CREATOR.createFromParcel(parcel);
                final RtpConfig rtpConfig2 = Utils.convertToRtpConfig(config2);

                try {
                    mMediaSessionListener.onConfirmConfigResponse(rtpConfig2, result3);
                } catch(RemoteException e) {
                    Rlog.e(TAG, "Failed to notify confirm config: " + e);
                }
                break;
            case AudioSession.EVENT_SESSION_CHANGED_IND:
                final int state = parcel.readInt();

                try {
                    mMediaSessionListener.onSessionChanged(state);
                } catch(RemoteException e) {
                    Rlog.e(TAG, "Failed to notify session changed: " + e);
                }
                break;
            case AudioSession.EVENT_FIRST_MEDIA_PACKET_IND:
                final AudioConfig mediaIndCfg = AudioConfig.CREATOR.createFromParcel(parcel);
                final RtpConfig mediaIndRtpCfg = Utils.convertToRtpConfig(mediaIndCfg);

                try {
                    mMediaSessionListener.onFirstMediaPacketReceived(mediaIndRtpCfg);
                } catch(RemoteException e) {
                    Rlog.e(TAG, "Failed to notify first media packet received: " + e);
                }
                break;
            case AudioSession.EVENT_RTP_HEADER_EXTENSION_IND:
                final List<RtpHeaderExtension> rtpHeaderExt = new ArrayList<RtpHeaderExtension>();
                parcel.readList(rtpHeaderExt, RtpHeaderExtension.class.getClassLoader());

                try {
                    mMediaSessionListener.onHeaderExtensionReceived(rtpHeaderExt);
                } catch(RemoteException e) {
                    Rlog.e(TAG, "Failed to notify rtp header extension: " + e);
                }
                break;
            case AudioSession.EVENT_MEDIA_INACTIVITY_IND:
                final int pktType = parcel.readInt();

                try {
                    mMediaSessionListener.notifyMediaInactivity(pktType);
                } catch(RemoteException e) {
                    Rlog.e(TAG, "Failed to notify media inactivity: " + e);
                }
                break;
            case AudioSession.EVENT_PACKET_LOSS_IND:
                final int pktLossInd = parcel.readInt();

                try {
                    mMediaSessionListener.notifyPacketLoss(pktLossInd);
                } catch(RemoteException e) {
                    Rlog.e(TAG, "Failed to notify packet loss: " + e);
                }
                break;
            case AudioSession.EVENT_JITTER_IND:
                final int jitter = parcel.readInt();

                try {
                    mMediaSessionListener.notifyJitter(jitter);
                } catch(RemoteException e) {
                    Rlog.e(TAG, "Failed to notify jitter indication: " + e);
                }
                break;
            default:
                Rlog.d(TAG, "unidentified event.");
                break;
            }
        }
    }
