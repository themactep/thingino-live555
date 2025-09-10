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
// A test program that validates Opus interoperability with real RTSP streams
// main program

#include "liveMedia.hh"
#include "BasicUsageEnvironment.hh"

UsageEnvironment* env;
char eventLoopWatchVariable = 0;

// Test configuration
char const* rtspURL = "rtsp://thingino:thingino@192.168.88.76:554/ch0";
char const* outputFileName = "opus_interop_test.ogg";
unsigned testDurationSeconds = 30; // Test for 30 seconds

// Forward declarations
void openURL(char const* url);
void setupNextSubsession(RTSPClient* rtspClient);
void subsessionAfterPlaying(void* clientData);
void subsessionByeHandler(void* clientData, char const* reason);
void sessionAfterPlaying(void* clientData = NULL);
void shutdown(int exitCode = 1);

// RTSP client implementation
class OpusTestRTSPClient: public RTSPClient {
public:
  static OpusTestRTSPClient* createNew(UsageEnvironment& env, char const* rtspURL,
                                       int verbosityLevel = 0,
                                       char const* applicationName = NULL,
                                       portNumBits tunnelOverHTTPPortNum = 0);

protected:
  OpusTestRTSPClient(UsageEnvironment& env, char const* rtspURL,
                     int verbosityLevel, char const* applicationName,
                     portNumBits tunnelOverHTTPPortNum);
  virtual ~OpusTestRTSPClient();

public:
  MediaSession* session;
  MediaSubsessionIterator* iter;
  Boolean madeProgress;
};

// Implementation
OpusTestRTSPClient* OpusTestRTSPClient::createNew(UsageEnvironment& env, char const* rtspURL,
                                                  int verbosityLevel,
                                                  char const* applicationName,
                                                  portNumBits tunnelOverHTTPPortNum) {
  return new OpusTestRTSPClient(env, rtspURL, verbosityLevel, applicationName, tunnelOverHTTPPortNum);
}

OpusTestRTSPClient::OpusTestRTSPClient(UsageEnvironment& env, char const* rtspURL,
                                       int verbosityLevel, char const* applicationName,
                                       portNumBits tunnelOverHTTPPortNum)
  : RTSPClient(env, rtspURL, verbosityLevel, applicationName, tunnelOverHTTPPortNum, -1),
    session(NULL), iter(NULL), madeProgress(False) {
}

OpusTestRTSPClient::~OpusTestRTSPClient() {
  Medium::close(session);
  delete iter;
}

// Response handlers
void continueAfterDESCRIBE(RTSPClient* rtspClient, int resultCode, char* resultString);
void continueAfterSETUP(RTSPClient* rtspClient, int resultCode, char* resultString);
void continueAfterPLAY(RTSPClient* rtspClient, int resultCode, char* resultString);

int main(int argc, char** argv) {
  // Begin by setting up our usage environment:
  TaskScheduler* scheduler = BasicTaskScheduler::createNew();
  env = BasicUsageEnvironment::createNew(*scheduler);

  *env << "Opus Interoperability Test\n";
  *env << "Testing with RTSP URL: " << rtspURL << "\n";
  *env << "Recording to: " << outputFileName << "\n";
  *env << "Test duration: " << testDurationSeconds << " seconds\n\n";

  // Open the RTSP URL:
  openURL(rtspURL);

  // Set up a timer to end the test after the specified duration
  env->taskScheduler().scheduleDelayedTask(testDurationSeconds * 1000000,
                                           (TaskFunc*)sessionAfterPlaying, NULL);

  // All subsequent activity takes place within the event loop:
  env->taskScheduler().doEventLoop(&eventLoopWatchVariable);

  return 0;
}

void openURL(char const* url) {
  OpusTestRTSPClient* rtspClient = OpusTestRTSPClient::createNew(*env, url, 1);
  if (rtspClient == NULL) {
    *env << "Failed to create RTSP client for URL \"" << url << "\": " << env->getResultMsg() << "\n";
    shutdown();
    return;
  }

  // Send a RTSP "DESCRIBE" command:
  rtspClient->sendDescribeCommand(continueAfterDESCRIBE);
}

void continueAfterDESCRIBE(RTSPClient* rtspClient, int resultCode, char* resultString) {
  do {
    if (resultCode != 0) {
      *env << "Failed to get a SDP description: " << resultString << "\n";
      delete[] resultString;
      break;
    }

    char* const sdpDescription = resultString;
    *env << "Got a SDP description:\n" << sdpDescription << "\n";

    // Create a media session object from this SDP description:
    OpusTestRTSPClient* client = (OpusTestRTSPClient*)rtspClient;
    client->session = MediaSession::createNew(*env, sdpDescription);
    delete[] sdpDescription;
    if (client->session == NULL) {
      *env << "Failed to create a MediaSession object from the SDP description: " << env->getResultMsg() << "\n";
      break;
    } else if (!client->session->hasSubsessions()) {
      *env << "This session has no media subsessions (i.e., no \"m=\" lines)\n";
      break;
    }

    // Then, create and set up our data source objects for the session:
    client->iter = new MediaSubsessionIterator(*client->session);
    setupNextSubsession(rtspClient);
    return;
  } while (0);

  shutdown();
}

void setupNextSubsession(RTSPClient* rtspClient) {
  OpusTestRTSPClient* client = (OpusTestRTSPClient*)rtspClient;
  MediaSubsession* subsession = client->iter->next();
  if (subsession != NULL) {
    if (!subsession->initiate()) {
      *env << "Failed to initiate the \"" << *subsession << "\" subsession: " << env->getResultMsg() << "\n";
      setupNextSubsession(rtspClient); // give up on this subsession; go to the next one
    } else {
      *env << "Initiated the \"" << *subsession << "\" subsession";
      if (subsession->rtcpIsMuxed()) {
        *env << " (client port " << subsession->clientPortNum() << ")";
      } else {
        *env << " (client ports " << subsession->clientPortNum() << "-" << subsession->clientPortNum()+1 << ")";
      }
      *env << "\n";

      // Check if this is an Opus audio subsession
      if (strcmp(subsession->mediumName(), "audio") == 0 &&
          strcmp(subsession->codecName(), "OPUS") == 0) {
        *env << "Found Opus audio subsession!\n";
        *env << "  Codec: " << subsession->codecName() << "\n";
        *env << "  Sampling frequency: " << subsession->rtpTimestampFrequency() << " Hz\n";
        *env << "  Channels: " << subsession->numChannels() << "\n";

        // Test SDP attribute parsing
        *env << "  SDP attributes:\n";
        *env << "    maxplaybackrate: " << subsession->attrVal_unsigned("maxplaybackrate") << "\n";
        *env << "    stereo: " << (subsession->attrVal_bool("stereo") ? "yes" : "no") << "\n";
        *env << "    useinbandfec: " << (subsession->attrVal_bool("useinbandfec") ? "yes" : "no") << "\n";
        *env << "    usedtx: " << (subsession->attrVal_bool("usedtx") ? "yes" : "no") << "\n";
        *env << "    maxaveragebitrate: " << subsession->attrVal_unsigned("maxaveragebitrate") << "\n";

        // Create a file sink for this subsession
        subsession->sink = OggFileSink::createNew(*env, outputFileName);
        if (subsession->sink == NULL) {
          *env << "Failed to create file sink for Opus subsession: " << env->getResultMsg() << "\n";
        } else {
          *env << "Created Ogg file sink for Opus recording\n";
        }
      }

      // Continue setting up this subsession, by sending a RTSP "SETUP" command:
      rtspClient->sendSetupCommand(*subsession, continueAfterSETUP);
    }
    return;
  }

  // We've finished setting up all of the subsessions. Now, send a RTSP "PLAY" command to start the streaming:
  if (client->session->absStartTime() != NULL) {
    rtspClient->sendPlayCommand(*client->session, continueAfterPLAY, client->session->absStartTime(), client->session->absEndTime());
  } else {
    rtspClient->sendPlayCommand(*client->session, continueAfterPLAY);
  }
}

void continueAfterSETUP(RTSPClient* rtspClient, int resultCode, char* resultString) {
  do {
    if (resultCode != 0) {
      *env << "Failed to set up the subsession: " << resultString << "\n";
      break;
    }

    *env << "Set up the subsession\n";

    // Set up the next subsession, if any:
    setupNextSubsession(rtspClient);
    return;
  } while (0);

  delete[] resultString;
  shutdown();
}

void continueAfterPLAY(RTSPClient* rtspClient, int resultCode, char* resultString) {
  Boolean success = False;

  do {
    if (resultCode != 0) {
      *env << "Failed to start playing session: " << resultString << "\n";
      break;
    }

    *env << "Started playing session...\n";

    // Start receiving data for each Opus subsession
    OpusTestRTSPClient* client = (OpusTestRTSPClient*)rtspClient;
    MediaSubsessionIterator iter(*client->session);
    MediaSubsession* subsession;
    while ((subsession = iter.next()) != NULL) {
      if (subsession->sink != NULL) {
        *env << "Starting to receive data for \"" << *subsession << "\" subsession\n";
        subsession->sink->startPlaying(*(subsession->readSource()),
                                       subsessionAfterPlaying, subsession);
        client->madeProgress = True;
      }
    }

    success = True;
  } while (0);

  delete[] resultString;

  if (!success) {
    shutdown();
  }
}

void subsessionAfterPlaying(void* clientData) {
  MediaSubsession* subsession = (MediaSubsession*)clientData;
  *env << "Subsession \"" << *subsession << "\" ended\n";

  Medium::close(subsession->sink);
  subsession->sink = NULL;

  sessionAfterPlaying();
}

void subsessionByeHandler(void* clientData, char const* reason) {
  *env << "Received RTCP \"BYE\"";
  if (reason != NULL) {
    *env << " (reason:\"" << reason << "\")";
  }
  *env << "\n";

  sessionAfterPlaying();
}

void sessionAfterPlaying(void* /*clientData*/) {
  *env << "\nOpus interoperability test completed!\n";
  *env << "Check the output file: " << outputFileName << "\n";
  eventLoopWatchVariable = 1;
}

void shutdown(int exitCode) {
  *env << "Shutting down...\n";
  eventLoopWatchVariable = 1;
}
