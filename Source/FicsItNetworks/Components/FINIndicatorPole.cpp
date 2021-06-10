﻿#include "FINIndicatorPole.h"
#include "FGColoredInstanceMeshProxy.h"

AFINIndicatorPole::AFINIndicatorPole() {
	Indicator = CreateDefaultSubobject<UStaticMeshComponent>("Indicator");
	Indicator->AttachToComponent(RootComponent, FAttachmentTransformRules::KeepRelativeTransform);
	
	Connector = CreateDefaultSubobject<UFINAdvancedNetworkConnectionComponent>("Connector");
	Connector->AttachToComponent(RootComponent, FAttachmentTransformRules::KeepRelativeTransform);

	SetActorTickEnabled(true);
	PrimaryActorTick.SetTickFunctionEnable(true);
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.bStartWithTickEnabled = true;
}

void AFINIndicatorPole::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const {
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	
	DOREPLIFETIME(AFINIndicatorPole, Height);
	DOREPLIFETIME(AFINIndicatorPole, IndicatorColor);
	DOREPLIFETIME(AFINIndicatorPole, EmessiveStrength);
}

void AFINIndicatorPole::OnConstruction(const FTransform& transform) {
	CreatePole();

	Super::OnConstruction(transform);
}

void AFINIndicatorPole::BeginPlay() {
	Super::BeginPlay();
	
	CreatePole();

	if (Indicator->GetMaterials().Num() > 0) {
		IndicatorInstance = UMaterialInstanceDynamic::Create(Cast<UMaterialInstanceDynamic>(Indicator->GetMaterial(0))->Parent, nullptr);
		Indicator->SetMaterial(0, IndicatorInstance);
	}
}

void AFINIndicatorPole::TickActor(float DeltaTime, ELevelTick TickType, FActorTickFunction& ThisTickFunction) {
	Super::TickActor(DeltaTime, TickType, ThisTickFunction);
	
	if (bHasChanged) {
		bHasChanged = false;
		UpdateEmessive();
		ForceNetUpdate();
	}
}

bool AFINIndicatorPole::ShouldSave_Implementation() const {
	return true;
}

int32 AFINIndicatorPole::GetDismantleRefundReturnsMultiplier() const {
	return Height + 6;
}

void AFINIndicatorPole::CreatePole() {
	// Clean up
	Poles.Empty();

	// Construction
	for (int i = 0; i < Height; ++i) {
		UFGColoredInstanceMeshProxy* Pole = NewObject<UFGColoredInstanceMeshProxy>(this);
		check(Pole);
		Pole->AttachToComponent(Indicator, FAttachmentTransformRules::KeepRelativeTransform);
		Pole->SetRelativeLocation(FVector(0,0, -(i) * 100.0));
		Pole->RegisterComponent();
		Pole->CreationMethod = EComponentCreationMethod::UserConstructionScript;
		Pole->SetStaticMesh(LongPole);
		Pole->SetMobility(EComponentMobility::Static);
		//Pole->SetColorSlot(mColorSlot);
		Poles.Add(Pole);
	}

	ReapplyColorSlot();
}

void AFINIndicatorPole::UpdateEmessive_Implementation() {
	if (IndicatorInstance) {
		IndicatorInstance->SetVectorParameterValue("Emissive Color", IndicatorColor);
		IndicatorInstance->SetScalarParameterValue("Emissive Strenght", EmessiveStrength);
	}
}

void AFINIndicatorPole::netFunc_setColor(float r, float g, float b, float e) {
	FLinearColor oldColor = IndicatorColor;
	float oldEmissive = EmessiveStrength;
	IndicatorColor.R = FMath::Clamp(r, 0.0f, 1.0f);
	IndicatorColor.G = FMath::Clamp(g, 0.0f, 1.0f);
	IndicatorColor.B = FMath::Clamp(b, 0.0f, 1.0f);
	EmessiveStrength = FMath::Clamp(e, 0.0f, 5.0f);
	netSig_ColorChanged(oldColor.R, oldColor.G, oldColor.B, oldEmissive);
	bHasChanged = true;
}

void AFINIndicatorPole::netFunc_getColor(float& r, float& g, float& b, float& e) {
	r = IndicatorColor.R;
	g = IndicatorColor.G;
	b = IndicatorColor.B;
	e = EmessiveStrength;
}

void AFINIndicatorPole::netSig_ColorChanged(float r, float g, float b, float e) {}

AFINIndicatorPole* AFINIndicatorPole::netFunc_getTopPole() {
	if (!IsValid(TopConnected)) TopConnected = nullptr;
	return TopConnected;
}

AFINIndicatorPole* AFINIndicatorPole::netFunc_getBottomPole() {
	if (!IsValid(BottomConnected)) BottomConnected = nullptr;
	return BottomConnected;
}
