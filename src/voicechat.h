//-----------------------------------------------------------------------------
//
// Zandronum Source
// Copyright (C) 2023 Adam Kaminski
// Copyright (C) 2023 Zandronum Development Team
// All rights reserved.
//
// Redistribution and use in source and binary forms, with or without
// modification, are permitted provided that the following conditions are met:
//
// 1. Redistributions of source code must retain the above copyright notice,
//    this list of conditions and the following disclaimer.
// 2. Redistributions in binary form must reproduce the above copyright notice,
//    this list of conditions and the following disclaimer in the documentation
//    and/or other materials provided with the distribution.
// 3. Neither the name of the Zandronum Development Team nor the names of its
//    contributors may be used to endorse or promote products derived from this
//    software without specific prior written permission.
// 4. Redistributions in any form must be accompanied by information on how to
//    obtain complete source code for the software and any accompanying
//    software that uses the software. The source code must either be included
//    in the distribution or be available for no more than the cost of
//    distribution plus a nominal fee, and must be freely redistributable
//    under reasonable conditions. For an executable file, complete source
//    code means the source code for all modules it contains. It does not
//    include source code for modules or files that typically accompany the
//    major components of the operating system on which the executable file
//    runs.
//
// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
// AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
// IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
// ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE
// LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
// CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
// SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
// INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
// CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
// ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
// POSSIBILITY OF SUCH DAMAGE.
//
//
//
// Filename: voicechat.h
//
//-----------------------------------------------------------------------------

#ifndef __VOICECHAT_H__
#define __VOICECHAT_H__

#include "doomdef.h"
#include "c_cvars.h"
#include "networkshared.h"
#include "i_soundinternal.h"

// [AK] Only include FMOD, Opus, and RNNoise files if compiling with sound.
#ifndef NO_SOUND
#include "fmod_wrap.h"
#include "opus.h"
#include "rnnoise.h"
#endif

//*****************************************************************************
//	DEFINES

enum VOICECHAT_e
{
	// Voice chatting is disabled by the server.
	VOICECHAT_OFF,

	// Everyone can chat with each other.
	VOICECHAT_EVERYONE,

	// Players can only use voice chat amongst their teammates.
	VOICECHAT_TEAMMATESONLY,
};

//*****************************************************************************
enum VOICEMODE_e
{
	// Voice chatting is disabled by the client.
	VOICEMODE_OFF,

	// The player transmits audio by pressing down +voicerecord.
	VOICEMODE_PUSHTOTALK,

	// The player transmits audio based on voice activity.
	VOICEMODE_VOICEACTIVITY,
};

//*****************************************************************************
enum TRANSMISSIONTYPE_e
{
	// Not transmitting audio right now.
	TRANSMISSIONTYPE_OFF,

	// Transmitting audio by pressing a button (i.e. "voicerecord").
	TRANSMISSIONTYPE_BUTTON,

	// Transmitting audio based on voice activity.
	TRANSMISSIONTYPE_VOICEACTIVITY,
};

//*****************************************************************************
//	CLASSES

class VOIPController
{
public:
	static VOIPController &GetInstance( void ) { static VOIPController instance; return instance; }

// [AK] Some of these functions only exist as stubs if compiling without sound.
#ifdef NO_SOUND

	void Tick( void ) { }
	void StartTransmission( const TRANSMISSIONTYPE_e type, const bool getRecordPosition ) { }
	void StopTransmission( void ) { }
	bool IsVoiceChatAllowed( void ) const { return false; }
	bool IsPlayerTalking( const unsigned int player ) const { return false; }
	void SetVolume( float volume ) { }
	void SetPitch( float pitch ) { }
	void ListRecordDrivers( void ) const { }
	FString GrabStats( void ) const { return ""; }
	void ReceiveAudioPacket( const unsigned int player, const unsigned int frame, const unsigned char *data, const unsigned int length ) { }
	void UpdateProximityChat( void ) { }
	void RemoveVoIPChannel( const unsigned int player ) { }

private:
	VOIPController( void ) { }
	~VOIPController( void ) { }

#else

	void Init( FMOD::System *mainSystem );
	void Shutdown( void );
	void Activate( void );
	void Deactivate( void );
	void Tick( void );
	void StartTransmission( const TRANSMISSIONTYPE_e type, const bool getRecordPosition );
	void StopTransmission( void );
	bool IsVoiceChatAllowed( void ) const;
	bool IsPlayerTalking( const unsigned int player ) const;
	void SetVolume( float volume );
	void SetPitch( float pitch );
	void ListRecordDrivers( void ) const;
	FString GrabStats( void ) const;
	void ReceiveAudioPacket( const unsigned int player, const unsigned int frame, const unsigned char *data, const unsigned int length );
	void UpdateProximityChat( void );
	void RemoveVoIPChannel( const unsigned int player );

	// [AK] Static constants of the audio's properties.
	static const int RECORD_SAMPLE_RATE = 48000; // 48 kHz.
	static const int PLAYBACK_SAMPLE_RATE = 24000; // 24 kHz.
	static const int SAMPLE_SIZE = sizeof( float ); // 32-bit floating point, mono-channel.
	static const int RECORD_SOUND_LENGTH = RECORD_SAMPLE_RATE; // 1 second.
	static const int PLAYBACK_SOUND_LENGTH = PLAYBACK_SAMPLE_RATE; // 1 second.

	static const int READ_BUFFER_SIZE = 2048;

	// [AK] Static constants for encoding or decoding frames via Opus.
	static const int FRAME_SIZE = 10; // 10 ms.
	static const int RECORD_SAMPLES_PER_FRAME = ( RECORD_SAMPLE_RATE * FRAME_SIZE ) / 1000;
	static const int PLAYBACK_SAMPLES_PER_FRAME = ( PLAYBACK_SAMPLE_RATE * FRAME_SIZE ) / 1000;
	static const int MAX_PACKET_SIZE = 1276; // Recommended max packet size by Opus.

private:
	struct VOIPChannel
	{
		struct AudioFrame
		{
			unsigned int frame;
			float samples[PLAYBACK_SAMPLES_PER_FRAME];
		};

		const unsigned int player;
		TArray<AudioFrame> jitterBuffer;
		TArray<float> extraSamples;
		FMOD::Sound *sound;
		FMOD::Channel *channel;
		OpusDecoder *decoder;
		int playbackTick;
		unsigned int lastReadPosition;
		unsigned int lastPlaybackPosition;
		unsigned int lastFrameRead;
		unsigned int samplesRead;
		unsigned int samplesPlayed;
		unsigned int dspEpochHi;
		unsigned int dspEpochLo;
		unsigned int endDelaySamples;

		VOIPChannel( const unsigned int player );
		~VOIPChannel( void );

		bool ShouldPlayIn3DMode( void ) const;
		int GetUnreadSamples( void ) const;
		int DecodeOpusFrame( const unsigned char *inBuffer, const unsigned int inLength, float *outBuffer, const unsigned int outLength );
		void StartPlaying( void );
		void ReadSamples( unsigned char *soundBuffer, const unsigned int length );
		void Update3DAttributes( void );
		void UpdatePlayback( void );
		void UpdateEndDelay( const bool resetEpoch );
	};

	VOIPController( void );
	~VOIPController( void ) { Shutdown( ); }

	void ReadRecordSamples( unsigned char *soundBuffer, unsigned int length );
	int EncodeOpusFrame( const float *inBuffer, const unsigned int inLength, unsigned char *outBuffer, const unsigned int outLength );

	static FMOD_CREATESOUNDEXINFO CreateSoundExInfo( const unsigned int sampleRate, const unsigned int fileLength );
	static FMOD_RESULT F_CALLBACK ChannelCallback( FMOD_CHANNEL *channel, FMOD_CHANNEL_CALLBACKTYPE type, void *commanddata1, void *commanddata2 );

	VOIPChannel *VoIPChannels[MAXPLAYERS];
	FMOD::System *system;
	FMOD::Sound *recordSound;
	FMOD::ChannelGroup *VoIPChannelGroup;
	OpusEncoder *encoder;
	RNNModel *denoiseModel;
	DenoiseState *denoiseState;
	int recordDriverID;
	unsigned int framesSent;
	unsigned int lastRecordPosition;
	bool isInitialized;
	bool isActive;
	bool isRecordButtonPressed;
	TRANSMISSIONTYPE_e transmissionType;

	// [AK] This is necessasry for setting up the sound rolloff settings of all
	// VoIP channels that are played in 3D mode (i.e. proximity chat is used).
	// A pointer to this struct is used for the channel's user data, which the
	// custom callback function FMODSoundRenderer::RolloffCallback then uses to
	// calculate the sound's volume based on distance.
	FISoundChannel proximityInfo;

#endif // NO_SOUND

};

//*****************************************************************************
//	EXTERNAL CONSOLE VARIABLES

EXTERN_CVAR( Int, sv_allowvoicechat )
EXTERN_CVAR( Bool, sv_proximityvoicechat )

#endif // __VOICECHAT_H__
