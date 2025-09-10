/**
 * Basic test for Opus audio RTP source and sink functionality
 * This test validates that our Opus implementation can be instantiated
 * and basic functionality works without requiring the full live555 build.
 */

#include <iostream>
#include <cstdlib>

// Include necessary live555 headers
#include "liveMedia.hh"
#include "BasicUsageEnvironment.hh"
#include "OpusAudioRTPSource.hh"
#include "OpusAudioRTPSink.hh"

int main(int argc, char* argv[]) {
    std::cout << "=== Basic Opus Audio RTP Test ===" << std::endl;

    // Create basic usage environment
    TaskScheduler* scheduler = BasicTaskScheduler::createNew();
    UsageEnvironment* env = BasicUsageEnvironment::createNew(*scheduler);

    std::cout << "Created usage environment" << std::endl;

    // Test Opus RTP Source creation
    try {
        // Create a dummy groupsock for testing
        struct sockaddr_storage dummyAddr;
        memset(&dummyAddr, 0, sizeof(dummyAddr));
        dummyAddr.ss_family = AF_INET;

        Groupsock* rtpGroupsock = new Groupsock(*env, dummyAddr, 0, 255);

        std::cout << "Created groupsock" << std::endl;

        // Test OpusAudioRTPSource creation
        OpusAudioRTPSource* opusSource = OpusAudioRTPSource::createNew(
            *env, rtpGroupsock,
            96,     // RTP payload format
            48000,  // RTP timestamp frequency
            48000,  // max playback rate
            True,   // stereo
            True,   // use FEC
            False,  // use DTX
            128000  // max average bitrate
        );

        if (opusSource != NULL) {
            std::cout << "✓ OpusAudioRTPSource created successfully" << std::endl;

            // Test basic accessors
            std::cout << "  - Max playback rate: " << opusSource->maxPlaybackRate() << std::endl;
            std::cout << "  - Stereo: " << (opusSource->isStereo() ? "Yes" : "No") << std::endl;
            std::cout << "  - FEC enabled: " << (opusSource->useFEC() ? "Yes" : "No") << std::endl;
            std::cout << "  - DTX enabled: " << (opusSource->useDTX() ? "Yes" : "No") << std::endl;
            std::cout << "  - Max average bitrate: " << opusSource->maxAverageBitrate() << std::endl;

            Medium::close(opusSource);
        } else {
            std::cout << "✗ Failed to create OpusAudioRTPSource" << std::endl;
            return 1;
        }

        // Test OpusAudioRTPSink creation
        OpusAudioRTPSink* opusSink = OpusAudioRTPSink::createNew(
            *env, rtpGroupsock,
            96,     // RTP payload format
            48000,  // RTP timestamp frequency
            2,      // num channels
            True,   // enable FEC
            False   // enable DTX
        );

        if (opusSink != NULL) {
            std::cout << "✓ OpusAudioRTPSink created successfully" << std::endl;

            // Test basic functionality
            std::cout << "  - OpusAudioRTPSink created with payload format: " << opusSink->rtpPayloadType() << std::endl;

            Medium::close(opusSink);
        } else {
            std::cout << "✗ Failed to create OpusAudioRTPSink" << std::endl;
            return 1;
        }

        delete rtpGroupsock;

    } catch (const std::exception& e) {
        std::cout << "✗ Exception caught: " << e.what() << std::endl;
        return 1;
    } catch (...) {
        std::cout << "✗ Unknown exception caught" << std::endl;
        return 1;
    }

    std::cout << "✓ All basic Opus tests passed!" << std::endl;

    // Cleanup
    env->reclaim();
    delete scheduler;

    return 0;
}
