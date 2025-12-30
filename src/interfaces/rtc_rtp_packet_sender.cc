/* Copyright (c) 2025 The node-webrtc project authors. All rights reserved.
 *
 * Use of this source code is governed by a BSD-style license that can be found
 * in the LICENSE.md file in the root of the source tree. All contributing
 * project authors may be found in the AUTHORS file in the root of the source
 * tree.
 */

#include "src/interfaces/rtc_rtp_packet_sender.hh"

#include <algorithm>
#include <atomic>
#include <cstdint>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <utility>
#include <vector>

#include <webrtc/api/peer_connection_interface.h>
#include <webrtc/api/rtp_transceiver_interface.h>
#include <webrtc/rtc_base/thread.h>

// Internal WebRTC headers (available in the libwebrtc source checkout)
#include "p2p/base/dtls_transport_internal.h"
#include "pc/peer_connection.h"
#include "pc/peer_connection_proxy.h"
#include "pc/rtp_transport_internal.h"
#include "rtc_base/async_packet_socket.h"
#include "rtc_base/copy_on_write_buffer.h"
#include "rtc_base/location.h"
#include "rtc_base/third_party/sigslot/sigslot.h"

#include "src/converters.hh"
#include "src/node/error_factory.hh"

namespace node_webrtc {

namespace {

std::string HexPrefix(const rtc::CopyOnWriteBuffer &buf, size_t max_bytes) {
  std::ostringstream oss;
  const auto *p = reinterpret_cast<const uint8_t *>(buf.cdata());
  const auto n = std::min(buf.size(), max_bytes);
  for (size_t i = 0; i < n; i++) {
    oss << std::hex << std::setw(2) << std::setfill('0')
        << static_cast<int>(p[i]);
  }
  return oss.str();
}

struct RtpHeaderSummary {
  int version = -1;
  bool extension = false;
  bool marker = false;
  int payload_type = -1;
  uint16_t seq = 0;
  uint32_t ts = 0;
  uint32_t ssrc = 0;
};

bool TryParseRtpHeader(const rtc::CopyOnWriteBuffer &buf, RtpHeaderSummary &out) {
  if (buf.size() < 12) {
    return false;
  }
  const auto *p = reinterpret_cast<const uint8_t *>(buf.cdata());
  const uint8_t b0 = p[0];
  const uint8_t b1 = p[1];
  out.version = (b0 >> 6) & 0x03;
  out.extension = (b0 & 0x10) != 0;
  out.marker = (b1 & 0x80) != 0;
  out.payload_type = b1 & 0x7f;
  out.seq = static_cast<uint16_t>((p[2] << 8) | p[3]);
  out.ts = (static_cast<uint32_t>(p[4]) << 24) |
           (static_cast<uint32_t>(p[5]) << 16) |
           (static_cast<uint32_t>(p[6]) << 8) | static_cast<uint32_t>(p[7]);
  out.ssrc = (static_cast<uint32_t>(p[8]) << 24) |
             (static_cast<uint32_t>(p[9]) << 16) |
             (static_cast<uint32_t>(p[10]) << 8) | static_cast<uint32_t>(p[11]);
  return out.version == 2;
}

std::atomic<int> g_send_rtp_false_logs{0};
std::atomic<int> g_send_rtcp_false_logs{0};
std::atomic<int> g_send_rtp_bypass_needed_logs{0};
std::atomic<int> g_send_rtcp_bypass_needed_logs{0};

struct SendAttemptDebug {
  bool ok = false;
  bool ok_flags0 = false;
  bool ok_srtp_bypass = false;
  bool writable = false;
  bool receiving = false;
};

}  // namespace

Napi::FunctionReference &RTCRtpPacketSender::constructor() {
  static Napi::FunctionReference constructor;
  return constructor;
}

// static
RTCPeerConnection *
RTCRtpPacketSender::UnwrapMaybePeerConnection(const Napi::Env &env,
                                              const Napi::Value &value) {
  if (!value.IsObject()) {
    return nullptr;
  }

  Napi::Object obj = value.As<Napi::Object>();

  // Allow passing the JS wrapper RTCPeerConnection (lib/peerconnection.js),
  // which stores the native binding object under the non-enumerable `_pc`.
  if (obj.Has("_pc")) {
    auto inner = obj.Get("_pc");
    if (inner.IsObject()) {
      obj = inner.As<Napi::Object>();
    }
  }

  auto isInstance = false;
  napi_instanceof(env, obj, RTCPeerConnection::constructor().Value(),
                  &isInstance);
  if (env.IsExceptionPending()) {
    env.GetAndClearPendingException();
    return nullptr;
  }
  if (!isInstance) {
    return nullptr;
  }

  return RTCPeerConnection::Unwrap(obj);
}

// static
rtc::scoped_refptr<webrtc::PeerConnectionInterface>
RTCRtpPacketSender::FindPeerConnectionForSender(
    const rtc::scoped_refptr<webrtc::RtpSenderInterface> &sender) {
  return RTCPeerConnection::FindPeerConnectionForSender(sender);
}

RTCRtpPacketSender::RTCRtpPacketSender(const Napi::CallbackInfo &info)
    : AsyncObjectWrapWithLoop<RTCRtpPacketSender>("RTCRtpPacketSender", *this,
                                                 info) {
  auto env = info.Env();

  if (!info.IsConstructCall()) {
    Napi::TypeError::New(env,
                         "Use the new operator to construct an "
                         "RTCRtpPacketSender.")
        .ThrowAsJavaScriptException();
    return;
  }

  if (info.Length() < 1) {
    Napi::TypeError::New(env, "RTCRtpPacketSender requires an RTCRtpSender")
        .ThrowAsJavaScriptException();
    return;
  }

  // First arg: RTCRtpSender (native binding object)
  auto maybeSender = From<RTCRtpSender *>(info[0]);
  if (maybeSender.IsInvalid()) {
    Napi::TypeError::New(env, "First argument must be an RTCRtpSender")
        .ThrowAsJavaScriptException();
    return;
  }
  auto senderWrap = maybeSender.UnsafeFromValid();
  _sender = senderWrap->sender();

  // Optional second arg: RTCPeerConnection (either JS wrapper or native)
  if (info.Length() >= 2) {
    auto pcWrap = UnwrapMaybePeerConnection(env, info[1]);
    if (!pcWrap) {
      Napi::TypeError::New(
          env,
          "Second argument must be an RTCPeerConnection (or wrapper with _pc)")
          .ThrowAsJavaScriptException();
      return;
    }
    _pc = pcWrap->jinglePeerConnection();
  } else {
    _pc = FindPeerConnectionForSender(_sender);
  }

  if (!_pc) {
    Napi::Error::New(env,
                     "Could not find owning RTCPeerConnection for RTCRtpSender")
        .ThrowAsJavaScriptException();
    return;
  }

  // The PeerConnection returned by WebRTC is a proxy. We need access to its
  // signaling thread and its internal (concrete) PeerConnection.
  auto pcProxy = static_cast<webrtc::PeerConnectionProxy *>(_pc.get());
  _signaling_thread = pcProxy->signaling_thread();

  if (!_signaling_thread) {
    Napi::Error::New(env, "PeerConnection signaling thread unavailable")
        .ThrowAsJavaScriptException();
    return;
  }

  // Resolve the MID for this sender and fetch the RtpTransportInternal (SRTP)
  // used for that MID.
  _signaling_thread->Invoke<void>(RTC_FROM_HERE, [this]() {
    if (_stopped) {
      return;
    }

    auto pcProxy = static_cast<webrtc::PeerConnectionProxy *>(_pc.get());
    auto *pcIface = pcProxy->internal(); // actually PeerConnection
    auto *pc = static_cast<webrtc::PeerConnection *>(pcIface);

    // Identify the transceiver MID corresponding to this sender.
    std::string mid;
    for (const auto &t : pcIface->GetTransceivers()) {
      if (!t) {
        continue;
      }
      auto s = t->sender();
      if (s.get() == _sender.get()) {
        auto maybeMid = t->mid();
        if (maybeMid.has_value()) {
          mid = *maybeMid;
        }
        break;
      }
    }

    if (mid.empty()) {
      // Mid isn't available until negotiation completes.
      _rtp_transport = nullptr;
      return;
    }

    {
      std::lock_guard<std::mutex> lock(_mutex);
      _mid = mid;
    }

    _network_thread = pc->network_thread();
    _rtp_transport = pc->GetRtpTransport(mid);

    if (_rtp_transport) {
      // Listen for inbound RTCP feedback (transport is network-thread-owned).
      auto transport = _rtp_transport;
      auto network_thread = _network_thread;
      if (network_thread) {
        network_thread->Invoke<void>(RTC_FROM_HERE, [this, transport]() {
          transport->SignalRtcpPacketReceived.connect(
              this, &RTCRtpPacketSender::OnRtcpPacketReceived);
        });
      }
    }
  });

  if (!_rtp_transport) {
    Napi::Error::New(
        env,
        "Failed to locate RtpTransport for sender (missing MID or transport)")
        .ThrowAsJavaScriptException();
    return;
  }
}

RTCRtpPacketSender::~RTCRtpPacketSender() { Disconnect(); }

void RTCRtpPacketSender::Disconnect() {
  webrtc::RtpTransportInternal *transport = nullptr;
  rtc::Thread *network_thread = nullptr;

  {
    std::lock_guard<std::mutex> lock(_mutex);
    if (_stopped) {
      return;
    }
    _stopped = true;
    transport = _rtp_transport;
    network_thread = _network_thread;
  }

  if (transport && network_thread) {
    network_thread->Invoke<void>(RTC_FROM_HERE, [this, transport]() {
      transport->SignalRtcpPacketReceived.disconnect(this);
    });
  }

  {
    std::lock_guard<std::mutex> lock(_mutex);
    _rtp_transport = nullptr;
    _network_thread = nullptr;
    _signaling_thread = nullptr;
    _pc = nullptr;
    _sender = nullptr;
  }
}

Napi::Value RTCRtpPacketSender::GetStopped(const Napi::CallbackInfo &info) {
  std::lock_guard<std::mutex> lock(_mutex);
  return Napi::Boolean::New(info.Env(), _stopped);
}

Napi::Value RTCRtpPacketSender::Stop(const Napi::CallbackInfo &info) {
  Disconnect();
  return info.Env().Undefined();
}

static bool
ReadPacketArg(const Napi::CallbackInfo &info, rtc::CopyOnWriteBuffer &out) {
  auto env = info.Env();
  if (info.Length() < 1) {
    Napi::TypeError::New(env, "Expected ArrayBuffer, TypedArray, or DataView")
        .ThrowAsJavaScriptException();
    return false;
  }

  Napi::ArrayBuffer arraybuffer;
  size_t byte_offset = 0;
  size_t byte_length = 0;

  if (info[0].IsTypedArray()) {
    auto typedArray = info[0].As<Napi::TypedArray>();
    arraybuffer = typedArray.ArrayBuffer();
    byte_offset = typedArray.ByteOffset();
    byte_length = typedArray.ByteLength();
  } else if (info[0].IsDataView()) {
    auto dataView = info[0].As<Napi::DataView>();
    arraybuffer = dataView.ArrayBuffer();
    byte_offset = dataView.ByteOffset();
    byte_length = dataView.ByteLength();
  } else if (info[0].IsArrayBuffer()) {
    arraybuffer = info[0].As<Napi::ArrayBuffer>();
    byte_length = arraybuffer.ByteLength();
  } else {
    Napi::TypeError::New(env, "Expected ArrayBuffer, TypedArray, or DataView")
        .ThrowAsJavaScriptException();
    return false;
  }

  auto content = static_cast<const char *>(arraybuffer.Data());
  out = rtc::CopyOnWriteBuffer(content + byte_offset, byte_length);
  return true;
}

Napi::Value RTCRtpPacketSender::SendRtp(const Napi::CallbackInfo &info) {
  rtc::CopyOnWriteBuffer packet;
  if (!ReadPacketArg(info, packet)) {
    return info.Env().Undefined();
  }

  webrtc::RtpTransportInternal *transport = nullptr;
  rtc::Thread *network_thread = nullptr;
  {
    std::lock_guard<std::mutex> lock(_mutex);
    if (_stopped || !_rtp_transport || !_network_thread) {
      Napi::Error(info.Env(),
                  ErrorFactory::CreateInvalidStateError(
                      info.Env(), "RTCRtpPacketSender is stopped"))
          .ThrowAsJavaScriptException();
      return info.Env().Undefined();
    }
    transport = _rtp_transport;
    network_thread = _network_thread;
  }

  // CopyOnWriteBuffer copy is cheap (ref-counted); keep `packet` intact for debug logging.
  auto dbg = network_thread->Invoke<SendAttemptDebug>(
      RTC_FROM_HERE, [transport, packet]() mutable {
        SendAttemptDebug out;

        // Helps distinguish transport readiness issues from packet issues.
        out.writable = transport->writable();
        out.receiving = transport->receiving();

        rtc::PacketOptions options = {};

        // First try: do NOT pass PF_SRTP_BYPASS at this layer. This is the
        // expected path for injecting *plaintext RTP* and letting libwebrtc
        // apply SRTP protection internally.
        {
          rtc::CopyOnWriteBuffer pkt = packet;
          out.ok_flags0 = transport->SendRtpPacket(&pkt, options, 0);
          if (out.ok_flags0) {
            out.ok = true;
            return out;
          }
        }

        // Fallback: some internal paths may accept only already-protected SRTP
        // when PF_SRTP_BYPASS is passed in.
        {
          rtc::CopyOnWriteBuffer pkt = packet;
          out.ok_srtp_bypass =
              transport->SendRtpPacket(&pkt, options, cricket::PF_SRTP_BYPASS);
          out.ok = out.ok_srtp_bypass;
          return out;
        }
      });
  const auto ok = dbg.ok;

  if (!ok) {
    // Log a small amount of high-signal debug data to help pinpoint why the
    // underlying transport refused to send. Limit spam: only first few.
    const auto n = g_send_rtp_false_logs.fetch_add(1);
    if (n < 8) {
      RtpHeaderSummary h;
      const bool rtp = TryParseRtpHeader(packet, h);
      std::string mid;
      {
        std::lock_guard<std::mutex> lock(_mutex);
        mid = _mid;
      }

      std::cerr << "[node-webrtc][RTCRtpPacketSender] sendRtp returned false"
                << " mid=" << (mid.empty() ? "(unknown)" : mid)
                << " transport=" << transport
                << " writable=" << (dbg.writable ? 1 : 0)
                << " receiving=" << (dbg.receiving ? 1 : 0)
                << " try0=" << (dbg.ok_flags0 ? 1 : 0)
                << " tryBypass=" << (dbg.ok_srtp_bypass ? 1 : 0)
                << " bytes=" << packet.size()
                << " head=" << HexPrefix(packet, 24);
      if (rtp) {
        std::cerr << " rtp{v=" << h.version << " pt=" << h.payload_type
                  << " m=" << (h.marker ? 1 : 0)
                  << " x=" << (h.extension ? 1 : 0)
                  << " seq=" << h.seq << " ts=" << h.ts << " ssrc=" << h.ssrc
                  << "}";
      } else {
        std::cerr << " rtp{parse=false}";
      }
      std::cerr << std::endl;
    }
  } else if (!dbg.ok_flags0 && dbg.ok_srtp_bypass) {
    // High-signal clue: sending only works if we pass PF_SRTP_BYPASS.
    const auto n = g_send_rtp_bypass_needed_logs.fetch_add(1);
    if (n < 2) {
      std::string mid;
      {
        std::lock_guard<std::mutex> lock(_mutex);
        mid = _mid;
      }
      std::cerr << "[node-webrtc][RTCRtpPacketSender] sendRtp succeeded only with PF_SRTP_BYPASS"
                << " mid=" << (mid.empty() ? "(unknown)" : mid)
                << " transport=" << transport
                << " bytes=" << packet.size() << std::endl;
    }
  }

  return Napi::Boolean::New(info.Env(), ok);
}

Napi::Value RTCRtpPacketSender::SendRtcp(const Napi::CallbackInfo &info) {
  rtc::CopyOnWriteBuffer packet;
  if (!ReadPacketArg(info, packet)) {
    return info.Env().Undefined();
  }

  webrtc::RtpTransportInternal *transport = nullptr;
  rtc::Thread *network_thread = nullptr;
  {
    std::lock_guard<std::mutex> lock(_mutex);
    if (_stopped || !_rtp_transport || !_network_thread) {
      Napi::Error(info.Env(),
                  ErrorFactory::CreateInvalidStateError(
                      info.Env(), "RTCRtpPacketSender is stopped"))
          .ThrowAsJavaScriptException();
      return info.Env().Undefined();
    }
    transport = _rtp_transport;
    network_thread = _network_thread;
  }

  // CopyOnWriteBuffer copy is cheap (ref-counted); keep `packet` intact for debug logging.
  auto dbg = network_thread->Invoke<SendAttemptDebug>(
      RTC_FROM_HERE, [transport, packet]() mutable {
        SendAttemptDebug out;
        out.writable = transport->writable();
        out.receiving = transport->receiving();

        rtc::PacketOptions options = {};

        // First try: let libwebrtc apply SRTCP protection.
        {
          rtc::CopyOnWriteBuffer pkt = packet;
          out.ok_flags0 = transport->SendRtcpPacket(&pkt, options, 0);
          if (out.ok_flags0) {
            out.ok = true;
            return out;
          }
        }

        // Fallback: bypass flag.
        {
          rtc::CopyOnWriteBuffer pkt = packet;
          out.ok_srtp_bypass =
              transport->SendRtcpPacket(&pkt, options, cricket::PF_SRTP_BYPASS);
          out.ok = out.ok_srtp_bypass;
          return out;
        }
      });
  const auto ok = dbg.ok;

  if (!ok) {
    const auto n = g_send_rtcp_false_logs.fetch_add(1);
    if (n < 8) {
      std::string mid;
      {
        std::lock_guard<std::mutex> lock(_mutex);
        mid = _mid;
      }
      std::cerr << "[node-webrtc][RTCRtpPacketSender] sendRtcp returned false"
                << " mid=" << (mid.empty() ? "(unknown)" : mid)
                << " transport=" << transport
                << " writable=" << (dbg.writable ? 1 : 0)
                << " receiving=" << (dbg.receiving ? 1 : 0)
                << " try0=" << (dbg.ok_flags0 ? 1 : 0)
                << " tryBypass=" << (dbg.ok_srtp_bypass ? 1 : 0)
                << " bytes=" << packet.size()
                << " head=" << HexPrefix(packet, 24) << std::endl;
    }
  } else if (!dbg.ok_flags0 && dbg.ok_srtp_bypass) {
    const auto n = g_send_rtcp_bypass_needed_logs.fetch_add(1);
    if (n < 2) {
      std::string mid;
      {
        std::lock_guard<std::mutex> lock(_mutex);
        mid = _mid;
      }
      std::cerr << "[node-webrtc][RTCRtpPacketSender] sendRtcp succeeded only with PF_SRTP_BYPASS"
                << " mid=" << (mid.empty() ? "(unknown)" : mid)
                << " transport=" << transport
                << " bytes=" << packet.size() << std::endl;
    }
  }

  return Napi::Boolean::New(info.Env(), ok);
}

void RTCRtpPacketSender::OnRtcpPacketReceived(rtc::CopyOnWriteBuffer *packet,
                                              int64_t) {
  if (!packet) {
    return;
  }

  {
    std::lock_guard<std::mutex> lock(_mutex);
    if (_stopped) {
      return;
    }
  }

  rtc::CopyOnWriteBuffer copy(*packet);

  Dispatch(CreateCallback<RTCRtpPacketSender>([this, copy = std::move(copy)]() {
    auto env = Env();
    Napi::HandleScope scope(env);

    auto data = Napi::Buffer<uint8_t>::Copy(
        env, reinterpret_cast<const uint8_t *>(copy.cdata()), copy.size());
    auto event = Napi::Object::New(env);
    event.Set("type", Napi::String::New(env, "rtcp"));
    event.Set("packet", data);
    MakeCallback("dispatchEvent", {event});
  }));
}

void RTCRtpPacketSender::Init(Napi::Env env, Napi::Object exports) {
  auto func = DefineClass(
      env, "RTCRtpPacketSender",
      {InstanceMethod("sendRtp", &RTCRtpPacketSender::SendRtp),
       InstanceMethod("sendRtcp", &RTCRtpPacketSender::SendRtcp),
       InstanceMethod("stop", &RTCRtpPacketSender::Stop),
       InstanceAccessor("stopped", &RTCRtpPacketSender::GetStopped, nullptr)});

  constructor() = Napi::Persistent(func);
  constructor().SuppressDestruct();

  exports.Set("RTCRtpPacketSender", func);
}

}  // namespace node_webrtc


