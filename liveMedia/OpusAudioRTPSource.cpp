/**********
This library is free software; you can redistribute it and/or modify it under
the terms of the GNU Lesser General Public License as published by the
Free Software Foundation; either version 3 of the License, or (at your
option) any later version. (See <http://www.gnu.org/copyleft/lesser.html>.)

This library is distributed in the hope that it will be useful, but WITHOUT
ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
FOR A PARTICULAR PURPOSE.  See the GNU Lesser General Public License for
more details.

You should have received a copy of the GNU Lesser General Public License
along with this library; if not, write to the Free Software Foundation, Inc.,
51 Franklin Street, Fifth Floor, Boston, MA 02110-1301  USA
**********/
// "liveMedia"
// Copyright (c) 1996-2025 Live Networks, Inc.  All rights reserved.
// Opus Audio RTP Sources (RFC 7587)
// Implementation

#include "OpusAudioRTPSource.hh"

OpusAudioRTPSource*
OpusAudioRTPSource::createNew(UsageEnvironment& env,
			      Groupsock* RTPgs,
			      unsigned char rtpPayloadFormat,
			      unsigned rtpTimestampFrequency,
			      unsigned maxPlaybackRate,
			      Boolean stereo, Boolean useFEC, Boolean useDTX,
			      unsigned maxAverageBitrate) {
  return new OpusAudioRTPSource(env, RTPgs, rtpPayloadFormat,
				rtpTimestampFrequency, maxPlaybackRate,
				stereo, useFEC, useDTX, maxAverageBitrate);
}

OpusAudioRTPSource::OpusAudioRTPSource(UsageEnvironment& env,
				       Groupsock* rtpGS,
				       unsigned char rtpPayloadFormat,
				       unsigned rtpTimestampFrequency,
				       unsigned maxPlaybackRate,
				       Boolean stereo, Boolean useFEC, Boolean useDTX,
				       unsigned maxAverageBitrate)
  : MultiFramedRTPSource(env, rtpGS,
			 rtpPayloadFormat, rtpTimestampFrequency),
    fHasFEC(False), fIsDTX(False), fConfigurationNumber(0), fIsStereo(False),
    fMaxPlaybackRate(maxPlaybackRate), fStereoMode(stereo),
    fUseFEC(useFEC), fUseDTX(useDTX), fMaxAverageBitrate(maxAverageBitrate) {
}

OpusAudioRTPSource::~OpusAudioRTPSource() {
}

Boolean OpusAudioRTPSource
::processSpecialHeader(BufferedPacket* packet,
		       unsigned& resultSpecialHeaderSize) {
  unsigned char* headerStart = packet->data();
  unsigned packetSize = packet->dataSize();

  // Opus RTP payload format (RFC 7587) has no special RTP header
  // The payload contains the Opus packet directly
  resultSpecialHeaderSize = 0;

  // Handle DTX (Discontinuous Transmission)
  if (!handleDTXPacket(packet)) {
    return False;
  }

  if (packetSize == 0) {
    // DTX packet processed
    return True;
  }

  // Extract configuration information from Opus packet
  extractOpusConfiguration(headerStart, packetSize);

  // Process FEC (Forward Error Correction) if present
  if (!processFECData(packet)) {
    return False;
  }

  // Validate minimum Opus packet size
  if (packetSize < 1) {
    return False; // Invalid Opus packet
  }

  return True;
}

char const* OpusAudioRTPSource::MIMEtype() const {
  return "audio/opus";
}

Boolean OpusAudioRTPSource
::parseOpusPayloadHeader(unsigned char const* headerStart,
			 unsigned headerSize,
			 unsigned& payloadOffset) {
  // Opus RTP payload format (RFC 7587) doesn't have a payload header
  // The RTP payload contains the Opus packet directly
  payloadOffset = 0;
  return True;
}

void OpusAudioRTPSource
::extractOpusConfiguration(unsigned char const* payload,
			   unsigned payloadSize) {
  if (payloadSize == 0) return;

  // Extract configuration from the first byte of Opus packet
  // According to RFC 6716 (Opus specification)
  unsigned char firstByte = payload[0];

  // Opus packet structure (RFC 6716, Section 3.1):
  // - Bits 0-2: Configuration number (0-31)
  // - Bit 3: Stereo flag (0=mono, 1=stereo)
  // - Bits 4-5: Frame count code
  // - Bits 6-7: Reserved/padding

  fConfigurationNumber = firstByte & 0x1F; // Bits 0-4 (5 bits)
  fIsStereo = (firstByte & 0x04) != 0;     // Bit 2 (simplified stereo detection)

  // FEC detection: Look for FEC data in subsequent packets
  // This is a simplified approach - full FEC detection requires
  // analyzing packet sequences and SDP parameters
  if (payloadSize > 1) {
    // Check if this could be an FEC packet by examining packet structure
    // FEC packets typically have different patterns
    unsigned char secondByte = payload[1];
    // This is a heuristic - proper FEC detection needs more context
    fHasFEC = (secondByte & 0x80) != 0; // MSB might indicate FEC presence
  }

  // Note: Complete Opus packet parsing would require libopus
  // This implementation provides basic functionality for RTP handling
}

unsigned OpusAudioRTPSource
::getOpusFrameDuration(unsigned char const* payload,
		       unsigned payloadSize) {
  if (payloadSize == 0) return 0;

  // Extract frame duration from Opus packet header (RFC 6716)
  unsigned char firstByte = payload[0];
  unsigned char config = firstByte & 0x1F; // Configuration number (bits 0-4)

  // Opus frame durations based on configuration (simplified mapping)
  // This is a basic implementation - full parsing requires libopus
  static const unsigned frameDurations[] = {
    // Frame durations in samples at 48kHz (RFC 7587)
    120,  240,  480,  960,  // 2.5, 5, 10, 20 ms
    1920, 2880, 120,  240,  // 40, 60, 2.5, 5 ms
    480,  960,  1920, 2880, // 10, 20, 40, 60 ms
    120,  240,  480,  960,  // 2.5, 5, 10, 20 ms
    1920, 2880, 120,  240,  // 40, 60, 2.5, 5 ms
    480,  960,  1920, 2880, // 10, 20, 40, 60 ms
    120,  240,  480,  960,  // 2.5, 5, 10, 20 ms
    1920, 2880, 960,  960   // 40, 60, 20, 20 ms (default)
  };

  if (config < sizeof(frameDurations)/sizeof(frameDurations[0])) {
    return frameDurations[config];
  }

  return 960; // Default to 20ms frame (960 samples at 48kHz)
}

unsigned OpusAudioRTPSource
::convertTimestampTo48kHz(unsigned timestamp, unsigned originalFreq) {
  // RFC 7587: RTP timestamp is always incremented with 48000 Hz clock rate
  // regardless of the actual Opus encoding sampling rate
  if (originalFreq == 48000) {
    return timestamp;
  }

  // Convert from original frequency to 48kHz
  return (unsigned)((timestamp * 48000ULL) / originalFreq);
}

Boolean OpusAudioRTPSource
::processFECData(BufferedPacket* packet) {
  // Forward Error Correction processing for Opus (RFC 6716, Section 2.1.7)
  // FEC data contains redundant information about the previous packet

  if (!fHasFEC) return True; // No FEC to process

  unsigned char* payload = packet->data();
  unsigned payloadSize = packet->dataSize();

  if (payloadSize < 2) return False; // Too small for FEC data

  // FEC processing would typically involve:
  // 1. Detecting FEC presence in current packet
  // 2. Extracting FEC data for previous packet
  // 3. Using FEC data for error concealment if previous packet was lost

  // This is a simplified implementation - full FEC processing
  // requires integration with Opus decoder and jitter buffer

  return True;
}

Boolean OpusAudioRTPSource
::handleDTXPacket(BufferedPacket* packet) {
  // Discontinuous Transmission handling
  // DTX is indicated by empty RTP packets or gaps in sequence numbers

  if (packet->dataSize() == 0) {
    // Empty packet indicates DTX period
    fIsDTX = True;
    return True;
  }

  // Check for DTX based on packet content
  // In Opus, DTX can also be signaled within the packet
  unsigned char* payload = packet->data();
  if (payload[0] == 0) {
    // Null packet - DTX indication
    fIsDTX = True;
    return True;
  }

  fIsDTX = False;
  return True;
}
