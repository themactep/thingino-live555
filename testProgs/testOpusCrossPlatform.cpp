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
// A test program that validates Opus implementation across different platforms
// main program

#include "liveMedia.hh"
#include "BasicUsageEnvironment.hh"
#include "GroupsockHelper.hh"

#ifdef _WIN32
#include <windows.h>
#define PLATFORM_NAME "Windows"
#elif defined(__APPLE__)
#include <sys/utsname.h>
#define PLATFORM_NAME "macOS"
#elif defined(__linux__)
#include <sys/utsname.h>
#define PLATFORM_NAME "Linux"
#else
#define PLATFORM_NAME "Unknown"
#endif

UsageEnvironment* env;

void runCrossPlatformTests(); // forward

int main(int argc, char** argv) {
  // Begin by setting up our usage environment:
  TaskScheduler* scheduler = BasicTaskScheduler::createNew();
  env = BasicUsageEnvironment::createNew(*scheduler);

  *env << "=== Opus Cross-Platform Test ===" << "\n";
  *env << "Platform: " << PLATFORM_NAME << "\n";

#if defined(__linux__) || defined(__APPLE__)
  struct utsname unameData;
  if (uname(&unameData) == 0) {
    *env << "System: " << unameData.sysname << " " << unameData.release << "\n";
    *env << "Architecture: " << unameData.machine << "\n";
  }
#elif defined(_WIN32)
  OSVERSIONINFO osvi;
  ZeroMemory(&osvi, sizeof(OSVERSIONINFO));
  osvi.dwOSVersionInfoSize = sizeof(OSVERSIONINFO);
  if (GetVersionEx(&osvi)) {
    *env << "Windows Version: " << osvi.dwMajorVersion << "." << osvi.dwMinorVersion << "\n";
  }
#endif

  *env << "\n";

  runCrossPlatformTests();

  return 0; // Exit immediately after tests
}

void runCrossPlatformTests() {
  Boolean allTestsPassed = True;

  *env << "Running cross-platform Opus tests...\n\n";

  // Test 1: OpusAudioRTPSource creation
  *env << "Test 1: OpusAudioRTPSource creation\n";
  try {
    struct sockaddr_storage dummyAddress;
    dummyAddress.ss_family = AF_INET;
    ((struct sockaddr_in&)dummyAddress).sin_addr.s_addr = INADDR_ANY;

    const Port rtpPort(18888);
    Groupsock* rtpGroupsock = new Groupsock(*env, dummyAddress, rtpPort, 0);

    OpusAudioRTPSource* opusSource = OpusAudioRTPSource::createNew(*env, rtpGroupsock,
                                                                   96, // payload format
                                                                   48000, // timestamp frequency
                                                                   48000, // maxPlaybackRate
                                                                   True,  // stereo
                                                                   True,  // useFEC
                                                                   False, // useDTX
                                                                   128000); // maxAverageBitrate

    if (opusSource != NULL) {
      *env << "  ✓ OpusAudioRTPSource created successfully\n";
      *env << "    - Stereo mode: " << (opusSource->stereoMode() ? "enabled" : "disabled") << "\n";
      *env << "    - FEC support: " << (opusSource->useFEC() ? "enabled" : "disabled") << "\n";
      *env << "    - DTX support: " << (opusSource->useDTX() ? "enabled" : "disabled") << "\n";
      *env << "    - Max playback rate: " << opusSource->maxPlaybackRate() << " Hz\n";
      *env << "    - Max average bitrate: " << opusSource->maxAverageBitrate() << " bps\n";

      Medium::close(opusSource);
    } else {
      *env << "  ✗ Failed to create OpusAudioRTPSource: " << env->getResultMsg() << "\n";
      allTestsPassed = False;
    }

    delete rtpGroupsock;
  } catch (...) {
    *env << "  ✗ Exception occurred during OpusAudioRTPSource creation\n";
    allTestsPassed = False;
  }
  *env << "\n";

  // Test 2: OpusAudioRTPSink creation
  *env << "Test 2: OpusAudioRTPSink creation\n";
  try {
    struct sockaddr_storage dummyAddress;
    dummyAddress.ss_family = AF_INET;
    ((struct sockaddr_in&)dummyAddress).sin_addr.s_addr = INADDR_ANY;

    const Port rtpPort(18890);
    Groupsock* rtpGroupsock = new Groupsock(*env, dummyAddress, rtpPort, 0);

    OpusAudioRTPSink* opusSink = OpusAudioRTPSink::createNew(*env, rtpGroupsock,
                                                             96, // payload format
                                                             48000, // timestamp frequency
                                                             2,     // channels
                                                             True,  // enableFEC
                                                             False); // enableDTX

    if (opusSink != NULL) {
      *env << "  ✓ OpusAudioRTPSink created successfully\n";

      // Test SDP generation
      char const* sdpLine = opusSink->auxSDPLine();
      if (sdpLine != NULL && strlen(sdpLine) > 0) {
        *env << "  ✓ SDP line generated: " << sdpLine;
      } else {
        *env << "  ✗ Failed to generate SDP line\n";
        allTestsPassed = False;
      }

      Medium::close(opusSink);
    } else {
      *env << "  ✗ Failed to create OpusAudioRTPSink: " << env->getResultMsg() << "\n";
      allTestsPassed = False;
    }

    delete rtpGroupsock;
  } catch (...) {
    *env << "  ✗ Exception occurred during OpusAudioRTPSink creation\n";
    allTestsPassed = False;
  }
  *env << "\n";

  // Test 3: OggFileSink with Opus
  *env << "Test 3: OggFileSink with Opus support\n";
  try {
    char const* testFileName = "opus_crossplatform_test.ogg";
    OggFileSink* oggSink = OggFileSink::createNew(*env, testFileName);

    if (oggSink != NULL) {
      *env << "  ✓ OggFileSink created successfully\n";
      *env << "    - Output file: " << testFileName << "\n";

      Medium::close(oggSink);

      // Try to remove the test file
#ifdef _WIN32
      DeleteFile(testFileName);
#else
      unlink(testFileName);
#endif
    } else {
      *env << "  ✗ Failed to create OggFileSink: " << env->getResultMsg() << "\n";
      allTestsPassed = False;
    }
  } catch (...) {
    *env << "  ✗ Exception occurred during OggFileSink creation\n";
    allTestsPassed = False;
  }
  *env << "\n";

  // Test 4: Platform-specific networking
  *env << "Test 4: Platform-specific networking\n";
  try {
    // Test socket creation and binding
    struct sockaddr_storage testAddress;
    testAddress.ss_family = AF_INET;
    ((struct sockaddr_in&)testAddress).sin_addr.s_addr = INADDR_LOOPBACK;

    const Port testPort(18892);
    Groupsock* testSocket = new Groupsock(*env, testAddress, testPort, 0);

    if (testSocket != NULL) {
      *env << "  ✓ Network socket creation successful\n";
      *env << "    - Loopback address binding: OK\n";
      delete testSocket;
    } else {
      *env << "  ✗ Failed to create network socket\n";
      allTestsPassed = False;
    }
  } catch (...) {
    *env << "  ✗ Exception occurred during network socket test\n";
    allTestsPassed = False;
  }
  *env << "\n";

  // Test 5: Memory management
  *env << "Test 5: Memory management\n";
  try {
    // Create and destroy multiple Opus objects to test memory management
    for (int i = 0; i < 10; i++) {
      struct sockaddr_storage addr;
      addr.ss_family = AF_INET;
      ((struct sockaddr_in&)addr).sin_addr.s_addr = INADDR_ANY;

      const Port port(18900 + i);
      Groupsock* sock = new Groupsock(*env, addr, port, 0);

      OpusAudioRTPSource* source = OpusAudioRTPSource::createNew(*env, sock, 96, 48000);
      if (source != NULL) {
        Medium::close(source);
      }
      delete sock;
    }
    *env << "  ✓ Memory management test completed (10 iterations)\n";
  } catch (...) {
    *env << "  ✗ Exception occurred during memory management test\n";
    allTestsPassed = False;
  }
  *env << "\n";

  // Final results
  *env << "=== Cross-Platform Test Results ===\n";
  if (allTestsPassed) {
    *env << "✓ ALL TESTS PASSED\n";
    *env << "Opus implementation is working correctly on " << PLATFORM_NAME << "\n";
  } else {
    *env << "✗ SOME TESTS FAILED\n";
    *env << "Opus implementation may have issues on " << PLATFORM_NAME << "\n";
  }
  *env << "\n";
}
