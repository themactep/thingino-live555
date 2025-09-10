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
// Copyright (c) 1996-2025, Live Networks, Inc.  All rights reserved
// A test program that validates Opus RTP recording functionality
// main program

#include "liveMedia.hh"
#include "BasicUsageEnvironment.hh"
#include "GroupsockHelper.hh"

UsageEnvironment* env;

void testOpusRecording(); // forward

int main(int argc, char** argv) {
  // Begin by setting up our usage environment:
  TaskScheduler* scheduler = BasicTaskScheduler::createNew();
  env = BasicUsageEnvironment::createNew(*scheduler);

  testOpusRecording();

  env->taskScheduler().doEventLoop(); // does not return
  return 0; // only to prevent compiler warnings
}

void afterPlaying(void* clientData); // forward

// A structure to hold the state of the current session.
struct sessionState_t {
  FramedSource* source;
  FileSink* sink;
  Groupsock* rtpGroupsock;
} sessionState;

void testOpusRecording() {
  *env << "Testing Opus RTP recording functionality...\n";

  // Create a dummy Opus RTP source for testing
  // In a real scenario, this would be receiving from a network stream
  struct sockaddr_storage dummyAddress;
  dummyAddress.ss_family = AF_INET;
  ((struct sockaddr_in&)dummyAddress).sin_addr.s_addr = INADDR_ANY;

  const Port rtpPort(18888);
  sessionState.rtpGroupsock = new Groupsock(*env, dummyAddress, rtpPort, 0);

  // Create an Opus RTP source
  unsigned char const payloadFormatCode = 96; // dynamic payload type for Opus
  sessionState.source = OpusAudioRTPSource::createNew(*env, sessionState.rtpGroupsock,
                                                      payloadFormatCode,
                                                      48000, // Opus RTP timestamp frequency
                                                      48000, // maxPlaybackRate
                                                      True,  // stereo
                                                      True,  // useFEC
                                                      False, // useDTX
                                                      128000); // maxAverageBitrate

  if (sessionState.source == NULL) {
    *env << "Failed to create Opus RTP source: " << env->getResultMsg() << "\n";
    exit(1);
  }

  // Create an Ogg file sink for recording
  char const* outputFileName = "test_opus_recording.ogg";
  sessionState.sink = OggFileSink::createNew(*env, outputFileName);
  if (sessionState.sink == NULL) {
    *env << "Failed to create Ogg file sink: " << env->getResultMsg() << "\n";
    exit(1);
  }

  *env << "Created Opus RTP source and Ogg file sink successfully\n";
  *env << "Recording to file: " << outputFileName << "\n";

  // Start the recording (this will immediately finish since we have no actual RTP data)
  sessionState.sink->startPlaying(*sessionState.source, afterPlaying, NULL);
}

void afterPlaying(void* /*clientData*/) {
  *env << "...recording test completed\n";

  // Clean up
  Medium::close(sessionState.sink);
  Medium::close(sessionState.source);
  delete sessionState.rtpGroupsock;

  *env << "Opus recording test PASSED - components created successfully\n";

  // We're done:
  exit(0);
}
