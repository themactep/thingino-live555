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
// A sink that generates a Matroska (MKV) file from a composite media session
// Implementation

#include "include/MatroskaFileSink.hh"
#include "include/InputFile.hh"
#include "include/OutputFile.hh"
#include "include/H264VideoRTPSource.hh"
#include "include/H264VideoStreamDiscreteFramer.hh"
#include "include/MPEGVideoStreamFramer.hh"
#include "GroupsockHelper.hh"
#include "EBMLNumber.hh"
#include <new>

////////// MatroskaFileSink implementation //////////

MatroskaFileSink* MatroskaFileSink::createNew(UsageEnvironment& env,
					      MediaSession& inputSession,
					      char const* outputFileName,
					      unsigned bufferSize,
					      unsigned short movieWidth,
					      unsigned short movieHeight,
					      unsigned movieFPS,
					      Boolean packetLossCompensate,
					      Boolean syncStreams) {
  MatroskaFileSink* newSink =
    new MatroskaFileSink(env, inputSession, outputFileName, bufferSize,
			 movieWidth, movieHeight, movieFPS,
			 packetLossCompensate, syncStreams);
  if (newSink == NULL || newSink->fOutFid == NULL) {
    Medium::close(newSink);
    return NULL;
  }

  return newSink;
}

MatroskaFileSink::MatroskaFileSink(UsageEnvironment& env,
				   MediaSession& inputSession,
				   char const* outputFileName,
				   unsigned bufferSize,
				   unsigned short movieWidth,
				   unsigned short movieHeight,
				   unsigned movieFPS,
				   Boolean packetLossCompensate,
				   Boolean syncStreams)
  : Medium(env), fInputSession(inputSession),
    fBufferSize(bufferSize), fPacketLossCompensate(packetLossCompensate),
    fSyncStreams(syncStreams), fAreCurrentlyBeingPlayed(False),
    fNumSubsessions(0), fNumSyncedSubsessions(0),
    fHaveCompletedOutputFile(False),
    fMovieWidth(movieWidth), fMovieHeight(movieHeight), fMovieFPS(movieFPS),
    fSegmentDataOffset(0), fCuesOffset(0), fSeekHeadOffset(0),
    fTimecodeScale(1000000), fSegmentDuration(0.0),
    fHaveSetStartTime(False),
    fVideoTrackNumber(1), fAudioTrackNumber(2),
    fHaveVideoTrack(False), fHaveAudioTrack(False),
    fVideoCodecId(NULL), fAudioCodecId(NULL),
    fVideoWidth(movieWidth), fVideoHeight(movieHeight),
    fAudioSamplingFrequency(48000), fAudioChannels(2),
    fH264CodecPrivateData(NULL), fH264CodecPrivateDataSize(0),
    fCurrentClusterOffset(0), fNeedNewCluster(True) {



  fOutFid = OpenOutputFile(env, outputFileName);
  if (fOutFid == NULL) {

    return;
  }


  // Initialize start time
  fStartTime.tv_sec = 0;
  fStartTime.tv_usec = 0;
  fCurrentClusterTimecode.tv_sec = 0;
  fCurrentClusterTimecode.tv_usec = 0;

  // Analyze the input session to determine track types
  MediaSubsessionIterator iter(fInputSession);
  MediaSubsession* subsession;
  while ((subsession = iter.next()) != NULL) {
    envir() << "MatroskaFileSink: Found subsession - medium: " << subsession->mediumName()
            << ", codec: " << subsession->codecName() << "\n";
    if (strcmp(subsession->mediumName(), "video") == 0) {
      fHaveVideoTrack = True;
      // Prefer frame rate from SDP if present
      if (subsession->videoFPS() > 0) {
        fMovieFPS = subsession->videoFPS();
      }
      if (strcmp(subsession->codecName(), "H264") == 0) {
        fVideoCodecId = "V_MPEG4/ISO/AVC";
        // Extract H.264 codec private data from SDP
        char const* spropParameterSets = subsession->fmtp_spropparametersets();
        if (spropParameterSets != NULL && spropParameterSets[0] != '\0') {
          extractH264CodecPrivateData(spropParameterSets);
        }
      } else if (strcmp(subsession->codecName(), "H265") == 0) {
        fVideoCodecId = "V_MPEGH/ISO/HEVC";
      } else {
        fVideoCodecId = "V_UNCOMPRESSED"; // fallback
      }
    } else if (strcmp(subsession->mediumName(), "audio") == 0) {
      fHaveAudioTrack = True;
      if (strcmp(subsession->codecName(), "OPUS") == 0) {
        fAudioCodecId = "A_OPUS";
        fAudioSamplingFrequency = subsession->rtpTimestampFrequency();
        // RFC 7587 mandates advertising "/2" in SDP regardless of actual encoded channels.
        // Our capture/encoder is mono; record true mono in MKV to avoid upmixing.
        fAudioChannels = 1;
      } else if (strcmp(subsession->codecName(), "VORBIS") == 0) {
        fAudioCodecId = "A_VORBIS";
        fAudioSamplingFrequency = subsession->rtpTimestampFrequency();
        fAudioChannels = subsession->numChannels();
      } else if (strcmp(subsession->codecName(), "MPEG4-GENERIC") == 0) {
        fAudioCodecId = "A_AAC";
        fAudioSamplingFrequency = subsession->rtpTimestampFrequency();
        fAudioChannels = subsession->numChannels();
      } else {
        fAudioCodecId = "A_PCM/INT/LIT"; // fallback
        fAudioSamplingFrequency = 48000; // default
        fAudioChannels = 2; // default
      }
    }
    ++fNumSubsessions;
  }

  envir() << "MatroskaFileSink: Track summary - Video: " << (fHaveVideoTrack ? "YES" : "NO")
          << ", Audio: " << (fHaveAudioTrack ? "YES" : "NO") << "\n";

  // Set up I/O state for each subsession:
  MediaSubsessionIterator iter2(fInputSession);
  MediaSubsession* subsession2;
  unsigned trackNumber = 1;
  while ((subsession2 = iter2.next()) != NULL) {
    if (subsession2->readSource() == NULL) continue; // was not initiated

    MatroskaSubsessionIOState* ioState = new MatroskaSubsessionIOState(*this, *subsession2);
    subsession2->miscPtr = ioState;

    if (strcmp(subsession2->mediumName(), "video") == 0) {
      ioState->setTrackNumber(fVideoTrackNumber);
    } else if (strcmp(subsession2->mediumName(), "audio") == 0) {
      ioState->setTrackNumber(fAudioTrackNumber);
    } else {
      ioState->setTrackNumber(trackNumber++);
    }
  }

  // Write EBML header and segment header
  writeEBMLHeader();
  writeSegmentHeader();


}

MatroskaFileSink::~MatroskaFileSink() {
  if (!fHaveCompletedOutputFile) {
    completeOutputFile();
  }

  if (fOutFid != NULL) {
    CloseOutputFile(fOutFid);
  }

  delete[] fH264CodecPrivateData;
}

Boolean MatroskaFileSink::startPlaying(afterPlayingFunc* afterFunc,
					void* afterClientData) {


  // Record the function (if any) to call when we're done playing data,
  // and start playing data:
  fAfterFunc = afterFunc;
  fAfterClientData = afterClientData;

  Boolean result = continuePlaying();

  return result;
}

Boolean MatroskaFileSink::continuePlaying() {


  if (!fAreCurrentlyBeingPlayed) {
    fAreCurrentlyBeingPlayed = True;

    // Write Matroska headers
    writeSeekHead();
    writeSegmentInfo();
    writeTracks();
  }

  // Run through each of our input session's 'subsessions',
  // asking for a frame from each one:
  Boolean haveActiveSubsessions = False;
  MediaSubsessionIterator iter(fInputSession);
  MediaSubsession* subsession;
  while ((subsession = iter.next()) != NULL) {
    MatroskaSubsessionIOState* ioState
      = (MatroskaSubsessionIOState*)(subsession->miscPtr);
    FramedSource* subsessionSource = ioState ? ioState->fSource : NULL;

    if (subsessionSource == NULL) continue;

    if (subsessionSource->isCurrentlyAwaitingData()) {

      continue;
    }

    if (ioState == NULL) {

      continue;
    }

    haveActiveSubsessions = True;
    unsigned char* toPtr = ioState->fBuffer;
    unsigned toSize = ioState->fBufferSize;

    subsessionSource->getNextFrame(toPtr, toSize,
                                   afterGettingFrame, ioState,
                                   onSourceClosure, ioState);
  }
  if (!haveActiveSubsessions) {

    envir().setResultMsg("No subsessions are currently active");
    return False;
  }


  return True;
}

void MatroskaFileSink::afterGettingFrame(void* clientData, unsigned packetDataSize,
                                         unsigned numTruncatedBytes,
                                         struct timeval presentationTime,
                                         unsigned /*durationInMicroseconds*/) {
  MatroskaSubsessionIOState* ioState = (MatroskaSubsessionIOState*)clientData;
  if (numTruncatedBytes > 0) {
    ioState->fOurSink.envir() << "MatroskaFileSink::afterGettingFrame(): The input frame data was too large for our buffer.  "
                              << numTruncatedBytes
                              << " bytes of trailing data was dropped!" << "\n";
    // Adaptively grow the subsession buffer to try to avoid future truncation
    unsigned oldSize = ioState->fBufferSize;
    unsigned needed = packetDataSize + numTruncatedBytes;
    // Add headroom (128 KB) and round up to nearest 64 KB
    unsigned newSize = needed + 131072;
    newSize = (newSize + 65535) & ~65535u;
    if (newSize <= oldSize) newSize = oldSize * 2; // fallback: at least double
    unsigned char* newBuf = new (std::nothrow) unsigned char[newSize];
    if (newBuf != NULL) {
      delete[] ioState->fBuffer;
      ioState->fBuffer = newBuf;
      ioState->fBufferSize = newSize;
      ioState->fOurSink.envir() << "MatroskaFileSink: Increased input buffer for subsession to " << newSize << " bytes\n";
    } else {
      ioState->fOurSink.envir() << "MatroskaFileSink: WARNING: Failed to grow input buffer (out of memory?)\n";
    }
  }
  ioState->afterGettingFrame(packetDataSize, presentationTime);
}

void MatroskaFileSink::onSourceClosure(void* clientData) {
  MatroskaSubsessionIOState* ioState = (MatroskaSubsessionIOState*)clientData;
  ioState->onSourceClosure();
}

void MatroskaFileSink::onSourceClosure1() {
  if (!fAreCurrentlyBeingPlayed) return; // we're not currently being played

  // Check whether *all* of the subsessions have closed.
  // If not, do nothing for now:
  MediaSubsessionIterator iter(fInputSession);
  MediaSubsession* subsession;
  while ((subsession = iter.next()) != NULL) {
    MatroskaSubsessionIOState* ioState = (MatroskaSubsessionIOState*)(subsession->miscPtr);
    if (ioState != NULL && ioState->fOurSourceIsActive) return; // this source hasn't closed
  }

  // All subsessions have closed, so we're done:
  completeOutputFile();
  fAreCurrentlyBeingPlayed = False;

  if (fAfterFunc != NULL) {
    (*fAfterFunc)(fAfterClientData);
  }
}

// EBML/Matroska writing helper functions
unsigned MatroskaFileSink::addByte(u_int8_t byte) {
  fputc(byte, fOutFid);
  return 1;
}

unsigned MatroskaFileSink::addWord(u_int32_t word) {
  // Add as big-endian:
  addByte(word >> 24);
  addByte(word >> 16);
  addByte(word >> 8);
  addByte(word);
  return 4;
}

unsigned MatroskaFileSink::add8ByteWord(u_int64_t word) {
  // Add as big-endian:
  addByte((u_int8_t)(word >> 56));
  addByte((u_int8_t)(word >> 48));
  addByte((u_int8_t)(word >> 40));
  addByte((u_int8_t)(word >> 32));
  addByte((u_int8_t)(word >> 24));
  addByte((u_int8_t)(word >> 16));
  addByte((u_int8_t)(word >> 8));
  addByte((u_int8_t)word);
  return 8;
}

unsigned MatroskaFileSink::addFloat(float value) {
  // Convert float to 32-bit big-endian representation
  union { float f; u_int32_t i; } converter;
  converter.f = value;
  return addWord(converter.i);
}

unsigned MatroskaFileSink::addEBMLNumber(u_int64_t number) {
  // Encode as EBML variable-length integer (VINT) according to RFC 8794
  if (number <= 126) { // 0x7E, reserve 0x7F for unknown size
    addByte(0x80 | (u_int8_t)number);
    return 1;
  } else if (number <= 16382) { // 0x3FFE, reserve 0x3FFF for unknown size
    addByte(0x40 | (u_int8_t)(number >> 8));
    addByte((u_int8_t)number);
    return 2;
  } else if (number <= 2097150) { // 0x1FFFFE, reserve 0x1FFFFF for unknown size
    addByte(0x20 | (u_int8_t)(number >> 16));
    addByte((u_int8_t)(number >> 8));
    addByte((u_int8_t)number);
    return 3;
  } else if (number <= 268435454) { // 0xFFFFFFE, reserve 0xFFFFFFF for unknown size
    addByte(0x10 | (u_int8_t)(number >> 24));
    addByte((u_int8_t)(number >> 16));
    addByte((u_int8_t)(number >> 8));
    addByte((u_int8_t)number);
    return 4;
  } else {
    // For larger numbers, use 8-byte encoding
    addByte(0x01);
    addByte((u_int8_t)(number >> 56));
    addByte((u_int8_t)(number >> 48));
    addByte((u_int8_t)(number >> 40));
    addByte((u_int8_t)(number >> 32));
    addByte((u_int8_t)(number >> 24));
    addByte((u_int8_t)(number >> 16));
    addByte((u_int8_t)(number >> 8));
    addByte((u_int8_t)number);
    return 8;
  }
}

unsigned MatroskaFileSink::addEBMLId(u_int32_t id) {
  // EBML IDs are variable length
  if (id <= 0xFF) {
    addByte((u_int8_t)id);
    return 1;
  } else if (id <= 0xFFFF) {
    addByte((u_int8_t)(id >> 8));
    addByte((u_int8_t)id);
    return 2;
  } else if (id <= 0xFFFFFF) {
    addByte((u_int8_t)(id >> 16));
    addByte((u_int8_t)(id >> 8));
    addByte((u_int8_t)id);
    return 3;
  } else {
    addWord(id);
    return 4;
  }
}

unsigned MatroskaFileSink::addEBMLSize(u_int64_t size) {
  return addEBMLNumber(size);
}

unsigned MatroskaFileSink::addEBMLUnknownSize(unsigned numBytes) {
  if (numBytes < 1) numBytes = 1;
  if (numBytes > 8) numBytes = 8;
  // First byte: marker bit for length followed by all value bits set to 1
  // For N bytes, first byte is (0xFF >> (N-1)), remaining bytes are 0xFF
  u_int8_t first = (u_int8_t)(0xFF >> (numBytes - 1));
  addByte(first);
  for (unsigned i = 1; i < numBytes; ++i) addByte(0xFF);
  return numBytes;
}


void MatroskaFileSink::writeEBMLHeader() {
  // EBML Header
  addEBMLId(MATROSKA_ID_EBML);
  addEBMLSize(27); // Size of EBML header content (corrected)

  // DocType
  addEBMLId(0x4282);
  addEBMLSize(8);
  fwrite("matroska", 1, 8, fOutFid);

  // DocTypeVersion
  addEBMLId(0x4287);
  addEBMLSize(1);
  addByte(4);

  // DocTypeReadVersion
  addEBMLId(0x4285);
  addEBMLSize(1);
  addByte(2);

  // EBMLMaxIDLength
  addEBMLId(0x42F2);
  addEBMLSize(1);
  addByte(4);

  // EBMLMaxSizeLength
  addEBMLId(0x42F3);
  addEBMLSize(1);
  addByte(8);
}

void MatroskaFileSink::writeSegmentHeader() {
  // Segment
  addEBMLId(MATROSKA_ID_SEGMENT);
  // Use unknown size for streaming; we'll not attempt to finalize
  addEBMLUnknownSize(8);
  fSegmentDataOffset = ftell(fOutFid);
}

void MatroskaFileSink::writeSeekHead() {
  fSeekHeadOffset = ftell(fOutFid);
  // SeekHead - placeholder for now
  addEBMLId(MATROSKA_ID_SEEK_HEAD);
  addEBMLSize(0); // Empty for now
}

void MatroskaFileSink::writeSegmentInfo() {
  // Segment Information
  addEBMLId(MATROSKA_ID_INFO);
  addEBMLSize(34); // Size of info content (corrected - removed null terminator)

  // TimecodeScale
  addEBMLId(MATROSKA_ID_TIMECODE_SCALE);
  addEBMLSize(4);
  addWord(fTimecodeScale);

  // MuxingApp
  addEBMLId(MATROSKA_ID_MUXING_APP);
  addEBMLSize(12);
  fwrite("live555-opus", 1, 12, fOutFid);

  // WritingApp
  addEBMLId(MATROSKA_ID_WRITING_APP);
  addEBMLSize(8);
  fwrite("openRTSP", 1, 8, fOutFid);
}

void MatroskaFileSink::writeTracks() {
  // Tracks
  addEBMLId(MATROSKA_ID_TRACKS);

  // Calculate tracks size precisely
  unsigned tracksSize = 0;
  if (fHaveVideoTrack) {
    // Video track entry content: TrackNumber(3) + TrackType(3) + CodecID(1+1+codeclen) + Video(1+1+12) + CodecPrivate(optional)
    unsigned videoCodecLen = strlen(fVideoCodecId);
    unsigned videoTrackSize = 18 + videoCodecLen; // Base size
    if (fH264CodecPrivateData != NULL && fH264CodecPrivateDataSize > 0) {
      // Add CodecPrivate: ID(2) + size(variable) + data
      unsigned codecPrivateSizeBytes = (fH264CodecPrivateDataSize <= 126) ? 1 :
                                       (fH264CodecPrivateDataSize <= 16382) ? 2 : 3;
      videoTrackSize += 2 + codecPrivateSizeBytes + fH264CodecPrivateDataSize;
    }
    // Add DefaultDuration if fps known: ID(3) + size(1) + data(4)
    if (fMovieFPS > 0) {
      videoTrackSize += 8;
    }
    // Calculate VINT size for track entry size
    unsigned trackEntrySizeBytes = (videoTrackSize <= 126) ? 1 :
                                   (videoTrackSize <= 16382) ? 2 : 3;
    tracksSize += 1 + trackEntrySizeBytes + videoTrackSize; // TRACK_ENTRY ID(1) + size + content
  }
  if (fHaveAudioTrack) {
    // Audio track entry content: TrackNumber(3) + TrackType(3) + CodecID(2+codeclen) + Audio(11) = 19+codeclen
    unsigned audioCodecLen = strlen(fAudioCodecId);
    unsigned audioTrackSize = 19 + audioCodecLen;
    // Calculate VINT size for track entry size
    unsigned trackEntrySizeBytes = (audioTrackSize <= 126) ? 1 :
                                   (audioTrackSize <= 16382) ? 2 : 3;
    tracksSize += 1 + trackEntrySizeBytes + audioTrackSize; // TRACK_ENTRY ID(1) + size + content
  }

  addEBMLSize(tracksSize);

  // Video track
  if (fHaveVideoTrack) {
    addEBMLId(MATROSKA_ID_TRACK_ENTRY);
    unsigned videoCodecLen = strlen(fVideoCodecId);
    unsigned videoTrackSize = 18 + videoCodecLen; // Base size
    if (fH264CodecPrivateData != NULL && fH264CodecPrivateDataSize > 0) {
      // Add CodecPrivate: ID(2) + size(variable) + data
      unsigned codecPrivateSizeBytes = (fH264CodecPrivateDataSize <= 126) ? 1 :
                                       (fH264CodecPrivateDataSize <= 16382) ? 2 : 3;
      videoTrackSize += 2 + codecPrivateSizeBytes + fH264CodecPrivateDataSize;
    }
    // Add DefaultDuration if fps known
    if (fMovieFPS > 0) { videoTrackSize += 8; }
    addEBMLSize(videoTrackSize);

    // TrackNumber
    addEBMLId(MATROSKA_ID_TRACK_NUMBER);
    addEBMLSize(1);
    addByte(fVideoTrackNumber);

    // TrackType (video = 1)
    addEBMLId(MATROSKA_ID_TRACK_TYPE);
    addEBMLSize(1);
    addByte(1);

    // CodecID
    addEBMLId(MATROSKA_ID_CODEC);
    unsigned codecIdLen = strlen(fVideoCodecId);
    addEBMLSize(codecIdLen);
    fwrite(fVideoCodecId, 1, codecIdLen, fOutFid);

    // CodecPrivate (H.264 SPS/PPS parameter sets)
    if (fH264CodecPrivateData != NULL && fH264CodecPrivateDataSize > 0) {
      addEBMLId(MATROSKA_ID_CODEC_PRIVATE);
      addEBMLSize(fH264CodecPrivateDataSize);
      fwrite(fH264CodecPrivateData, 1, fH264CodecPrivateDataSize, fOutFid);
    }

    // DefaultDuration (ns) if fps known
    if (fMovieFPS > 0) {
      u_int32_t defaultDuration = (u_int32_t)(1000000000ULL / fMovieFPS);
      addEBMLId(MATROSKA_ID_DEFAULT_DURATION);
      addEBMLSize(4);
      addWord(defaultDuration);
    }

    // Video settings
    addEBMLId(MATROSKA_ID_VIDEO);
    addEBMLSize(8); // PixelWidth(4) + PixelHeight(4)

    // PixelWidth
    addEBMLId(MATROSKA_ID_PIXEL_WIDTH);
    addEBMLSize(2);
    addByte(fVideoWidth >> 8);
    addByte(fVideoWidth);

    // PixelHeight
    addEBMLId(MATROSKA_ID_PIXEL_HEIGHT);
    addEBMLSize(2);
    addByte(fVideoHeight >> 8);
    addByte(fVideoHeight);
  }

  // Audio track
  if (fHaveAudioTrack) {
    addEBMLId(MATROSKA_ID_TRACK_ENTRY);
    unsigned audioCodecLen = strlen(fAudioCodecId);
    addEBMLSize(19 + audioCodecLen); // Precise size: TrackNumber(3) + TrackType(3) + CodecID(2+len) + Audio(11)

    // TrackNumber
    addEBMLId(MATROSKA_ID_TRACK_NUMBER);
    addEBMLSize(1);
    addByte(fAudioTrackNumber);

    // TrackType (audio = 2)
    addEBMLId(MATROSKA_ID_TRACK_TYPE);
    addEBMLSize(1);
    addByte(2);

    // CodecID
    addEBMLId(MATROSKA_ID_CODEC);
    unsigned codecIdLen = strlen(fAudioCodecId);
    addEBMLSize(codecIdLen);
    fwrite(fAudioCodecId, 1, codecIdLen, fOutFid);

    // Audio settings
    addEBMLId(MATROSKA_ID_AUDIO);
    addEBMLSize(9); // SamplingFrequency(6) + Channels(3)

    // SamplingFrequency
    addEBMLId(MATROSKA_ID_SAMPLING_FREQUENCY);
    addEBMLSize(4);
    addFloat((float)fAudioSamplingFrequency);

    // Channels
    addEBMLId(MATROSKA_ID_CHANNELS);
    addEBMLSize(1);
    addByte(fAudioChannels);
  }
}

void MatroskaFileSink::writeCues() {
  fCuesOffset = ftell(fOutFid);
  // Cues - placeholder for now
  addEBMLId(MATROSKA_ID_CUES);
  addEBMLSize(0); // Empty for now
}

void MatroskaFileSink::completeOutputFile() {
  if (fHaveCompletedOutputFile || fOutFid == NULL) return;

  writeCues();
  fHaveCompletedOutputFile = True;
}

void MatroskaFileSink::extractH264CodecPrivateData(char const* spropParameterSets) {
  // Parse the sprop-parameter-sets to extract SPS and PPS
  unsigned numSPropRecords;
  SPropRecord* sPropRecords = parseSPropParameterSets(spropParameterSets, numSPropRecords);

  if (sPropRecords == NULL || numSPropRecords == 0) {
    return;
  }

  // Find SPS and PPS
  unsigned char* sps = NULL;
  unsigned spsSize = 0;
  unsigned char* pps = NULL;
  unsigned ppsSize = 0;

  for (unsigned i = 0; i < numSPropRecords; ++i) {
    if (sPropRecords[i].sPropLength > 0) {
      u_int8_t nalType = sPropRecords[i].sPropBytes[0] & 0x1F;
      if (nalType == 7) { // SPS
        sps = sPropRecords[i].sPropBytes;
        spsSize = sPropRecords[i].sPropLength;
      } else if (nalType == 8) { // PPS
        pps = sPropRecords[i].sPropBytes;
        ppsSize = sPropRecords[i].sPropLength;
      }
    }
  }

  if (sps != NULL && pps != NULL) {
    // Create AVCC format codec private data
    unsigned totalSize = 6 + 2 + spsSize + 1 + 2 + ppsSize;

    fH264CodecPrivateData = new unsigned char[totalSize];
    fH264CodecPrivateDataSize = totalSize;

    unsigned char* ptr = fH264CodecPrivateData;

    // AVCC header
    *ptr++ = 1; // configurationVersion
    *ptr++ = sps[1]; // profile_idc
    *ptr++ = sps[2]; // profile_compatibility
    *ptr++ = sps[3]; // level_idc
    *ptr++ = 0xFF; // lengthSizeMinusOne (4 bytes - 1 = 3, with reserved bits)

    // SPS
    *ptr++ = 0xE1; // numOfSequenceParameterSets (1 with reserved bits)
    *ptr++ = (spsSize >> 8) & 0xFF; // SPS length high byte
    *ptr++ = spsSize & 0xFF; // SPS length low byte
    memcpy(ptr, sps, spsSize);
    ptr += spsSize;

    // PPS
    *ptr++ = 1; // numOfPictureParameterSets
    *ptr++ = (ppsSize >> 8) & 0xFF; // PPS length high byte
    *ptr++ = ppsSize & 0xFF; // PPS length low byte
    memcpy(ptr, pps, ppsSize);
  }

  // Clean up - SPropRecord destructor automatically deletes sPropBytes
  delete[] sPropRecords;
}

void MatroskaSubsessionIOState::processH264Frame(unsigned char* frameSource, unsigned frameSize) {
  // Detect Annex B start codes; if present, split into NAL units; else treat as single NAL
  const unsigned char* buf = frameSource;
  unsigned n = frameSize;
  // Search for 0x000001 or 0x00000001 patterns
  auto is_start = [](const unsigned char* p, const unsigned char* end) {
    if (p + 3 <= end && p[0] == 0 && p[1] == 0 && p[2] == 1) return 3u;
    if (p + 4 <= end && p[0] == 0 && p[1] == 0 && p[2] == 0 && p[3] == 1) return 4u;
    return 0u;
  };

  const unsigned char* end = buf + n;
  const unsigned char* p = buf;
  // Skip leading zeros
  while (p < end && *p == 0) ++p;

  unsigned sc = 0;
  if ((sc = is_start(p, end)) == 0) {
    // No start codes: treat whole buffer as one NAL
    appendH264NALToPending((unsigned char*)buf, n);
    return;
  }

  // We have Annex B: iterate over NAL units
  const unsigned char* nal_start = p + sc;
  const unsigned char* q = nal_start;
  while (q < end) {
    unsigned sc_len = is_start(q, end);
    if (sc_len > 0) {
      // NAL is [nal_start, q)
      if (q > nal_start) appendH264NALToPending((unsigned char*)nal_start, (unsigned)(q - nal_start));
      q += sc_len;
      nal_start = q;
    } else {
      ++q;
    }
  }
  if (end > nal_start) {
    appendH264NALToPending((unsigned char*)nal_start, (unsigned)(end - nal_start));
  }
}

void MatroskaFileSink::startNewCluster(struct timeval presentationTime) {
  fCurrentClusterOffset = ftell(fOutFid);
  fCurrentClusterTimecode = presentationTime;
  fNeedNewCluster = False;

  // Cluster
  addEBMLId(MATROSKA_ID_CLUSTER);
  addEBMLUnknownSize(8); // unknown cluster size (streaming)

  // Timecode
  addEBMLId(MATROSKA_ID_TIMECODE);

  // Calculate timecode in milliseconds
  u_int64_t timecode = 0;
  if (fHaveSetStartTime) {
    timecode = (presentationTime.tv_sec - fStartTime.tv_sec) * 1000 +
               (presentationTime.tv_usec - fStartTime.tv_usec) / 1000;
  } else {
    fStartTime = presentationTime;
    fHaveSetStartTime = True;
  }

  // Encode timecode as EBML variable-length integer
  if (timecode < 0xFF) {
    addEBMLSize(1);
    addByte((u_int8_t)timecode);
  } else if (timecode < 0xFFFF) {
    addEBMLSize(2);
    addByte((u_int8_t)(timecode >> 8));
    addByte((u_int8_t)timecode);
  } else if (timecode < 0xFFFFFF) {
    addEBMLSize(3);
    addByte((u_int8_t)(timecode >> 16));
    addByte((u_int8_t)(timecode >> 8));
    addByte((u_int8_t)timecode);
  } else {
    addEBMLSize(4);
    addWord((u_int32_t)timecode);
  }
}

////////// MatroskaSubsessionIOState implementation //////////

MatroskaSubsessionIOState::MatroskaSubsessionIOState(MatroskaFileSink& sink, MediaSubsession& subsession)
  : fOurSink(sink), fOurSubsession(subsession),
    fBuffer(NULL), fBufferSize(sink.fBufferSize), fTrackNumber(1),
    fSource(NULL), fOurSourceIsActive(False), fAfterFunc(NULL), fAfterClientData(NULL) {

  fBuffer = new unsigned char[fBufferSize];
  fPrevPresentationTime.tv_sec = 0;
  fPrevPresentationTime.tv_usec = 0;

  // Wrap the source with a framer for H.264 to ensure complete access units
  if (strcmp(fOurSubsession.codecName(), "H264") == 0) {
    // Include Annex B start codes in output to simplify AU parsing downstream
    fSource = H264VideoStreamDiscreteFramer::createNew(fOurSink.envir(), fOurSubsession.readSource(), /*includeStartCodeInOutput*/True, /*insertAUD*/False);
    // Prime the framer with SPS/PPS from SDP, in case the stream doesn't repeat them in-band
    H264or5VideoStreamFramer* vfr = dynamic_cast<H264or5VideoStreamFramer*>(fSource);
    if (vfr != NULL) {
      char const* sprop = fOurSubsession.fmtp_spropparametersets();
      if (sprop != NULL && sprop[0] != '\0') {
        unsigned num;
        SPropRecord* recs = parseSPropParameterSets(sprop, num);
        unsigned char* sps = NULL; unsigned spsSize = 0; unsigned char* pps = NULL; unsigned ppsSize = 0;
        for (unsigned i = 0; i < num; ++i) {
          if (recs[i].sPropLength > 0) {
            u_int8_t nalType = recs[i].sPropBytes[0] & 0x1F;
            if (nalType == 7) { sps = recs[i].sPropBytes; spsSize = recs[i].sPropLength; }
            else if (nalType == 8) { pps = recs[i].sPropBytes; ppsSize = recs[i].sPropLength; }
          }
        }
        if (sps != NULL && pps != NULL) {
          vfr->setVPSandSPSandPPS(NULL, 0, sps, spsSize, pps, ppsSize);
        }
        delete[] recs;
      }
    }
  } else {
    fSource = fOurSubsession.readSource();
  }

  // Ensure a sufficiently large initial buffer, especially for video IDRs
  if (strcmp(fOurSubsession.mediumName(), "video") == 0) {
    unsigned const minVideoBuf = 4*1024*1024; // 4 MB floor
    if (fBufferSize < minVideoBuf) {
      unsigned char* newBuf = new (std::nothrow) unsigned char[minVideoBuf];
      if (newBuf != NULL) {
        delete[] fBuffer;
        fBuffer = newBuf;
        fBufferSize = minVideoBuf;
        fOurSink.envir() << "MatroskaFileSink: Using initial video buffer " << fBufferSize << " bytes\n";
      }
    }
  } else if (strcmp(fOurSubsession.mediumName(), "audio") == 0) {
    unsigned const minAudioBuf = 256*1024; // 256 KB floor for safety
    if (fBufferSize < minAudioBuf) {
      unsigned char* newBuf = new (std::nothrow) unsigned char[minAudioBuf];
      if (newBuf != NULL) {
        delete[] fBuffer;
        fBuffer = newBuf;
        fBufferSize = minAudioBuf;
        fOurSink.envir() << "MatroskaFileSink: Using initial audio buffer " << fBufferSize << " bytes\n";
      }
    }
  }

  // Init H.264 pending AU aggregation state
  fPendingH264 = NULL;
  fPendingH264Size = 0;
  fPendingH264Capacity = 0;
  fHasPendingH264 = False;
  fPendingH264PTS.tv_sec = 0;
  fPendingH264PTS.tv_usec = 0;
}

MatroskaSubsessionIOState::~MatroskaSubsessionIOState() {
  delete[] fBuffer;
  delete[] fPendingH264;
}





void MatroskaSubsessionIOState::afterGettingFrame(unsigned packetDataSize, struct timeval presentationTime) {
  if (packetDataSize == 0) {
    onSourceClosure();
    return;
  }

  useFrame(fBuffer, packetDataSize, presentationTime);

  // Continue reading from our source:
  fOurSink.continuePlaying();
}

void MatroskaSubsessionIOState::useFrame(unsigned char* frameSource, unsigned frameSize,
                                         struct timeval presentationTime) {
  Boolean isH264 = (strcmp(fOurSubsession.mediumName(), "video") == 0 &&
                    strcmp(fOurSubsession.codecName(), "H264") == 0);

  if (isH264) {
    // Aggregate NAL units into a single access unit (frame) based on presentationTime
    if (!fHasPendingH264) {
      // Start a new access unit; use the PTS of the first NAL in the AU
      fPendingH264PTS = presentationTime;
      fHasPendingH264 = True;
      fPendingH264Size = 0;
    }

    // Append current NAL(s) to pending AU (convert Annex B to AVCC if needed)
    processH264Frame(frameSource, frameSize);

    // Prefer RTP marker bit to detect end-of-access-unit (RFC 6184):
    RTPSource* rtp = fOurSubsession.rtpSource();
    if (rtp != NULL && rtp->curPacketMarkerBit()) {
      flushPendingH264();
    }

    fPrevPresentationTime = presentationTime;
    return; // Defer writing until AU boundary
  }

  // Non-H.264: write a SimpleBlock immediately
  if (fOurSink.fNeedNewCluster) {
    fOurSink.startNewCluster(presentationTime);
  }

  fOurSink.addEBMLId(MATROSKA_ID_SIMPLEBLOCK);

  unsigned trackNumberSize;
  if (fTrackNumber <= 126) trackNumberSize = 1;
  else if (fTrackNumber <= 16382) trackNumberSize = 2;
  else if (fTrackNumber <= 2097150) trackNumberSize = 3;
  else if (fTrackNumber <= 268435454) trackNumberSize = 4;
  else trackNumberSize = 8;

  unsigned blockSize = trackNumberSize + 2 + 1 + frameSize;
  fOurSink.addEBMLSize(blockSize);

  fOurSink.addEBMLNumber(fTrackNumber);

  u_int64_t frameTimecode = 0, clusterTimecode = 0;
  if (fOurSink.fHaveSetStartTime) {
    frameTimecode = (presentationTime.tv_sec - fOurSink.fStartTime.tv_sec) * 1000 +
                    (presentationTime.tv_usec - fOurSink.fStartTime.tv_usec) / 1000;
    clusterTimecode = (fOurSink.fCurrentClusterTimecode.tv_sec - fOurSink.fStartTime.tv_sec) * 1000 +
                      (fOurSink.fCurrentClusterTimecode.tv_usec - fOurSink.fStartTime.tv_usec) / 1000;
  }
  int16_t relativeTimecode = (int16_t)(frameTimecode - clusterTimecode);
  fOurSink.addByte((u_int8_t)(relativeTimecode >> 8));
  fOurSink.addByte((u_int8_t)relativeTimecode);

  u_int8_t flags = 0x00;
  if (strcmp(fOurSubsession.mediumName(), "audio") == 0) flags = 0x80;
  fOurSink.addByte(flags);

  fwrite(frameSource, 1, frameSize, fOurSink.fOutFid);

  fPrevPresentationTime = presentationTime;
}


void MatroskaSubsessionIOState::appendH264NALToPending(unsigned char* frameSource, unsigned frameSize) {
  unsigned needed = fPendingH264Size + 4 + frameSize;
  if (needed > fPendingH264Capacity) {
    unsigned newCap = fPendingH264Capacity == 0 ? (needed + 1024) : fPendingH264Capacity;
    while (newCap < needed) newCap *= 2;
    unsigned char* newBuf = new unsigned char[newCap];
    if (fPendingH264Size > 0 && fPendingH264 != NULL) {
      memcpy(newBuf, fPendingH264, fPendingH264Size);
    }
    delete[] fPendingH264;
    fPendingH264 = newBuf;
    fPendingH264Capacity = newCap;
  }
  // 4-byte big-endian length prefix followed by NAL bytes
  u_int32_t n = frameSize;
  unsigned char* p = fPendingH264 + fPendingH264Size;
  p[0] = (u_int8_t)(n >> 24);
  p[1] = (u_int8_t)(n >> 16);
  p[2] = (u_int8_t)(n >> 8);
  p[3] = (u_int8_t)n;
  memcpy(p + 4, frameSource, frameSize);
  fPendingH264Size += 4 + frameSize;
}

void MatroskaSubsessionIOState::flushPendingH264() {
  if (!fHasPendingH264 || fPendingH264Size == 0) return;

  // Ensure we have a current cluster
  if (fOurSink.fNeedNewCluster) {
    fOurSink.startNewCluster(fPendingH264PTS);
  }

  // Determine keyframe (IDR present)
  Boolean isKey = False;
  unsigned off = 0;
  while (off + 5 <= fPendingH264Size) {
    u_int32_t len = (fPendingH264[off] << 24) | (fPendingH264[off+1] << 16) |
                    (fPendingH264[off+2] << 8) | fPendingH264[off+3];
    if (len == 0 || off + 4 + len > fPendingH264Size) break;
    u_int8_t nalType = fPendingH264[off + 4] & 0x1F;
    if (nalType == 5) { isKey = True; break; }
    off += 4 + len;
  }

  // Write SimpleBlock
  fOurSink.addEBMLId(MATROSKA_ID_SIMPLEBLOCK);

  unsigned trackNumberSize;
  if (fTrackNumber <= 126) trackNumberSize = 1;
  else if (fTrackNumber <= 16382) trackNumberSize = 2;
  else if (fTrackNumber <= 2097150) trackNumberSize = 3;
  else if (fTrackNumber <= 268435454) trackNumberSize = 4;
  else trackNumberSize = 8;

  unsigned blockSize = trackNumberSize + 2 + 1 + fPendingH264Size;
  fOurSink.addEBMLSize(blockSize);

  fOurSink.addEBMLNumber(fTrackNumber);

  u_int64_t frameTimecode = 0, clusterTimecode = 0;
  if (fOurSink.fHaveSetStartTime) {
    frameTimecode = (fPendingH264PTS.tv_sec - fOurSink.fStartTime.tv_sec) * 1000 +
                    (fPendingH264PTS.tv_usec - fOurSink.fStartTime.tv_usec) / 1000;
    clusterTimecode = (fOurSink.fCurrentClusterTimecode.tv_sec - fOurSink.fStartTime.tv_sec) * 1000 +
                      (fOurSink.fCurrentClusterTimecode.tv_usec - fOurSink.fStartTime.tv_usec) / 1000;
  }
  int16_t relativeTimecode = (int16_t)(frameTimecode - clusterTimecode);
  fOurSink.addByte((u_int8_t)(relativeTimecode >> 8));
  fOurSink.addByte((u_int8_t)relativeTimecode);

  u_int8_t flags = isKey ? 0x80 : 0x00;
  fOurSink.addByte(flags);

  fwrite(fPendingH264, 1, fPendingH264Size, fOurSink.fOutFid);

  // Reset pending buffer state
  fPendingH264Size = 0;
  fHasPendingH264 = False;
}


void MatroskaSubsessionIOState::onSourceClosure() {
  // Flush any pending H.264 access unit before closing
  if (fHasPendingH264 && fPendingH264Size > 0) {
    flushPendingH264();
  }
  fOurSourceIsActive = False;
  fOurSink.onSourceClosure1();

  if (fAfterFunc != NULL) {
    (*fAfterFunc)(fAfterClientData);
  }
}
