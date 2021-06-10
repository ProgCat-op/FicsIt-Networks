﻿#include "FINNetworkTrace.h"

#include "FGPowerConnectionComponent.h"
#include "FGPowerInfoComponent.h"
#include "FGPowerCircuit.h"
#include "FGRailroadTimeTable.h"
#include "FGRailroadTrackConnectionComponent.h"
#include "FGRailroadVehicle.h"
#include "FGRailroadVehicleMovementComponent.h"
#include "FGTrain.h"
#include "FINNetworkCircuit.h"
#include "FINNetworkCircuitNode.h"
#include "FINNetworkComponent.h"
#include "Buildables/FGBuildableDockingStation.h"
#include "Buildables/FGBuildableRailroadSignal.h"
#include "Buildables/FGBuildableRailroadStation.h"
#include "Buildables/FGBuildableRailroadSwitchControl.h"
#include "Buildables/FGBuildableTrainPlatform.h"
#include "FicsItNetworks/Components/FINVehicleScanner.h"

#define StepFuncName(A, B) Step ## _ ## A ## _ ## B
#define StepRegName(A, B) StepReg ## _ ## A ## _ ## B
#define StepRegSigName(A, B) StepRegSig ## _ ## A ## _ ## B
#define StepFuncSig(A, B) bool StepFuncName(A, B)(UObject* oA, UObject* oB)
#define StepFuncSigReg(A, B) \
	TPair<TPair<UClass*, UClass*>, TPair<FString, FFINTraceStep*>> StepRegSigName(A, B)() { \
		return TPair<TPair<UClass*, UClass*>, TPair<FString, FFINTraceStep*>>{ \
			TPair<UClass*, UClass*>{ \
				A::StaticClass(), \
				B::StaticClass() \
			}, \
			TPair<FString, FFINTraceStep*>{ \
				#A "_" #B, \
				new FFINTraceStep(&StepFuncName(A, B)) \
			} \
		}; \
	}
#define Step(CA, CB, Code) \
	StepFuncSig(CA, CB); \
	StepFuncSigReg(CA, CB) \
	FFINTraceStepRegisterer StepRegName(CA, CB)(&StepRegSigName(CA, CB)); \
	StepFuncSig(CA, CB) { \
		CA* A = Cast<CA>(oA); \
		CB* B = Cast<CB>(oB); \
		Code \
	}

TSharedPtr<FFINTraceStep, ESPMode::ThreadSafe> FFINNetworkTrace::fallbackTraceStep;
TArray<TPair<TPair<UClass*, UClass*>, TPair<FString, FFINTraceStep*>>(*)()> FFINNetworkTrace::toRegister;
TMap<FString, TSharedPtr<FFINTraceStep, ESPMode::ThreadSafe>> FFINNetworkTrace::traceStepRegistry;
TMap<TSharedPtr<FFINTraceStep, ESPMode::ThreadSafe>, FString> FFINNetworkTrace::inverseTraceStepRegistry;
TMap<UClass*, TPair<TMap<UClass*, TSharedPtr<FFINTraceStep, ESPMode::ThreadSafe>>, TMap<UClass*, TSharedPtr<FFINTraceStep, ESPMode::ThreadSafe>>>> FFINNetworkTrace::traceStepMap;
TMap<UClass*, TPair<TMap<UClass*, TSharedPtr<FFINTraceStep, ESPMode::ThreadSafe>>, TMap<UClass*, TSharedPtr<FFINTraceStep, ESPMode::ThreadSafe>>>> FFINNetworkTrace::interfaceTraceStepMap;

class FFINTraceStepRegisterer {
public:
    FFINTraceStepRegisterer(TPair<TPair<UClass*, UClass*>, TPair<FString, FFINTraceStep*>>(*regSig)()) {
        FFINNetworkTrace::toRegister.Add(regSig);
    }
};

void traceRegisterSteps() {
	TPair<UClass*, FString> tests = TPair<UClass*, FString>{UFINNetworkComponent::StaticClass(), ""};
	for (auto& stepSig : FFINNetworkTrace::toRegister) {
		auto step = stepSig();
		FFINTraceStep* tStep = step.Value.Value;
		UClass* A = step.Key.Key;
		UClass* B = step.Key.Value;
		TMap<UClass*, TPair<TMap<UClass*, TSharedPtr<FFINTraceStep, ESPMode::ThreadSafe>>, TMap<UClass*, TSharedPtr<FFINTraceStep, ESPMode::ThreadSafe>>>>* AMap;
		if (A->GetSuperClass() == UInterface::StaticClass()) AMap = &FFINNetworkTrace::interfaceTraceStepMap;
		else AMap = &FFINNetworkTrace::traceStepMap;
		TMap<UClass*, TSharedPtr<FFINTraceStep, ESPMode::ThreadSafe>>* BMap;
		if (B->GetSuperClass() == UInterface::StaticClass()) BMap = &(*AMap).FindOrAdd(A).Value;
		else BMap = &(*AMap).FindOrAdd(A).Key;
		TSharedPtr<FFINTraceStep, ESPMode::ThreadSafe> stepPtr = TSharedPtr<FFINTraceStep, ESPMode::ThreadSafe>(tStep);
		(*BMap).FindOrAdd(B) = stepPtr;
		FFINNetworkTrace::traceStepRegistry.FindOrAdd(step.Value.Key) = stepPtr;
		FFINNetworkTrace::inverseTraceStepRegistry.FindOrAdd(stepPtr) = step.Value.Key;
	}
	FFINNetworkTrace::toRegister.Empty();
};

TSharedPtr<FFINTraceStep, ESPMode::ThreadSafe> findTraceStep2(TPair<TMap<UClass*, TSharedPtr<FFINTraceStep, ESPMode::ThreadSafe>>, TMap<UClass*, TSharedPtr<FFINTraceStep, ESPMode::ThreadSafe>>>& stepList, UClass* B) {
	UClass* Bi = B;
	while (Bi && Bi != UObject::StaticClass()) {
		auto stepB = stepList.Key.Find(Bi);
		if (stepB) {
			return *stepB;
		}
		Bi = Bi->GetSuperClass();
	}

	for (FImplementedInterface& interface : B->Interfaces) {
		auto stepB = stepList.Value.Find(interface.Class);
		if (stepB) {
			return *stepB;
		}
	}

	return nullptr;
}

TSharedPtr<FFINTraceStep, ESPMode::ThreadSafe> FFINNetworkTrace::findTraceStep(UClass* A, UClass* B) {
	if (!A || !B) return fallbackTraceStep;
	UClass* Ai = A;
	while (Ai && Ai != UObject::StaticClass()) {
		auto stepA = traceStepMap.Find(Ai);
		if (stepA) {
			TSharedPtr<FFINTraceStep, ESPMode::ThreadSafe> step = findTraceStep2(*stepA, B);
			if (step.IsValid()) return step;
		}
		Ai = Ai->GetSuperClass();
	}
	
	for (FImplementedInterface& interface : A->Interfaces) {
		auto stepA = interfaceTraceStepMap.Find(interface.Class);
		if (stepA) {
			TSharedPtr<FFINTraceStep, ESPMode::ThreadSafe> step = findTraceStep2(*stepA, B);
			if (step.IsValid()) return step;
		}
	}

	return fallbackTraceStep;
}

FFINNetworkTrace::FFINNetworkTrace(const FFINNetworkTrace& trace) {
	traceRegisterSteps();
	
	Prev = MakeShareable((trace.Prev) ? new FFINNetworkTrace(*trace.Prev) : nullptr);
	Step = trace.Step;
	Obj = trace.Obj;
}

FFINNetworkTrace& FFINNetworkTrace::operator=(const FFINNetworkTrace& trace) {
	Prev = MakeShareable((trace.Prev) ? new FFINNetworkTrace(*trace.Prev) : nullptr);
	Step = trace.Step;
	Obj = trace.Obj;

	return *this;
}

FFINNetworkTrace::FFINNetworkTrace() : FFINNetworkTrace(nullptr) {
	traceRegisterSteps();
}

FFINNetworkTrace::FFINNetworkTrace(UObject* Obj) : Obj(Obj) {
	traceRegisterSteps();
}

FFINNetworkTrace::~FFINNetworkTrace() {}

bool FFINNetworkTrace::Serialize(FStructuredArchive::FSlot Slot) {
	if (Slot.GetUnderlyingArchive().IsSaveGame()) {
		FStructuredArchive::FRecord Record = Slot.EnterRecord();
		UObject* ObjRaw = Obj.Get();
		Record.EnterField(SA_FIELD_NAME(TEXT("Ptr"))) << ObjRaw;
		Obj = ObjRaw;

		TOptional<FStructuredArchive::FSlot> PrevSlot = Record.TryEnterField(SA_FIELD_NAME(TEXT("Next")), Prev.IsValid());
		if (PrevSlot.IsSet()) {
			if (!Prev.IsValid()) Prev = MakeShared<FFINNetworkTrace>();
			Prev->Serialize(PrevSlot.GetValue());
		} else {
			Prev.Reset();
		}

		TOptional<FStructuredArchive::FSlot> StepSlot = Record.TryEnterField(SA_FIELD_NAME(TEXT("Step")), Step.IsValid());
		if (StepSlot.IsSet()) {
			FString StepName;
			if (Step.IsValid()) StepName = inverseTraceStepRegistry[Step];
			StepSlot.GetValue() << StepName;
			Step = traceStepRegistry[StepName];
		} else {
			Step.Reset();
		}
	}
	
	return true;
}

void FFINNetworkTrace::AddStructReferencedObjects(FReferenceCollector& ReferenceCollector) const {
	UObject* Ptr = Obj.GetEvenIfUnreachable();
	if (Ptr) {
		ReferenceCollector.AddReferencedObject(Ptr);
	}
	if (Prev) {
		Prev->AddStructReferencedObjects(ReferenceCollector);
	}
}

FFINNetworkTrace FFINNetworkTrace::operator/(UObject* other) const {
	FFINNetworkTrace trace(other);

	UObject* A = Obj.Get();
	if (!A || !other) return FFINNetworkTrace(nullptr); // if A is not valid, the network trace will always be not invalid
	trace.Prev = MakeShared<FFINNetworkTrace>(*this);
	trace.Step = findTraceStep(A->GetClass(), other->GetClass());
	return trace;
}

UObject* FFINNetworkTrace::operator*() const {
	UObject* B = Obj.Get();
	if (IsValid() && B) {
		return B;
	} else {
		return nullptr;
	}
}

UObject* FFINNetworkTrace::Get() const {
	return **this;
}

UObject* FFINNetworkTrace::operator->() const {
	return **this;
}

FFINNetworkTrace FFINNetworkTrace::operator()(UObject* other) const {
	if (!other) return FFINNetworkTrace(nullptr);

	FFINNetworkTrace trace(*this);
	trace.Obj = other;
	
	if (trace.Prev) {
		auto A = trace.Prev->Obj.Get();
		if (!A) return FFINNetworkTrace(nullptr); // if the previous network trace object is invalid, the trace will be always invalid
		trace.Step = findTraceStep(trace.Prev->Obj.Get()->GetClass(), other->GetClass());
	}

	return trace;
}

bool FFINNetworkTrace::operator==(const FFINNetworkTrace& other) const {
	return Obj == other.Obj;
}

void FFINNetworkTrace::CheckTrace() const {
	if (!**this) throw std::exception("Object is not reachable");
}

FFINNetworkTrace FFINNetworkTrace::Reverse() const {
	if (!Obj.IsValid()) return FFINNetworkTrace(nullptr);
	FFINNetworkTrace trace(Obj.Get());
	TSharedPtr<FFINNetworkTrace> prev;
	if (this->Prev) prev = MakeShared<FFINNetworkTrace>(*this->Prev);
	while (prev) {
		trace = trace / prev->Obj.Get();
		prev = prev->Prev;
	}
	return trace;
}

bool FFINNetworkTrace::IsValid() const {
	UObject* B = Obj.Get();
	if (!B) return false;
	if (Prev && Step && *Step) {
		UObject* A = Prev->Obj.Get();
		if (!A || !(*Step)(A, B)) return false;
	}
	if (Prev) {
		return Prev->IsValid();
	}
	return true;
}

bool FFINNetworkTrace::IsEqualObj(const FFINNetworkTrace& other) const {
	return Obj == other.Obj;
}

bool FFINNetworkTrace::operator<(const FFINNetworkTrace& other) const {
	struct TWOP {
		int32		ObjectIndex;
		int32		ObjectSerialNumber;
	};
	TWOP* d1 = (TWOP*)&Obj;
	TWOP* d2 = (TWOP*)&other.Obj;
	if (d1->ObjectIndex < d2->ObjectIndex) return true;
	else return d1->ObjectSerialNumber < d2->ObjectSerialNumber;
}

const FWeakObjectPtr& FFINNetworkTrace::GetUnderlyingPtr() const {
	return Obj;
}

FWeakObjectPtr FFINNetworkTrace::GetStartPtr() const {
	const FFINNetworkTrace* Trace = this;
	while (Trace->Prev) Trace = Trace->Prev.Get();
	if (Trace) return Trace->Obj.Get();
	return nullptr;
}

FFINNetworkTrace::operator bool() const {
	return IsValid();
}

/* ############### */
/* # Trace Steps # */
/* ############### */

Step(UFGPowerConnectionComponent, UFGPowerCircuit, {
	return A->GetCircuitID() == B->GetCircuitID();
})
Step(UFGPowerCircuit, UFGPowerConnectionComponent, {
	return A->GetCircuitID() == B->GetCircuitID();
})

Step(UFGPowerInfoComponent, UFGPowerCircuit, {
	return A->GetPowerCircuit()->GetCircuitID() == B->GetCircuitID();
})
Step(UFGPowerCircuit, UFGPowerInfoComponent, {
	return A->GetCircuitID() == B->GetPowerCircuit()->GetCircuitID();
})

Step(UFGPowerInfoComponent, UFGPowerConnectionComponent, {
	return A == B->GetPowerInfo();
})
Step(UFGPowerConnectionComponent, UFGPowerInfoComponent, {
	return A->GetPowerInfo() == B;
})

Step(UFINNetworkComponent, UFINNetworkComponent, {
	return IFINNetworkCircuitNode::Execute_GetCircuit(oA)->HasNode(oB);
})

Step(UFINNetworkComponent, AFINNetworkCircuit, {
	return B->HasNode(oA);
})
Step(AFINNetworkCircuit, UFINNetworkComponent, {
	return A->HasNode(oB);
})

Step(AFGBuildableTrainPlatform, AFGBuildableTrainPlatform, {
	return A->GetTrackGraphID() == B->GetTrackGraphID();
})

Step(AFGBuildableTrainPlatform, AFGRailroadVehicle, {
	return A->GetTrackGraphID() == B->GetTrackGraphID();
})
Step(AFGRailroadVehicle, AFGBuildableTrainPlatform, {
	return A->GetTrackGraphID() == B->GetTrackGraphID();
})

Step(AFGRailroadVehicle, AFGTrain, {
	return A->GetTrackGraphID() == B->GetTrackGraphID();
})
Step(AFGTrain, AFGRailroadVehicle, {
	return A->GetTrackGraphID() == B->GetTrackGraphID();
})

Step(AFGTrain, AFGRailroadTimeTable, {
	return A->GetTimeTable() == B;
})
Step(AFGRailroadTimeTable, AFGTrain, {
    return A == B->GetTimeTable();
})

Step(AFGRailroadVehicle, AFGRailroadVehicle, {
	return A->GetTrackGraphID() == B->GetTrackGraphID();
})

Step(AFGRailroadVehicle, UFGRailroadVehicleMovementComponent, {
	return A->GetRailroadVehicleMovementComponent() == B;
});
Step(UFGRailroadVehicleMovementComponent, AFGRailroadVehicle, {
    return A == B->GetRailroadVehicleMovementComponent();
});

Step(AFGBuildableRailroadTrack, UFGRailroadTrackConnectionComponent, {
	return A == B->GetTrack();
})
Step(UFGRailroadTrackConnectionComponent, AFGBuildableRailroadTrack, {
    return A->GetTrack() == B;
})

Step(UFGRailroadTrackConnectionComponent, UFGRailroadTrackConnectionComponent, {
	return A->GetConnections().Contains(B) || A->GetOpposite() == B;
})

Step(UFGRailroadTrackConnectionComponent, AFGBuildableRailroadSwitchControl, {
	return A->GetSwitchControl() == B;
})
Step(AFGBuildableRailroadSwitchControl, UFGRailroadTrackConnectionComponent, {
    return A == B->GetSwitchControl();
})

Step(UFGRailroadTrackConnectionComponent, AFGBuildableRailroadSignal, {
    return A->GetSignal() == B;
})
Step(AFGBuildableRailroadSignal, UFGRailroadTrackConnectionComponent, {
    return A == B->GetSignal();
})

Step(UFGRailroadTrackConnectionComponent, AFGBuildableRailroadStation, {
    return A->GetStation() == B;
})
Step(AFGBuildableRailroadStation, UFGRailroadTrackConnectionComponent, {
    return A == B->GetStation();
})

Step(AFGVehicle, AFGBuildableDockingStation, {
	return false;
})
Step(AFGBuildableDockingStation, AFGVehicle, {
	return false;
})

Step(AFGVehicle, AFINVehicleScanner, {
	TArray<AActor*> Actors;
	B->VehicleCollision->GetOverlappingActors(Actors);
	return Actors.Contains(A);
})
Step(AFINVehicleScanner, AFGVehicle, {
    TArray<AActor*> Actors;
    A->VehicleCollision->GetOverlappingActors(Actors);
    return Actors.Contains(B);
})