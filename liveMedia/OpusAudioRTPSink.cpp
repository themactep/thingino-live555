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
// RTP sink for Opus audio (RFC 7587)
// Implementation

#include "OpusAudioRTPSink.hh"
#include <string.h>

OpusAudioRTPSink::OpusAudioRTPSink(UsageEnvironment& env, Groupsock* RTPgs,
				   u_int8_t rtpPayloadFormat,
				   u_int32_t rtpTimestampFrequency,
				   unsigned numChannels,
				   Boolean enableFEC, Boolean enableDTX)
  : AudioRTPSink(env, RTPgs, rtpPayloadFormat,
		 rtpTimestampFrequency, "OPUS", numChannels),
    fMaxPlaybackRate(48000), fStereoMode(numChannels > 1),
    fUseFEC(enableFEC), fUseDTX(enableDTX), fMaxAverageBitrate(0),
    fFmtpSDPLine(NULL) {
}

OpusAudioRTPSink::~OpusAudioRTPSink() {
  delete[] fFmtpSDPLine;
}

OpusAudioRTPSink*
OpusAudioRTPSink::createNew(UsageEnvironment& env, Groupsock* RTPgs,
			    u_int8_t rtpPayloadFormat,
			    u_int32_t rtpTimestampFrequency,
			    unsigned numChannels,
			    Boolean enableFEC, Boolean enableDTX) {
  return new OpusAudioRTPSink(env, RTPgs, rtpPayloadFormat,
			      rtpTimestampFrequency, numChannels,
			      enableFEC, enableDTX);
}

Boolean OpusAudioRTPSink
::frameCanAppearAfterPacketStart(unsigned char const* frameStart,
				 unsigned numBytesInFrame) const {
  // RFC 7587: An RTP payload MUST contain exactly one Opus packet
  // Therefore, only one frame per packet is allowed
  return False;
}

void OpusAudioRTPSink
::doSpecialFrameHandling(unsigned fragmentationOffset,
			 unsigned char* frameStart,
			 unsigned numBytesInFrame,
			 struct timeval framePresentationTime,
			 unsigned numRemainingBytes) {
  // RFC 7587: Opus RTP payload format has no special header
  // The RTP payload contains the Opus packet directly

  // Validate that this is an Opus frame
  if (!isOpusFrame(frameStart, numBytesInFrame)) {
    // Invalid Opus frame - handle gracefully
    return;
  }

  // No special header needed for Opus - payload starts with Opus data
  // The RTP timestamp handling is done by the base class
}

unsigned OpusAudioRTPSink::specialHeaderSize() const {
  // RFC 7587: No special header for Opus RTP payload format
  return 0;
}

char const* OpusAudioRTPSink::auxSDPLine() {
  if (fFmtpSDPLine == NULL) {
    generateFmtpSDPLine();
  }
  return fFmtpSDPLine;
}

void OpusAudioRTPSink::generateFmtpSDPLine() {
  // Generate SDP fmtp line according to RFC 7587
  char buffer[500];
  char* p = buffer;

  // Start with basic format
  sprintf(p, "a=fmtp:%d", rtpPayloadType());
  p += strlen(p);

  // Add maxplaybackrate parameter (RFC 7587)
  // This is a hint about the maximum output sampling rate
  if (fMaxPlaybackRate != 48000) {
    sprintf(p, " maxplaybackrate=%u", fMaxPlaybackRate);
    p += strlen(p);
  }

  // Add stereo parameter (RFC 7587)
  // Specifies whether decoder prefers receiving stereo or mono
  sprintf(p, " stereo=%d", fStereoMode ? 1 : 0);
  p += strlen(p);

  // Add useinbandfec parameter (RFC 7587)
  // Specifies that decoder can take advantage of Opus in-band FEC
  if (fUseFEC) {
    sprintf(p, " useinbandfec=1");
    p += strlen(p);
  }

  // Add usedtx parameter (RFC 7587)
  // Specifies if decoder prefers the use of DTX
  if (fUseDTX) {
    sprintf(p, " usedtx=1");
    p += strlen(p);
  }

  // Add maxaveragebitrate parameter (RFC 7587)
  // Specifies maximum average receive bitrate in bits per second
  if (fMaxAverageBitrate > 0) {
    sprintf(p, " maxaveragebitrate=%u", fMaxAverageBitrate);
    p += strlen(p);
  }

  // Add cbr parameter if constant bitrate is preferred
  // (This could be made configurable in the future)

  sprintf(p, "\r\n");

  fFmtpSDPLine = strDup(buffer);
}

Boolean OpusAudioRTPSink
::isOpusFrame(unsigned char const* frameStart,
	      unsigned numBytesInFrame) const {
  // Basic validation of Opus frame
  if (numBytesInFrame == 0) {
    // Could be DTX packet
    return fUseDTX;
  }

  if (numBytesInFrame < 1) {
    return False; // Too small for valid Opus packet
  }

  // Basic Opus packet validation (simplified)
  // A complete validation would require libopus
  unsigned char firstByte = frameStart[0];

  // Check if configuration number is valid (0-31)
  unsigned char config = firstByte & 0x1F;
  if (config > 31) {
    return False;
  }

  return True;
}
