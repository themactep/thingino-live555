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
// C++ header

#ifndef _MATROSKA_FILE_SINK_HH
#define _MATROSKA_FILE_SINK_HH

#ifndef _MEDIA_SESSION_HH
#include "MediaSession.hh"
#endif
#ifndef _FILE_SINK_HH
#include "FileSink.hh"
#endif
#ifndef _FRAMED_SOURCE_HH
#include "FramedSource.hh"
#endif

class MatroskaFileSink: public Medium {
public:
  static MatroskaFileSink* createNew(UsageEnvironment& env,
				     MediaSession& inputSession,
				     char const* outputFileName,
				     unsigned bufferSize = 20000,
				     unsigned short movieWidth = 240,
				     unsigned short movieHeight = 180,
				     unsigned movieFPS = 15,
				     Boolean packetLossCompensate = False,
				     Boolean syncStreams = False);

  typedef void (afterPlayingFunc)(void* clientData);
  Boolean startPlaying(afterPlayingFunc* afterFunc,
                       void* afterClientData);

  unsigned numActiveSubsessions() const { return fNumSubsessions; }

private:
  MatroskaFileSink(UsageEnvironment& env,
		   MediaSession& inputSession,
		   char const* outputFileName,
		   unsigned bufferSize,
		   unsigned short movieWidth, unsigned short movieHeight,
		   unsigned movieFPS,
		   Boolean packetLossCompensate,
		   Boolean syncStreams);
      // called only by createNew()
  virtual ~MatroskaFileSink();

  Boolean continuePlaying();
  static void afterGettingFrame(void* clientData, unsigned packetDataSize,
                                unsigned numTruncatedBytes,
                                struct timeval presentationTime,
                                unsigned durationInMicroseconds);
  static void onSourceClosure(void* clientData);
  void onSourceClosure1();

private:
  friend class MatroskaSubsessionIOState;
  MediaSession& fInputSession;
  FILE* fOutFid;
  unsigned fBufferSize;
  Boolean fPacketLossCompensate;
  Boolean fSyncStreams;
  Boolean fAreCurrentlyBeingPlayed;
  unsigned fNumSubsessions, fNumSyncedSubsessions;
  Boolean fHaveCompletedOutputFile;
  unsigned short fMovieWidth, fMovieHeight;
  unsigned fMovieFPS;

  // Matroska-specific members
  u_int64_t fSegmentDataOffset;
  u_int64_t fCuesOffset;
  u_int64_t fSeekHeadOffset;
  unsigned fTimecodeScale; // in nanoseconds
  double fSegmentDuration;

  struct timeval fStartTime;
  Boolean fHaveSetStartTime;

  // Callback function pointers
  afterPlayingFunc* fAfterFunc;
  void* fAfterClientData;

private:
  // EBML/Matroska writing functions
  unsigned addByte(u_int8_t byte);
  unsigned addWord(u_int32_t word);
  unsigned add8ByteWord(u_int64_t word);
  unsigned addFloat(float value);
  unsigned addEBMLNumber(u_int64_t number);
  unsigned addEBMLId(u_int32_t id);
  unsigned addEBMLSize(u_int64_t size);
  unsigned addEBMLUnknownSize(unsigned numBytes);

  // Matroska structure writing
  void writeEBMLHeader();
  void writeSegmentHeader();
  void writeSeekHead();
  void writeSegmentInfo();
  void writeTracks();
  void writeCues();
  void completeOutputFile();

  // H.264 codec private data handling
  void extractH264CodecPrivateData(char const* spropParameterSets);

  // Track management
  unsigned fVideoTrackNumber, fAudioTrackNumber;
  Boolean fHaveVideoTrack, fHaveAudioTrack;
  char const* fVideoCodecId;
  char const* fAudioCodecId;
  unsigned fVideoWidth, fVideoHeight;
  unsigned fAudioSamplingFrequency, fAudioChannels;

  // H.264 codec private data
  unsigned char* fH264CodecPrivateData;
  unsigned fH264CodecPrivateDataSize;

  // Cluster management
  void startNewCluster(struct timeval presentationTime);
  u_int64_t fCurrentClusterOffset;
  struct timeval fCurrentClusterTimecode;
  Boolean fNeedNewCluster;
};

// A structure used to represent input substreams:
class MatroskaSubsessionIOState {
public:
  MatroskaSubsessionIOState(MatroskaFileSink& sink, MediaSubsession& subsession);
  virtual ~MatroskaSubsessionIOState();

  void setTrackNumber(unsigned trackNumber) { fTrackNumber = trackNumber; }
  unsigned trackNumber() const { return fTrackNumber; }

  void startPlaying(MatroskaFileSink::afterPlayingFunc* afterFunc, void* afterClientData);
  static void afterGettingFrame(void* clientData, unsigned packetDataSize,
				unsigned numTruncatedBytes,
				struct timeval presentationTime,
				unsigned durationInMicroseconds);
  void afterGettingFrame(unsigned packetDataSize, struct timeval presentationTime);
  void onSourceClosure();

public:
  MatroskaFileSink& fOurSink;
  MediaSubsession& fOurSubsession;

  unsigned char* fBuffer;
  unsigned fBufferSize;
  unsigned fTrackNumber;
  FramedSource* fSource; // source we actually read from (may be a framer)

public:
  Boolean fOurSourceIsActive;
private:
  struct timeval fPrevPresentationTime;

  // H.264 pending access-unit aggregation
  unsigned char* fPendingH264;
  unsigned fPendingH264Size;
  unsigned fPendingH264Capacity;
  Boolean fHasPendingH264;
  struct timeval fPendingH264PTS;

  MatroskaFileSink::afterPlayingFunc* fAfterFunc;
  void* fAfterClientData;

  void useFrame(unsigned char* frameSource, unsigned frameSize,
                struct timeval presentationTime);
  // For H.264: append a NAL (with 4-byte length prefix) to the pending AU buffer
  void processH264Frame(unsigned char* frameSource, unsigned frameSize);
  void appendH264NALToPending(unsigned char* frameSource, unsigned frameSize);
  void flushPendingH264();
  static void onSourceClosure(void* clientData);
};

#endif
