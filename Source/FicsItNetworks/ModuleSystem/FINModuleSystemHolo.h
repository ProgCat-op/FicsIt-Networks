#pragma once

#include "CoreMinimal.h"
#include "FINModuleSystemPanel.h"
#include "Hologram/FGBuildableHologram.h"
#include "FINModuleSystemHolo.generated.h"

UCLASS()
class FICSITNETWORKS_API AFINModuleSystemHolo : public AFGBuildableHologram {
	GENERATED_BODY()

public:
	UPROPERTY()
	UFINModuleSystemPanel* Snapped = nullptr;
	FVector SnappedLoc;
	int SnappedRot;

	//UPROPERTY(Replicated)
	bool bIsValid = false;
	bool bOldIsValid = false;

	AFINModuleSystemHolo();
	~AFINModuleSystemHolo();

	// Begin AActor
	virtual void Tick(float DeltaSeconds) override;
	// End AActor

	// Begin AFGHologram
	virtual AActor* Construct(TArray<AActor*>& out_children, FNetConstructionID constructionID) override;
	virtual bool IsValidHitResult(const FHitResult& hit) const override;
	virtual bool TrySnapToActor(const FHitResult& hitResult) override;
	virtual void SetHologramLocationAndRotation(const FHitResult& hit) override;
	virtual void CheckValidPlacement() override;
	// End AFGHologram

private:
	bool checkSpace(FVector min, FVector max);
	FVector getModuleSize();

	//UFUNCTION(NetMulticast, Unreliable)
	//void ValidChanged(bool bNewValid);
};