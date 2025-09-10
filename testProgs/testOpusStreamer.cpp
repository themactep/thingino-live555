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
// A test program that streams an Opus audio file via RTP/RTCP
// main program

#include "liveMedia.hh"
#include "BasicUsageEnvironment.hh"
#include "announceURL.hh"
#include "GroupsockHelper.hh"

UsageEnvironment* env;

void play(); // forward

int main(int argc, char** argv) {
  // Begin by setting up our usage environment:
  TaskScheduler* scheduler = BasicTaskScheduler::createNew();
  env = BasicUsageEnvironment::createNew(*scheduler);

  play();

  env->taskScheduler().doEventLoop(); // does not return
  return 0; // only to prevent compiler warnings
}

char const* inputFileName = "test.opus";

void afterPlaying(void* clientData); // forward

// A structure to hold the state of the current session.
// It is used in the "afterPlaying()" function to clean up the session.
struct sessionState_t {
  FramedSource* source;
  RTPSink* sink;
  RTCPInstance* rtcpInstance;
  Groupsock* rtpGroupsock;
  Groupsock* rtcpGroupsock;
  RTSPServer* rtspServer;
} sessionState;

void play() {
  // Open the file as a 'ByteStream' file source:
  // (Note: For a real Opus streaming application, you would typically
  // use a more sophisticated source that can parse Opus packets)
  ByteStreamFileSource* fileSource = ByteStreamFileSource::createNew(*env, inputFileName);
  if (fileSource == NULL) {
    *env << "Unable to open file \"" << inputFileName
         << "\" as a byte-stream file source: "
         << env->getResultMsg() << "\n";
    exit(1);
  }

  sessionState.source = fileSource;

  // Create 'groupsocks' for RTP and RTCP:
  struct sockaddr_storage destinationAddress;
  destinationAddress.ss_family = AF_INET;
  ((struct sockaddr_in&)destinationAddress).sin_addr.s_addr = chooseRandomIPv4SSMAddress(*env);
  // Note: This is a multicast address.  If you wish instead to stream
  // using unicast, then you should use the "testOnDemandRTSPServer"
  // test program - not this test program - as a model.

  const unsigned short rtpPortNum = 18888;
  const unsigned short rtcpPortNum = rtpPortNum+1;
  const unsigned char ttl = 255;

  const Port rtpPort(rtpPortNum);
  const Port rtcpPort(rtcpPortNum);

  sessionState.rtpGroupsock
    = new Groupsock(*env, destinationAddress, rtpPort, ttl);
  sessionState.rtcpGroupsock
    = new Groupsock(*env, destinationAddress, rtcpPort, ttl);

  // Create an Opus RTP sink from the RTP 'groupsock':
  unsigned char const payloadFormatCode = 96; // dynamic payload type for Opus
  sessionState.sink = OpusAudioRTPSink::createNew(*env, sessionState.rtpGroupsock,
                                                  payloadFormatCode,
                                                  48000, // Opus RTP timestamp frequency is always 48kHz
                                                  2,     // stereo
                                                  False, // FEC
                                                  False); // DTX
  if (sessionState.sink == NULL) {
    *env << "Failed to create Opus RTP sink: " << env->getResultMsg() << "\n";
    exit(1);
  }

  // Create (and start) a 'RTCP instance' for this RTP sink:
  const unsigned estimatedSessionBandwidth = 160; // in kbps; for RTCP b/w share
  const unsigned maxCNAMElen = 100;
  unsigned char CNAME[maxCNAMElen+1];
  gethostname((char*)CNAME, maxCNAMElen);
  CNAME[maxCNAMElen] = '\0'; // just in case
  sessionState.rtcpInstance
    = RTCPInstance::createNew(*env, sessionState.rtcpGroupsock,
                              estimatedSessionBandwidth, CNAME,
                              sessionState.sink, NULL /* we're a server */,
                              True /* we're a SSM source */);
  // Note: This starts RTCP running automatically

  // Create and start an RTSP server to serve this stream:
  sessionState.rtspServer = RTSPServer::createNew(*env, 8554);
  if (sessionState.rtspServer == NULL) {
    *env << "Failed to create RTSP server: " << env->getResultMsg() << "\n";
    exit(1);
  }
  ServerMediaSession* sms
    = ServerMediaSession::createNew(*env, "opusStream", inputFileName,
                                    "Session streamed by \"testOpusStreamer\"",
                                    True /*SSM*/);
  sms->addSubsession(PassiveServerMediaSubsession::createNew(*sessionState.sink,
                                                             sessionState.rtcpInstance));
  sessionState.rtspServer->addServerMediaSession(sms);

  announceURL(sessionState.rtspServer, sms);

  // Start the streaming:
  *env << "Beginning streaming...\n";
  sessionState.sink->startPlaying(*sessionState.source, afterPlaying, NULL);
}

void afterPlaying(void* /*clientData*/) {
  *env << "...done streaming\n";

  // End by closing the media:
  Medium::close(sessionState.rtcpInstance); // Note: Sends a RTCP BYE
  Medium::close(sessionState.sink);
  Medium::close(sessionState.source);
  delete sessionState.rtpGroupsock;
  delete sessionState.rtcpGroupsock;

  // We're done:
  exit(0);
}
