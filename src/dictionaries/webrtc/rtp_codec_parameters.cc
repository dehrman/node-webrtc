#include "src/dictionaries/webrtc/rtp_codec_parameters.hh"

#include <cstdint>
#include <iosfwd>
#include <string>
#include <unordered_map>
#include <utility>

#include <absl/types/optional.h>
#include <node-addon-api/napi.h>
#include <webrtc/api/rtp_parameters.h>

#include "src/converters.hh"
#include "src/converters/object.hh"
#include "src/dictionaries/macros/napi.hh"
#include "src/functional/curry.hh"
#include "src/functional/maybe.hh"
#include "src/functional/operators.hh"
#include "src/functional/validation.hh"

namespace node_webrtc {

TO_NAPI_IMPL(webrtc::RtpCodecParameters, pair) {
  auto env = pair.first;
  Napi::EscapableHandleScope scope(env);
  auto params = pair.second;
  NODE_WEBRTC_CREATE_OBJECT_OR_RETURN(env, object)
  NODE_WEBRTC_CONVERT_AND_SET_OR_RETURN(env, object, "payloadType",
                                        params.payload_type)
  NODE_WEBRTC_CONVERT_AND_SET_OR_RETURN(env, object, "mimeType",
                                        params.mime_type())
  if (params.clock_rate) {
    NODE_WEBRTC_CONVERT_AND_SET_OR_RETURN(env, object, "clockRate",
                                          *params.clock_rate)
  }
  if (params.num_channels) {
    NODE_WEBRTC_CONVERT_AND_SET_OR_RETURN(env, object, "channels",
                                          *params.num_channels)
  }
  if (!params.parameters.empty()) {
    // Format: "a=fmtp:PT key1=value1; key2=value2"
    // This format is what browsers return and what the parsing code expects
    std::string fmtp("a=fmtp:" + std::to_string(params.payload_type));
    uint64_t i = 0;
    for (const auto &param : params.parameters) {
      fmtp += " " + param.first + "=" + param.second;
      if (i < params.parameters.size() - 1) {
        fmtp += ";";
      }
      i++;
    }
    NODE_WEBRTC_CONVERT_AND_SET_OR_RETURN(env, object, "sdpFmtpLine", fmtp)
  }
  return Pure(scope.Escape(object));
}

static webrtc::RtpCodecParameters NapiToRtpCodecParameters(
    const uint8_t payloadType, const std::string &mimeType,
    const uint64_t clockRate, const node_webrtc::Maybe<uint8_t> channels,
    const node_webrtc::Maybe<std::string> &maybeSdpFmtpLine) {
  webrtc::RtpCodecParameters result;
  auto indexOfSlash = mimeType.find('/');
  auto kindString = mimeType.substr(0, indexOfSlash);
  auto nameString = mimeType.substr(indexOfSlash + 1);
  result.kind = kindString == "audio" ? cricket::MEDIA_TYPE_AUDIO
                                      : cricket::MEDIA_TYPE_VIDEO;
  result.name = nameString;
  result.payload_type = payloadType;
  result.clock_rate = clockRate;
  if (channels.IsJust()) {
    result.num_channels = channels.UnsafeFromJust();
  }
  if (maybeSdpFmtpLine.IsJust()) {
    auto sdpFmtpLine = maybeSdpFmtpLine.UnsafeFromJust();
    
    // Handle "a=fmtp:PT " prefix if present
    // Format is: "a=fmtp:96 key1=value1; key2=value2"
    auto fmtpPrefix = std::string("a=fmtp:");
    if (sdpFmtpLine.find(fmtpPrefix) == 0) {
      // Find the space after "a=fmtp:PT"
      auto spacePos = sdpFmtpLine.find(' ');
      if (spacePos != std::string::npos) {
        sdpFmtpLine = sdpFmtpLine.substr(spacePos + 1);
      } else {
        // No parameters after the prefix
        sdpFmtpLine = "";
      }
    }
    
    if (!sdpFmtpLine.empty()) {
      // Now parse "key1=value1; key2=value2" format
      sdpFmtpLine += ";";  // Add trailing semicolon for easier parsing
      size_t pos = 0;
      std::string keyValue;
      while ((pos = sdpFmtpLine.find(";")) != std::string::npos) {
        keyValue = sdpFmtpLine.substr(0, pos);
        sdpFmtpLine.erase(0, pos + 1);
        
        // Trim leading/trailing whitespace from keyValue
        while (!keyValue.empty() && keyValue[0] == ' ') {
          keyValue.erase(0, 1);
        }
        while (!keyValue.empty() && keyValue[keyValue.size() - 1] == ' ') {
          keyValue.erase(keyValue.size() - 1);
        }
        
        if (keyValue.empty()) continue;
        
        auto indexOfEquals = keyValue.find('=');
        if (indexOfEquals != std::string::npos) {
          auto key = keyValue.substr(0, indexOfEquals);
          auto value = keyValue.substr(indexOfEquals + 1);
          result.parameters[key] = value;
        }
      }
    }
  }
  return result;
}

FROM_NAPI_IMPL(webrtc::RtpCodecParameters, value) {
  return From<Napi::Object>(value).FlatMap<webrtc::RtpCodecParameters>(
      [](auto object) {
        return curry(NapiToRtpCodecParameters) %
               GetRequired<uint8_t>(object, "payloadType") *
               GetRequired<std::string>(object, "mimeType") *
               GetRequired<uint64_t>(object, "clockRate") *
               GetOptional<uint8_t>(object, "channels") *
               GetOptional<std::string>(object, "sdpFmtpLine");
      });
}

} // namespace node_webrtc
