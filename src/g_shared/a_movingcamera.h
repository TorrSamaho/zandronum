#ifndef __A_MOVINGCAMERA__
#define __A_MOVINGCAMERA__

#include "actor.h"
// [BB] New #includes.
#include "network.h"
#include "farchive.h"

class AInterpolationPoint : public AActor
{
	DECLARE_CLASS (AInterpolationPoint, AActor)
	HAS_OBJECT_POINTERS
public:
	void BeginPlay ();
	void HandleSpawnFlags ();
	void Tick () {}		// Nodes do no thinking
	AInterpolationPoint *ScanForLoop ();
	void FormChain ();

	void Serialize (FArchive &arc);

	TObjPtr<AInterpolationPoint> Next;
};

class APathFollower : public AActor
{
	DECLARE_CLASS (APathFollower, AActor)
	HAS_OBJECT_POINTERS
public:
	void BeginPlay ();
	void PostBeginPlay ();
	void Tick ();
	void Activate (AActor *activator);
	void Deactivate (AActor *activator);
protected:
	float Splerp (float p1, float p2, float p3, float p4);
	float Lerp (float p1, float p2);
	virtual bool Interpolate ();
	virtual void NewNode ();

	void Serialize (FArchive &arc);

	bool bActive, bJustStepped;
	TObjPtr<AInterpolationPoint> PrevNode, CurrNode;
	float Time;		// Runs from 0.0 to 1.0 between CurrNode and CurrNode->Next
	int HoldTime;

	bool bPostBeginPlayCalled;
	bool bActivateCalledBeforePostBeginPlay;

	unsigned short ServerPrevNodeId, ServerCurrNodeId;
	float fServerTime;
public:
	bool IsActive () const;
	void SyncWithClient ( const ULONG ulClient );
	static void InitFromStream ( BYTESTREAM_s *pByteStream );
};

#endif
