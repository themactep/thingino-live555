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
// C++ header

#ifndef _OPUS_AUDIO_RTP_SOURCE_HH
#define _OPUS_AUDIO_RTP_SOURCE_HH

#ifndef _MULTI_FRAMED_RTP_SOURCE_HH
#include "MultiFramedRTPSource.hh"
#endif

class OpusAudioRTPSource: public MultiFramedRTPSource {
public:
  static OpusAudioRTPSource*
  createNew(UsageEnvironment& env, Groupsock* RTPgs,
	    unsigned char rtpPayloadFormat,
	    unsigned rtpTimestampFrequency,
	    unsigned maxPlaybackRate = 48000,
	    Boolean stereo = False,
	    Boolean useFEC = False,
	    Boolean useDTX = False,
	    unsigned maxAverageBitrate = 0);

  // Opus-specific accessors:
  Boolean hasFEC() const { return fHasFEC; }
  Boolean isDTX() const { return fIsDTX; }
  unsigned char configurationNumber() const { return fConfigurationNumber; }
  Boolean isStereo() const { return fIsStereo; }

  // SDP parameter accessors:
  unsigned maxPlaybackRate() const { return fMaxPlaybackRate; }
  Boolean stereoMode() const { return fStereoMode; }
  Boolean useFEC() const { return fUseFEC; }
  Boolean useDTX() const { return fUseDTX; }
  unsigned maxAverageBitrate() const { return fMaxAverageBitrate; }

protected:
  OpusAudioRTPSource(UsageEnvironment& env, Groupsock* RTPgs,
		     unsigned char rtpPayloadFormat,
		     unsigned rtpTimestampFrequency,
		     unsigned maxPlaybackRate,
		     Boolean stereo, Boolean useFEC, Boolean useDTX,
		     unsigned maxAverageBitrate);
      // called only by createNew()

  virtual ~OpusAudioRTPSource();

protected:
  // redefined virtual functions:
  virtual Boolean processSpecialHeader(BufferedPacket* packet,
                                       unsigned& resultSpecialHeaderSize);
  virtual char const* MIMEtype() const;

private:
  // Opus-specific state:
  Boolean fHasFEC;              // Forward Error Correction present
  Boolean fIsDTX;               // Discontinuous Transmission
  unsigned char fConfigurationNumber; // Opus configuration number (0-31)
  Boolean fIsStereo;            // Stereo encoding

  // SDP parameters (RFC 7587):
  unsigned fMaxPlaybackRate;    // maxplaybackrate parameter
  Boolean fStereoMode;          // stereo parameter from SDP
  Boolean fUseFEC;              // useinbandfec parameter
  Boolean fUseDTX;              // usedtx parameter
  unsigned fMaxAverageBitrate;  // maxaveragebitrate parameter

  // Helper methods:
  Boolean parseOpusPayloadHeader(unsigned char const* headerStart,
                                 unsigned headerSize,
                                 unsigned& payloadOffset);
  void extractOpusConfiguration(unsigned char const* payload,
                                unsigned payloadSize);
  unsigned getOpusFrameDuration(unsigned char const* payload,
                                unsigned payloadSize);
  unsigned convertTimestampTo48kHz(unsigned timestamp,
                                   unsigned originalFreq);
  Boolean processFECData(BufferedPacket* packet);
  Boolean handleDTXPacket(BufferedPacket* packet);
};

#endif
