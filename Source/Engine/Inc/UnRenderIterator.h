// UnRenderIterator.h

struct ActorBuffer
{
	char Buffer[ sizeof(AActor) ];
};

/*-----------------------------------------------------------------------------
	URenderIterator.
-----------------------------------------------------------------------------*/

class ENGINE_API URenderIterator : public UObject
{
	DECLARE_CLASS(URenderIterator,UObject,0)

	INT				MaxItems;
	INT				Index;
	APlayerPawn*	Observer;
	struct FSceneNode* Frame;	// v436-compat port: the frame being rendered, valid between Init and UnInit.

	// Constructor.
	URenderIterator();

	// URenderIterator interface
	// v436-compat port: v436 initializes the iterator with the scene frame (v400 took the camera
	// pawn) and releases it with UnInit, as URender::SetupDynamics does around each iteration.
	virtual void Init( FSceneNode* Camera );	//override to initialize subclass data (call Super required)
	virtual void UnInit();						//must be called before Camera goes out of scope.
	virtual void First();
	virtual void Next();
	virtual bool IsDone();						//override to adjust iteration termination criteria (call Super recommended)
	virtual AActor* CurrentItem();				//override to adjust actor render properties based on Index (call Super recommended)
};

/*----------------------------------------------------------------------------
	FActorNode.
----------------------------------------------------------------------------*/

class ENGINE_API FActorNode
{
public:
	ActorBuffer ActorProxy;
	FActorNode* NextNode;

	// Constructors.
	FActorNode(): NextNode( NULL ) {}
	~FActorNode() 
	{
		if( NextNode != NULL )
			delete NextNode;
	}
};

// end of UnRenderIterator.h
