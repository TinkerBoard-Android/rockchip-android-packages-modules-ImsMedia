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

package com.android.telephony.imsmedia;

import android.os.Handler;
import android.os.Parcel;
import android.telephony.Rlog;
import android.telephony.imsmedia.VideoConfig;

/**
 * Video listener to process JNI messages from local AP based RTP stack
 */
public class VideoListener implements JNIImsMediaListener {
    private static final String LOG_TAG = "VideoListener";
    private final Handler mHandler;
    private ImsMediaController.OpenSessionCallback mCallback;
    private long mNativeObject;

    VideoListener(final Handler handler) {
        mHandler = handler;
    }

    /**
     * Sets callback to call ImsMediaController to handle responses when
     * openSession method called in @VideoService
     *
     * @param callback A Callback of @ImsMediaController#OpenSessionCallback
     */
    public void setMediaCallback(final ImsMediaController.OpenSessionCallback callback) {
        mCallback = callback;
    }

    /**
     * Sets native object to identify the instance of @BaseManager
     *
     * @param object
     */
    public void setNativeObject(final long object) {
        mNativeObject = object;
    }

    /**
     * Processes parcel messages from native code and posts messages to {@link VideoSession}
     *
     * @param parcel A parcel from native @VideoManager in libimsmedia library
     */
    @Override
    public void onMessage(final Parcel parcel) {
        final int event = parcel.readInt();
        switch (event) {
            case VideoSession.EVENT_OPEN_SESSION_SUCCESS:
                final int sessionId = parcel.readInt();
                Rlog.d(LOG_TAG, "onMessage=" + event);
                mCallback.onOpenSessionSuccess(sessionId,
                        new VideoLocalSession(sessionId, mNativeObject));
                break;
            case VideoSession.EVENT_OPEN_SESSION_FAILURE:
                Rlog.d(LOG_TAG, "onMessage=" + event);
                mCallback.onOpenSessionFailure(parcel.readInt(), parcel.readInt());
                break;
            case VideoSession.EVENT_SESSION_CLOSED:
                Rlog.d(LOG_TAG, "onMessage=" + event);
                mCallback.onSessionClosed(parcel.readInt());
                break;
            case VideoSession.EVENT_MODIFY_SESSION_RESPONSE:
            {
                final int result = parcel.readInt();
                final VideoConfig config = VideoConfig.CREATOR.createFromParcel(parcel);
                Rlog.d(LOG_TAG, "onMessage=" + event + ", result=" + result);
                Utils.sendMessage(mHandler, event, result, Utils.UNUSED, config);
            }
                break;
            case VideoSession.EVENT_SESSION_CHANGED_IND:
                //TODO: add implementation
                break;
            case VideoSession.EVENT_FIRST_MEDIA_PACKET_IND:
            {
                final VideoConfig config = VideoConfig.CREATOR.createFromParcel(parcel);
                Utils.sendMessage(mHandler, event, config);
            }
                break;
            case VideoSession.EVENT_PEER_DIMENSION_CHANGED:
                Utils.sendMessage(mHandler, event, parcel.readInt(), parcel.readInt());
                break;
            case VideoSession.EVENT_RTP_HEADER_EXTENSION_IND:
                //TODO: add implementation
                break;
            case VideoSession.EVENT_MEDIA_INACTIVITY_IND:
                Utils.sendMessage(mHandler, event, parcel.readInt(), Utils.UNUSED);
                break;
            case VideoSession.EVENT_PACKET_LOSS_IND:
                Utils.sendMessage(mHandler, event, parcel.readInt());
                break;
            case VideoSession.EVENT_VIDEO_DATA_USAGE_IND:
                Utils.sendMessage(mHandler, event, parcel.readLong());
                break;
            default:
                break;
        }
    }
}
