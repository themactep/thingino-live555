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
// C++ header

#ifndef _OPUS_AUDIO_RTP_SINK_HH
#define _OPUS_AUDIO_RTP_SINK_HH

#ifndef _AUDIO_RTP_SINK_HH
#include "AudioRTPSink.hh"
#endif

class OpusAudioRTPSink: public AudioRTPSink {
public:
  static OpusAudioRTPSink* createNew(UsageEnvironment& env,
				     Groupsock* RTPgs,
				     u_int8_t rtpPayloadFormat,
				     u_int32_t rtpTimestampFrequency,
				     unsigned numChannels = 2,
				     Boolean enableFEC = False,
				     Boolean enableDTX = False);

  // Opus-specific configuration methods:
  void setMaxPlaybackRate(unsigned rate) { fMaxPlaybackRate = rate; }
  void setStereoMode(Boolean stereo) { fStereoMode = stereo; }
  void setUseFEC(Boolean useFEC) { fUseFEC = useFEC; }
  void setUseDTX(Boolean useDTX) { fUseDTX = useDTX; }
  void setMaxAverageBitrate(unsigned bitrate) { fMaxAverageBitrate = bitrate; }

protected:
  OpusAudioRTPSink(UsageEnvironment& env, Groupsock* RTPgs,
		   u_int8_t rtpPayloadFormat,
		   u_int32_t rtpTimestampFrequency,
		   unsigned numChannels,
		   Boolean enableFEC, Boolean enableDTX);
	// called only by createNew()

  virtual ~OpusAudioRTPSink();

private: // redefined virtual functions:
  virtual char const* auxSDPLine(); // for the "a=fmtp:" SDP line

  virtual Boolean frameCanAppearAfterPacketStart(unsigned char const* frameStart,
						 unsigned numBytesInFrame) const;
  virtual void doSpecialFrameHandling(unsigned fragmentationOffset,
                                      unsigned char* frameStart,
                                      unsigned numBytesInFrame,
                                      struct timeval framePresentationTime,
                                      unsigned numRemainingBytes);
  virtual unsigned specialHeaderSize() const;

private:
  // Opus-specific parameters for SDP
  unsigned fMaxPlaybackRate;    // maxplaybackrate parameter
  Boolean fStereoMode;          // stereo parameter
  Boolean fUseFEC;              // useinbandfec parameter
  Boolean fUseDTX;              // usedtx parameter
  unsigned fMaxAverageBitrate;  // maxaveragebitrate parameter

  char* fFmtpSDPLine;           // Cached SDP fmtp line

  // Helper methods:
  void generateFmtpSDPLine();
  Boolean isOpusFrame(unsigned char const* frameStart,
                      unsigned numBytesInFrame) const;
};

#endif
