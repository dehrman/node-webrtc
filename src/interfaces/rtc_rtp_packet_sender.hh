/* Copyright (c) 2025 The node-webrtc project authors. All rights reserved.
 *
 * Use of this source code is governed by a BSD-style license that can be found
 * in the LICENSE.md file in the root of the source tree. All contributing
 * project authors may be found in the AUTHORS file in the root of the source
 * tree.
 */
#pragma once

#include <cstdint>
#include <mutex>
#include <string>

#include <node-addon-api/napi.h>
#include <webrtc/api/rtp_sender_interface.h>
#include <webrtc/rtc_base/third_party/sigslot/sigslot.h>

#include "src/interfaces/rtc_peer_connection.hh"
#include "src/interfaces/rtc_rtp_sender.hh"
#include "src/node/async_object_wrap_with_loop.hh"
#include "src/node/ref_ptr.hh"

namespace webrtc {
class RtpTransportInternal;
}  // namespace webrtc

namespace rtc {
class CopyOnWriteBuffer;
class Thread;
}  // namespace rtc

namespace node_webrtc {

/**
 * Nonstandard API: allows pushing pre-packetized RTP/RTCP into libwebrtc's SRTP
 * transport (selected ICE path + DTLS-SRTP keys) for a given RTCRtpSender.
 *
 * Usage:
 *   const injector = new wrtc.nonstandard.RTCRtpPacketSender(sender[, pc]);
 *   injector.sendRtp(uint8ArrayOrArrayBuffer);
 *   injector.sendRtcp(uint8ArrayOrArrayBuffer);
 *   injector.onrtcp = ({ packet }) => { ... }   // inbound RTCP feedback
 */
class RTCRtpPacketSender : public AsyncObjectWrapWithLoop<RTCRtpPacketSender>,
                           public sigslot::has_slots<> {
 public:
  RTCRtpPacketSender(const Napi::CallbackInfo &);
  ~RTCRtpPacketSender() override;

  RTCRtpPacketSender(const RTCRtpPacketSender &) = delete;
  RTCRtpPacketSender(RTCRtpPacketSender &&) = delete;
  RTCRtpPacketSender &operator=(const RTCRtpPacketSender &) = delete;
  RTCRtpPacketSender &operator=(RTCRtpPacketSender &&) = delete;

  static void Init(Napi::Env, Napi::Object);

 private:
  static Napi::FunctionReference &constructor();

  // JS API
  Napi::Value SendRtp(const Napi::CallbackInfo &);
  Napi::Value SendRtcp(const Napi::CallbackInfo &);
  Napi::Value Stop(const Napi::CallbackInfo &);
  Napi::Value GetStopped(const Napi::CallbackInfo &);

  // Helpers
  static rtc::scoped_refptr<webrtc::PeerConnectionInterface>
  FindPeerConnectionForSender(
      const rtc::scoped_refptr<webrtc::RtpSenderInterface> &sender);

  static RTCPeerConnection *UnwrapMaybePeerConnection(const Napi::Env &env,
                                                      const Napi::Value &value);

  // RTCP callback from transport (called on network thread)
  void OnRtcpPacketReceived(rtc::CopyOnWriteBuffer *packet,
                            int64_t packet_time_us);

  void Disconnect();

  // Sender + owning PC
  rtc::scoped_refptr<webrtc::RtpSenderInterface> _sender;
  rtc::scoped_refptr<webrtc::PeerConnectionInterface> _pc;

  // Threading + transport
  rtc::Thread *_signaling_thread = nullptr;
  rtc::Thread *_network_thread = nullptr;
  webrtc::RtpTransportInternal *_rtp_transport = nullptr;

  bool _stopped = false;
  std::mutex _mutex;
};

}  // namespace node_webrtc


